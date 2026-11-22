#!/usr/bin/env python3
"""M4.1c conformance: `arcforge validate` (C) vs the Python oracle.

The whole non-raster half of the CLI -- expression evaluation, .px
parsing, extends/variants, compile_asset, recipe/style documents,
similarity, registry cross-checks, frame states -- must reproduce
`tools/arcforge_ref.py validate` BYTE-FOR-BYTE: same stdout, same exit
code (0 PASS / 1 FAIL; an oracle traceback is an exit 1 with EMPTY
stdout), plain and under -fsanitize=undefined,address, twice in a row
(arcforge AGENTS rule 8: Python stays the oracle until M4.3).

Parts:
  1. build both binaries (plain + sanitizers), zero warnings;
  2. the real corpus: examples/*.json + tests/invalid/*.json;
  3. a deterministic mutation batch on a private copy of the arcforge
     tree (build/m41c_sandbox): schema/type errors, shape/px faults,
     variant/extends faults (incl. cycles and unreadable targets),
     mutated registry style + recipe documents, and crash-equivalence
     cases (unparsable input, non-dict docs, NaN token, bad UTF-8,
     empty file) where the oracle tracebacks and prints nothing;
  4. CLI contract: usage errors exit 2, an unreadable input is exit 1
     with empty stdout on both sides;
  5. coverage assertions: the batch keeps producing the diagnostics it
     exists for (E_* codes and PASS/FAIL/status shapes).

Known, deliberate divergences (spec'd; counted as `known`, never as a
failure):
  * extends target that is unparsable: Python raises JSONDecodeError
    with its own message text; the C parser reports its own text --
    the REPORTED shape (exit 1 report containing the message) matches;
  * duplicate object keys / integers beyond int64 / the parser's error
    texts elsewhere (tests/test_c_json.py pins that layer);
  * non-ASCII identifiers, \\N{...} escapes, hex-float literals and the
    RecursionError band in expressions (tests/test_expr_parity.py).

Exit code 0 = all checks hold.
"""
import copy
import json
import locale
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import arcforge_ref as A          # noqa: E402

fails = 0
known_count = 0
SAN = {"ASAN_OPTIONS": "detect_leaks=0",
       "PATH": os.environ.get("PATH", "/usr/bin:/bin")}


def check(name, ok, detail=""):
    global fails
    print(f"{'PASS' if ok else 'FAIL'}  {name} {detail}")
    fails += 0 if ok else 1


def run(cmd, cwd, env=None):
    p = subprocess.run(cmd, cwd=str(cwd), capture_output=True, env=env)
    return p.stdout, p.returncode, p.stderr


# --- 1. build both binaries -------------------------------------------------
for extra, binname in (([], "arcforge"), (["SAN=1"], "arcforge_san")):
    p = subprocess.run(["make", "-C", str(ROOT / "c")] + extra,
                       capture_output=True, text=True)
    check(f"build {binname} clean (-Wall -Wextra -Werror)",
          p.returncode == 0 and "warning" not in (p.stdout + p.stderr).lower(),
          (p.stdout + p.stderr)[-400:])

PY = [sys.executable, str(ROOT / "tools" / "arcforge_ref.py"), "validate"]
BIN = str(ROOT / "build" / "arcforge")
BIN_SAN = str(ROOT / "build" / "arcforge_san")

check("oracle reads files as UTF-8 (locale caveat)",
      locale.getpreferredencoding(False).lower().replace("-", "") == "utf8",
      locale.getpreferredencoding(False))


def compare(label, rel, cwd, known=()):
    """One oracle-vs-C comparison; returns the oracle's stdout (for
    coverage)."""
    global known_count
    o1, r1, _ = run(PY + [rel], cwd)
    for binname, binary, env in (("plain", BIN, None),
                                 ("san", BIN_SAN, SAN),
                                 ("plain-2", BIN, None)):
        o2, r2, e2 = run([binary, "validate", rel], cwd, env)
        san = (b"AddressSanitizer" in e2 or b"runtime error" in e2)
        if (o1, r1) == (o2, r2) and not san:
            continue
        if label in known and r1 == r2 and bool(o1) == bool(o2) and not san:
            if binname == "plain":
                known_count += 1
                print(f"    --- KNOWN {label}: message text differs")
            continue
        detail = f"{binname}: py rc={r1} c rc={r2}"
        if o1 == o2:
            detail += " stderr=" + e2.decode(errors="replace")[-200:]
        else:
            import difflib
            d = list(difflib.unified_diff(
                o1.decode(errors="replace").splitlines(),
                o2.decode(errors="replace").splitlines(),
                "py", "c", lineterm="", n=1))
            detail += "\n" + "\n".join(d[:30])
        check(f"{label} [{binname}]", False, detail)
        return o1
    check(f"{label} (byte-identical, plain+SAN+deterministic)", True)
    return o1


