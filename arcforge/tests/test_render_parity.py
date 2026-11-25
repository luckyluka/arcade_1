#!/usr/bin/env python3
"""M4.2 conformance: the C rasterizer vs the Python oracle.

The raster half of the pipeline -- drawables -> RGBA frames, the outline
post-pass, the auto cell, the frame SHA-256 hashes -- must reproduce
`tools/arcforge_ref.py` BYTE-FOR-BYTE for the whole corpus:

  * the observable contract per file is (exit code, stdout bytes):

        DOC <id>
        CELL <W> <H>
        FRAME <name>/<i> <sha64> clipped=<n>

    with everything buffered on both sides, so any failure is rc 1 with
    EMPTY stdout (the oracle's uncaught exception shape);
  * plain and under -fsanitize=undefined,address, twice in a row
    (arcforge AGENTS rules 4 and 8: integer-only, deterministic, and the
    Python tool stays the oracle until M4.3);
  * and C's frame hashes must reproduce every tests/golden/*.json.

Parts:
  1. build both harnesses (plain + sanitizers), zero warnings;
  2. the real corpus: examples/*.json + tests/invalid/*.json, plus a
     style pack and a recipe document (valid documents that carry no
     asset IR: rc 1, empty stdout on both sides) and the CLI input
     failures;
  3. the goldens;
  4. coverage: auto canvas, shaded style, inner outline, rotate and
     frame-state tracks, 7-bit ASCII output.

Host cc, C11, no external libraries: tests/render_harness.c links the
ARCFORGE_LIB objects (json, schema, expr, sim, px, validate, raster,
sha256).
"""
import difflib
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import arcforge_ref as A          # noqa: E402

fails = 0
SAN = {"ASAN_OPTIONS": "detect_leaks=0",
       "PATH": os.environ.get("PATH", "/usr/bin:/bin")}

HARN = str(ROOT / "build" / "arcforge_render_harness")
HARN_SAN = str(ROOT / "build" / "arcforge_render_harness_san")

# what the corpus must exercise (all measured by the oracle below)
cov = {"docs": 0, "frames": 0, "auto": 0, "shaded": 0, "inner": 0,
       "rotate": 0, "params": 0, "swap": 0}


def check(name, ok, detail=""):
    global fails
    print(f"{'PASS' if ok else 'FAIL'}  {name} {detail}")
    fails += 0 if ok else 1


def run(cmd, cwd, env=None):
    p = subprocess.run(cmd, cwd=str(cwd), capture_output=True, env=env)
    return p.stdout, p.returncode, p.stderr


# --- 1. build both harnesses ------------------------------------------------
for extra, binname in (([], "arcforge_render_harness"),
                       (["SAN=1"], "arcforge_render_harness_san")):
    p = subprocess.run(["make", "-C", str(ROOT / "c"), "render-harness"] + extra,
                       capture_output=True, text=True)
    check(f"build {binname} clean (-Wall -Wextra -Werror)",
          p.returncode == 0 and "warning" not in (p.stdout + p.stderr).lower(),
          (p.stdout + p.stderr)[-400:])


# --- 2. the oracle side -----------------------------------------------------
def doc_id(ir):
    """IR.asset.asset.id, '?' when absent or not a string (asset_id)."""
    a = ir.get("asset") if isinstance(ir, dict) else None
    ad = a.get("asset") if isinstance(a, dict) else None
    i = ad.get("id") if isinstance(ad, dict) else None
    return i if isinstance(i, str) else "?"


def oracle(path, stats=None):
    """(rc, stdout) for the harness contract; the oracle's own failures --
    an exception, an expansion/compile diagnostic, an empty document list --
    are (1, b''), exactly what the C side must produce."""
    try:
        reg = A.load_registry()
        docs, xdiags, base = A.load_asset_docs(path)
        if xdiags or not docs:
            return 1, b""
        out = []
        for aid, d2, pfx in docs:
            diags, ir = A.compile_asset(d2, reg, base)
            if ir is None:
                return 1, b""
            renders = A.render_all(ir)
            out.append(f"DOC {doc_id(ir)}")
            out.append(f"CELL {ir['W']} {ir['H']}")
            if stats is not None:
                stats["docs"] += 1
                stats["frames"] += sum(len(v) for v in renders.values())
                if ir.get("auto"):
                    stats["auto"] += 1
                style = ir.get("style") or {}
                if "shading" in style:
                    stats["shaded"] += 1
                inner = (style.get("outline") or {}).get("inner") or {}
                if inner.get("mode") == "part":
                    stats["inner"] += 1
                for tpl in (ir.get("animations") or {}).values():
                    for fr in tpl.get("frames") or []:
                        if fr.get("rotate"):
                            stats["rotate"] += 1
                        if fr.get("params"):
                            stats["params"] += 1
                        if fr.get("swap"):
                            stats["swap"] += 1
            for name, fs in renders.items():
                for i, f in enumerate(fs):
                    sha = hashlib.sha256(f["rgba"]).hexdigest()
                    out.append(f"FRAME {name}/{i} {sha} clipped={f['clipped']}")
        return 0, ("\n".join(out) + "\n").encode()
    except Exception:
        return 1, b""


def compare(label, rel):
    """One oracle-vs-C comparison; returns the oracle's stdout (for the
    golden cross-check and the ASCII assertion)."""
    r1, o1 = oracle(rel, cov)
    for binname, binary, env in (("plain", HARN, None),
                                 ("san", HARN_SAN, SAN),
                                 ("plain-2", HARN, None)):
        o2, r2, e2 = run([binary, rel], ROOT, env)
        san = b"AddressSanitizer" in e2 or b"runtime error" in e2
        if (o1, r1) == (o2, r2) and not san:
            continue
        detail = f"{binname}: py rc={r1} c rc={r2}"
        if o1 == o2:
            detail += " stderr=" + e2.decode(errors="replace")[-200:]
        else:
            d = list(difflib.unified_diff(
                o1.decode(errors="replace").splitlines(),
                o2.decode(errors="replace").splitlines(),
                "py", "c", lineterm="", n=1))
            detail += "\n" + "\n".join(d[:30])
        check(f"{label} [{binname}]", False, detail)
        return o1
    check(f"{label} (byte-identical, plain+SAN+deterministic)", True)
    return o1


# --- 3. the real corpus -----------------------------------------------------
corpus = sorted((ROOT / "examples").glob("*.json")) + \
         sorted((ROOT / "tests" / "invalid").glob("*.json"))
oracle_bytes = b""
for f in corpus:
    rel = str(f.relative_to(ROOT))
    out = compare(f"corpus {rel}", rel)
    if rel.startswith("examples/"):
        oracle_bytes += out

# valid non-asset documents carry no IR: rc 1, empty stdout both sides
style_rel = "registry/styles/arcade_16.style.json"
compare("style pack document (no asset IR)", style_rel)
recipe_rel = None
for p in sorted((ROOT / "registry" / "recipes").rglob("*.recipe.json")):
    recipe_rel = str(p.relative_to(ROOT))
    break
if recipe_rel:
    compare("recipe document (no asset IR)", recipe_rel)

# CLI input failures
for rel in ("__absent__.json", "."):
    r1, o1 = oracle(rel)
    o2, r2, e2 = run([HARN, rel], ROOT)
    check(f"input failure {rel!r}: both sides rc 1, empty stdout",
          (r1, o1) == (r2, o2) and (r1, o1) == (1, b""),
          f"py=({r1},{o1[:40]!r}) c=({r2},{o2[:40]!r})")
o, r, e = run([HARN], ROOT)
check("usage: render harness with no argument exits 2", r == 2,
      f"rc={r} {e[:100]!r}")

# --- 4. the goldens ---------------------------------------------------------
gold = {}
for p in sorted((ROOT / "tests" / "golden").glob("*.json")):
    d = json.loads(p.read_text())
    gold[d["asset"]] = d["frames"]


def parse_frames(out):
    """{doc id: {frame key: sha64}} from a harness stdout."""
    docs, cur = {}, None
    for line in out.decode().splitlines():
        if line.startswith("DOC "):
            cur = line[4:]
            docs[cur] = {}
        elif line.startswith("FRAME ") and cur is not None:
            key, rest = line[6:].split(" ", 1)
            docs[cur][key] = rest.split()[0]
    return docs


c_docs = {}
for f in sorted((ROOT / "examples").glob("*.json")):
    rel = str(f.relative_to(ROOT))
    o, r, _ = run([HARN, rel], ROOT)
    if r == 0:
        for k, v in parse_frames(o).items():
            if k in c_docs:
                check(f"golden: doc id {k} is unique", False)
            c_docs[k] = v
    else:
        check(f"golden: C renders {rel}", False, f"rc={r}")

missing = sorted(set(gold) - set(c_docs))
extra = sorted(set(c_docs) - set(gold))
mismatch = sorted(k for k in gold if k in c_docs and c_docs[k] != gold[k])
check("C reproduces every tests/golden/*.json frame hash",
      not missing and not extra and not mismatch,
      f"missing={missing} extra={extra} differing={mismatch}"
      if (missing or extra or mismatch)
      else f"{len(gold)} goldens, "
           f"{sum(len(v) for v in gold.values())} frames")
check("golden frame totals match the corpus",
      sum(len(v) for v in c_docs.values()) == sum(len(v) for v in gold.values()),
      f"{sum(len(v) for v in c_docs.values())} vs "
      f"{sum(len(v) for v in gold.values())}")

# --- 5. coverage ------------------------------------------------------------
check("coverage: every expanded document rendered", cov["docs"] >= 14,
      f"{cov['docs']} docs")
check("coverage: every golden frame rendered", cov["frames"] >= 189,
      f"{cov['frames']} frames")
check("coverage: auto canvas exercised (>=3 documents)", cov["auto"] >= 3,
      f"{cov['auto']} auto docs")
check("coverage: shaded style pack exercised", cov["shaded"] >= 1,
      f"{cov['shaded']} shaded docs")
check("coverage: inner outline mode 'part' exercised", cov["inner"] >= 1,
      f"{cov['inner']} docs")
check("coverage: frame rotate track exercised", cov["rotate"] >= 1,
      f"{cov['rotate']} rotating frames")
check("coverage: frame params/swap state tracks exercised",
      cov["params"] >= 1 and cov["swap"] >= 1,
      f"params={cov['params']} swap={cov['swap']}")
non_ascii = sorted({b for b in oracle_bytes if b >= 128})
check("oracle output is 7-bit ASCII", not non_ascii, f"bytes {non_ascii[:8]}")
check("every example produced output", bool(oracle_bytes), "")

print(f"\n{'ALL GREEN' if not fails else str(fails) + ' FAILED'}")
sys.exit(1 if fails else 0)