# --- 2. the real corpus -----------------------------------------------------
corpus = sorted((ROOT / "examples").glob("*.json")) + \
         sorted((ROOT / "tests" / "invalid").glob("*.json"))
codes = set()
for f in corpus:
    rel = str(f.relative_to(ROOT))
    out = compare(f"corpus {rel}", rel, ROOT)
    try:
        for d in json.loads(out).get("diagnostics", []):
            codes.add(d["code"])
    except Exception:
        pass

# --- 3. the mutation batch on a private tree copy ---------------------------
SB = ROOT / "build" / "m41c_sandbox"
KNOWN = {"extends-not-json"}          # JSONDecodeError text (spec'd)


def setup_sandbox():
    if SB.is_dir():
        shutil.rmtree(SB)
    SB.mkdir(parents=True)
    for d in ("schema", "registry", "examples", "tools"):
        shutil.copytree(ROOT / d, SB / d,
                        ignore=shutil.ignore_patterns("__pycache__"))
    (SB / "build").mkdir()
    for b in ("arcforge", "arcforge_san"):
        shutil.copy(ROOT / "build" / b, SB / "build" / b)
    inv = ROOT / "tests" / "invalid" / "fragments"
    if inv.is_dir():
        for p in inv.glob("*.px"):
            shutil.copy(p, SB / "examples" / "fragments" / p.name)


def sb_write(name, content):
    path = SB / name
    if isinstance(content, bytes):
        path.write_bytes(content)
    elif isinstance(content, str):
        path.write_text(content)
    else:
        path.write_text(json.dumps(content, indent=1))
    return name


def mutate(doc, path, value, delete=False):
    d = copy.deepcopy(doc)
    cur = d
    for k in path[:-1]:
        cur = cur[k]
    if delete:
        del cur[path[-1]]
    else:
        cur[path[-1]] = value
    return d


def build_cases():
    base = {}
    for name in ("hero.scalable_auto.json", "hero.iron_knight.json",
                 "hero.drawn.json"):
        base[name] = json.loads((ROOT / "examples" / name).read_text())
    auto, ik, drawn = (base["hero.scalable_auto.json"],
                       base["hero.iron_knight.json"],
                       base["hero.drawn.json"])
    cases = []

    # A: schema / type errors
    A_CASES = [
        ("arcforge-old", ["arcforge"], "1.0", False),
        ("asset-int", ["asset"], 5, False),
        ("asset-id-int", ["asset", "id"], 123, False),
        ("asset-id-missing", ["asset", "id"], None, True),
        ("target-list", ["target"], [], False),
        ("canvas-str", ["target", "canvas"], "big", False),
        ("canvas-small", ["target", "canvas"], [32, 32], False),
        ("canvas-float", ["target", "canvas"], [48.5, 46], False),
        ("style-unknown", ["style_pack"], "arcade_16x", False),
        ("style-missing", ["style_pack"], None, True),
        ("style-int", ["style_pack"], 7, False),
        ("arch-name-unknown", ["archetype", "name"], "humanoid_x", False),
        ("arch-name-int", ["archetype", "name"], 9, False),
        ("arch-size-null", ["archetype", "size"], None, False),
        ("arch-size-huge", ["archetype", "size", "height"], 99999999999, False),
        ("arch-size-neg", ["archetype", "size", "height"], -5, False),
        ("features-str", ["features"], "nope", False),
        ("feature-int", ["features", 0], 5, False),
        ("feature-recipe-unknown", ["features", 0, "recipe"], "helmet", False),
        ("param-glow-int", ["features", 0, "params", "glow"], 7, False),
        ("param-reach-low", ["features", 2, "params", "reach"], 5, False),
        ("param-reach-hi", ["features", 2, "params", "reach"], 500, False),
        ("param-color-bad", ["features", 2, "params", "color"], "purple", False),
        ("param-extra", ["features", 0, "params", "sparkle"], 1, False),
        ("param-missing", ["features", 2, "params"], None, True),
        ("unknown-field", ["visual"], "free text", False),
        ("tests-int", ["tests"], 5, False),
        ("test-noassert", ["tests", 0], {}, False),
        ("animations-int", ["animations"], 5, False),
        ("anim-template-unknown", ["animations", "idle", "template"], "fly", False),
        ("arch-metrics-bad", ["archetype", "metrics"], [["x"]], False),
        ("arch-badramp", ["archetype", "params", "armor_ramp"], "nope", False),
    ]
    for label, path, val, dele in A_CASES:
        cases.append((label, "mut_auto.json", mutate(auto, path, val, dele)))

    # B: shapes / custom / px
    B_CASES = [
        ("custom-anchor-bad", ["custom", 0, "anchor"], "hand_x", False),
        ("custom-layer-bad", ["custom", 0, "layer"], "nowhere", False),
        ("custom-file-missing", ["custom", 0, "file"], "fragments/nope.px", False),
        ("custom-file-badpx", ["custom", 0, "file"], "fragments/bad_frames.px", False),
        ("custom-file-dir", ["custom", 0, "file"], "fragments", False),
        ("custom-rotate-range", ["custom", 0, "rotate"], 400, False),
        ("custom-rotate-str", ["custom", 0, "rotate"], "90", False),
        ("custom-fill-bad", ["custom", 0, "fill"], "steel.ultra", False),
        ("custom-fill-int", ["custom", 0, "fill"], 5, False),
        ("custom-when-bad", ["custom", 0, "when"], "(", False),
        ("custom-mirror-bad", ["custom", 0, "mirror"], "y", False),
        ("shape-rect-float", ["custom", 0, "w"], 2.5, False),
    ]
    for label, path, val, dele in B_CASES:
        cases.append((label, "mut_ik.json", mutate(ik, path, val, dele)))
    d = copy.deepcopy(ik)
    d["custom"][0] = {"kind": "rect", "x": 1, "y": 1, "w": 2, "h": 2}
    cases.append(("shape-missing-fill", "mut_ik2.json", d))

    # C: variants / extends
    C_CASES = [
        ("var-dup", ["variants", 0, "id"], "hero.drawn.small", False),
        ("var-from-unknown", ["variants", 0, "from"], "ghost", False),
        ("var-from-int", ["variants", 0, "from"], 5, False),
        ("var-id-missing", ["variants", 0, "id"], None, True),
        ("var-entry-str", ["variants", 0], "oops", False),
        ("var-override-list", ["variants", 0, "override"], [], False),
        ("var-variants-null", ["variants"], None, False),
        ("var-palette-bad", ["variants", 0, "palette_swap"], {"red": "nope"}, False),
    ]
    for label, path, val, dele in C_CASES:
        cases.append((label, "mut_drawn.json", mutate(drawn, path, val, dele)))
    sb_write("base_bad.json", "{not json")
    sb_write("base_utf8.json", b'{"arcforge": "\xff\xfe"}')
    sb_write("base_int.json", "5")
    # the cycle pair: BOTH files must exist before either side runs
    # (each case below also rewrites its own file with the same content)
    sb_write("mut_ext6.json", json.dumps(
        {"arcforge": "2.1", "asset": {"id": "e6"},
         "extends": "mut_ext7.json"}))
    sb_write("mut_ext7.json", json.dumps(
        {"arcforge": "2.1", "asset": {"id": "e7"},
         "extends": "mut_ext6.json"}))
    for label, name, target in (
            ("extends-missing", "mut_ext1.json", "ghost.json"),
            ("extends-self", "mut_ext2.json", "mut_ext2.json"),
            ("extends-not-json", "mut_ext3.json", "base_bad.json"),
            ("extends-badutf8", "mut_ext4.json", "base_utf8.json"),
            ("extends-asset-int", "mut_ext5.json", "base_int.json")):
        cases.append((label, name, json.dumps(
            {"arcforge": "2.1", "asset": {"id": name[4:-5]},
             "extends": target})))
    cases.append(("extends-cycle", "mut_ext6.json", json.dumps(
        {"arcforge": "2.1", "asset": {"id": "e6"},
         "extends": "mut_ext7.json"})))
    cases.append(("extends-cycle-b", "mut_ext7.json", json.dumps(
        {"arcforge": "2.1", "asset": {"id": "e7"},
         "extends": "mut_ext6.json"})))

    # D: registry documents (style + recipe), mutated in the sandbox tree
    style = json.loads((ROOT / "registry" / "styles" /
                        "arcade_16.style.json").read_text())
    S_CASES = [
        ("style-outline-int", ["outline"], 5, False),
        ("style-inner-missing", ["outline", "inner"], None, True),
        ("style-mode-str", ["outline", "inner", "mode"], "colors", False),
        ("style-parts-int", ["outline", "inner", "parts"], 3, False),
        ("style-part-null", ["outline", "inner", "parts", 0], None, False),
        ("style-ramps-int", ["ramps"], 5, False),
        ("style-ramp-tone-bad", ["ramps", "steel"], {"nope": [1, 2, 3]}, False),
        ("style-arcforge", ["arcforge"], "2.1", False),
    ]
    rec_path, rec_doc = None, None
    for p in sorted((ROOT / "registry" / "recipes").rglob("*.recipe.json")):
        doc = json.loads(p.read_text())
        if isinstance(doc.get("geometry"), dict) and doc.get("recipe") in (
                "helmet_closed", "boots_heavy", "energy_sword",
                "shoulder_armor_heavy"):
            rec_path, rec_doc = p, doc
            break
    R_CASES = [
        ("rec-applies-str", ["applies_to"], "x", False),
        ("rec-applies-null", ["applies_to"], None, False),
        ("rec-geo-int", ["geometry"], 5, False),
        ("rec-geo-extra", ["geometry", "humanoid_front_scalable"], {}, False),
        ("rec-recipe-int", ["recipe"], 5, False),
        ("rec-arcforge", ["arcforge"], "2.1", False),
    ]

    # E: crash-equivalence (oracle traceback == empty stdout, exit 1)
    E_CASES = [
        ("not-json", "{not json"),
        ("doc-int", "5"),
        ("doc-list", "[1, 2]"),
        ("asset-null-doc", '{"asset": null}'),
        ("asset-str-doc", '{"asset": "x"}'),
        ("nan-token", '{"arcforge": NaN}'),
        ("bad-utf8", b'{"arcforge": "2.1", "asset": {"id": "\xff\xfe"}}'),
        ("empty-file", ""),
        ("variants-int-doc",
         '{"arcforge": "2.1", "asset": {"id": "v"}, "variants": 5}'),
        ("recipe-str-doc", '{"recipe": 5}'),
        ("style-null-doc", '{"style_pack": null, "ramps": {}}'),
        ("style-outline-str",
         '{"style_pack": "s", "ramps": {}, "outline": "x"}'),
        ("arch-params-null",
         '{"arcforge": "2.1", "asset": {"id": "ap"}, "archetype": '
         '{"name": "humanoid_front_scalable", "params": null}}'),
        ("frame-state-bad", '{"arcforge": "2.1", "asset": {"id": "fs"}, '
                            '"frames": 5}'),
        ("no-arcforge", None),                       # built below
        ("missing-file", "__absent__.json"),         # special-cased
        ("directory-input", "."),                    # special-cased
    ]
    noaf = copy.deepcopy(auto)
    del noaf["arcforge"]
    return cases, S_CASES, style, (rec_path, rec_doc), R_CASES, E_CASES, noaf


def run_mutations():
    global known_count
    setup_sandbox()
    cases, S_CASES, style, (rec_path, rec_doc), R_CASES, E_CASES, noaf = \
        build_cases()
    target = "examples/hero.scalable_auto.json"

    def one(label, rel, known=()):
        global known_count
        o1, r1, _ = run(PY + [rel], SB)
        try:                                   # coverage: what the oracle says
            for d in json.loads(o1).get("diagnostics", []):
                codes.add(d["code"])
        except Exception:
            pass
        for binname, binary, env in (("plain", BIN, None),
                                     ("san", BIN_SAN, SAN)):
            o2, r2, e2 = run([binary, "validate", rel], SB, env)
            san = (b"AddressSanitizer" in e2 or b"runtime error" in e2)
            if (o1, r1) == (o2, r2) and not san:
                continue
            if label in known and r1 == r2 and bool(o1) == bool(o2) \
                    and not san:
                if binname == "plain":
                    known_count += 1
                    print(f"    --- KNOWN {label}: message text differs")
                continue
            import difflib
            if o1 and o2:
                d = list(difflib.unified_diff(
                    o1.decode(errors="replace").splitlines(),
                    o2.decode(errors="replace").splitlines(),
                    "py", "c", lineterm="", n=1))
                detail = "\n".join(d[:30])
            else:
                detail = (f"py rc={r1} out={o1[:200]!r} "
                          f"c rc={r2} out={o2[:200]!r} "
                          f"err={e2.decode(errors='replace')[-200:]!r}")
            check(f"mut {label} [{binname}]", False, detail)
            return
        check(f"mut {label} (byte-identical, plain+SAN)", True)

    for label, name, content in cases:
        one(label, sb_write(name, content), KNOWN)

    # D: style pack, mutated in place in the sandbox registry
    style_path = SB / "registry" / "styles" / "arcade_16.style.json"
    style_orig = style_path.read_text()
    for label, path, val, dele in S_CASES:
        doc = (copy.deepcopy(style) if dele and path == ["outline", "inner"]
               else mutate(style, path, val, dele))
        style_path.write_text(json.dumps(doc, indent=1))
        one(label, target)
    style_path.write_text(style_orig)

    # D: recipe, mutated in place in the sandbox registry
    sb_rec = SB / rec_path.relative_to(ROOT)
    rec_orig = sb_rec.read_text()
    for label, path, val, dele in R_CASES:
        sb_rec.write_text(json.dumps(mutate(rec_doc, path, val, dele),
                                     indent=1))
        one(label, target)
    sb_rec.write_text(rec_orig)

    # E: crash-equivalence + CLI input failures
    for label, content in E_CASES:
        if label == "no-arcforge":
            one(label, sb_write("mut_noaf.json", noaf), KNOWN)
        elif label == "missing-file":
            one(label, content)
        elif label == "directory-input":
            one(label, content)
        else:
            one(label, sb_write("mut_e_" + label + ".json", content), KNOWN)


run_mutations()

# --- 4. CLI contract --------------------------------------------------------
o, r, e = run([BIN, "validate"], ROOT)
check("usage: `arcforge validate` (no argument) exits 2", r == 2,
      f"rc={r} {e[:120]!r}")
o, r, e = run([BIN, "not-a-command"], ROOT)
check("usage: unknown subcommand exits 2", r == 2, f"rc={r}")
o, r, e = run([BIN, "validate", "__absent__.json"], ROOT)
o2, r2, _ = run(PY + ["__absent__.json"], ROOT)
check("unreadable input: both sides exit 1 with empty stdout",
      r == 1 and o == b"" and r2 == 1 and o2 == b"",
      f"c rc={r} out={o[:80]!r} py rc={r2} out={o2[:80]!r}")
o, r, e = run([BIN_SAN, "validate", "__absent__.json"], ROOT, SAN)
check("unreadable input (SAN): exit 1, empty stdout, no sanitizer report",
      r == 1 and o == b"" and b"AddressSanitizer" not in e,
      f"rc={r} {e[:120]!r}")

# --- 5. coverage ------------------------------------------------------------
need_codes = {
    "E_UNKNOWN_STYLE_PACK", "E_UNKNOWN_ARCHETYPE", "E_ARCHETYPE_EXPR",
    "E_RECIPE_EXPR", "E_PARAM_RANGE", "E_PARAM_ENUM", "E_MISSING_ANCHOR",
    "E_UNKNOWN_LAYER", "E_RECIPE_GEOMETRY", "E_UNKNOWN_ANIMATION_TEMPLATE",
    "E_SCHEMA", "E_EXTENDS_CYCLE", "E_EXTENDS_PATH", "E_UNKNOWN_VARIANT",
    "E_UNKNOWN_RECIPE", "E_UNKNOWN_RAMP", "E_SLOT_CONFLICT",
}
missing = sorted(need_codes - codes)
check("diagnostic codes exercised (corpus + mutations)", not missing,
      f"missing {missing}" if missing else f"{len(codes)} distinct codes")

print(f"\nknown divergences reported: {known_count}")
print(f"\n{'ALL GREEN' if not fails else str(fails) + ' FAILED'}")
sys.exit(1 if fails else 0)
