#!/usr/bin/env python3
"""M4.1b conformance: the C schema validator + registry loader vs the oracle.

The hand-coded, table-driven validator (arcforge/c/schema.c) must reproduce
`tools/arcforge_ref.py::schema_diags` BYTE-FOR-BYTE for every job of a
corpus + mutation batch, and `arcforge/c`'s registry loader must reproduce
`load_registry()`'s view byte-for-byte (arcforge AGENTS rule 8: Python stays
the oracle until M4.3 flips the gate).

The batch covers, with deterministic jobs only (no randomness):
  * every registry file (all three globs, base = ROOT-relative path);
  * examples, tests/invalid (both schema families), tests/golden,
    tests/*.json and spec-next documents against the five real schemas;
  * schema documents validated against each of the five schemas (stress);
  * deterministic mutations of five base documents: deleted keys, retyped
    scalars, unknown-field inserts, array shrink / grow (duplicate entry),
    int32-range violations at both ends, corrupted `id`/`version` strings;
  * hand-written oneOf jobs: merged type shapes, the "intended form"
    (missing key inside a branch), and an unknown field inside a branch.

The jobs are serialised with `A.dump_json` and RE-PARSED before the
expectations are computed, because dump_json rounds floats to 4 decimals --
the expectations must come from exactly the text the C binary reads.

Both binaries must agree: plain, under -fsanitize=undefined,address (M4
acceptance item 5), and twice in a row (determinism). Output must be 7-bit
ASCII. Coverage assertions keep the batch honest: if a mutation stops
producing the diagnostics it exists for, this test fails.

Known, deliberate divergences (spec'd, not tested here):
  * duplicate object keys / integers beyond int64: the C parser rejects
    what Python tolerates (tests/test_c_json.py pins corpus parity instead).

Exit code 0 = all checks hold.
"""
import copy
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import arcforge_ref as A          # noqa: E402

fails = 0


def check(name, ok, detail=""):
    global fails
    print(f"{'PASS' if ok else 'FAIL'}  {name} {detail}")
    fails += 0 if ok else 1


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


# --- 1. build both binaries (plain + sanitizers) ---------------------------
for extra, binname in (([], "arcforge"), (["SAN=1"], "arcforge_san")):
    p = run(["make", "-C", str(ROOT / "c")] + extra)
    check(f"build {binname} clean (-Wall -Wextra -Werror)",
          p.returncode == 0, (p.stdout + p.stderr)[-400:])

BIN = ROOT / "build" / "arcforge"
BIN_SAN = ROOT / "build" / "arcforge_san"


# --- 2. deterministic job set ----------------------------------------------
jobs = []


def file_jobs(pattern, schema, base_is_rel=False, skip=()):
    for p in sorted(ROOT.glob(pattern)):
        if p.name in skip:
            continue
        try:
            A.load_json(p)
        except Exception:
            continue            # acceptance parity is tests/test_c_json.py
        rel = str(p.relative_to(ROOT))
        jobs.append({"schema": schema,
                     "base": rel if base_is_rel else "",
                     "file": rel})


def file_schema(p):
    return ("archetype.schema.json" if p.name.endswith(".archetype.json")
            else "asset.schema.json")


# registry: base = ROOT-relative path (lint's prefix)
for pat, schema in (("registry/archetypes/*.archetype.json", "archetype.schema.json"),
                    ("registry/recipes/**/*.recipe.json", "recipe.schema.json"),
                    ("registry/styles/*.style.json", "style_pack.schema.json")):
    file_jobs(pat, schema, base_is_rel=True)

# examples: assets
file_jobs("examples/*.json", "asset.schema.json")

# negative fixtures: both schema families (expectations.json is not a doc)
for p in sorted((ROOT / "tests" / "invalid").glob("*.json")):
    if p.name == "expectations.json":
        continue
    try:
        A.load_json(p)
    except Exception:
        continue
    rel = str(p.relative_to(ROOT))
    jobs.append({"schema": file_schema(p), "base": "", "file": rel})

# golden reports, vector tables and the PROPOSED documents: stress inputs
file_jobs("tests/golden/*.json", "asset.schema.json")
file_jobs("tests/*.json", "asset.schema.json")
file_jobs("spec-next/**/*.json", "asset.schema.json")

# every schema document against every schema (25 stress jobs)
SCHEMAS = sorted(p.name for p in (ROOT / "schema").glob("*.json"))
check("five schemas found", len(SCHEMAS) == 5, str(SCHEMAS))
for sname in SCHEMAS:
    file_jobs("schema/*.json", sname)


# --- deterministic mutations of five base documents -------------------------
def first_leaf(doc, pred):
    """(container, key) of the first scalar in document order matching pred.
    List elements are keyed by index -- assignment works there too."""
    if isinstance(doc, dict):
        for k, v in doc.items():
            if pred(v):
                return (doc, k)
            r = first_leaf(v, pred)
            if r:
                return r
    elif isinstance(doc, list):
        for idx, v in enumerate(doc):
            if pred(v):
                return (doc, idx)
            r = first_leaf(v, pred)
            if r:
                return r
    return None


def first_array(doc):
    """(container, key) of the first array in document order."""
    if isinstance(doc, dict):
        for k, v in doc.items():
            if isinstance(v, list):
                return (doc, k)
            r = first_array(v)
            if r:
                return r
    elif isinstance(doc, list):
        for v in doc:
            r = first_array(v)
            if r:
                return r
    return None


def mut_delete(doc):
    d = copy.deepcopy(doc)
    if not d:
        return None
    del d[next(iter(d))]                       # first top-level key
    for v in d.values():                       # first nested dict's first key
        if isinstance(v, dict) and v:
            del v[next(iter(v))]
            break
        if isinstance(v, list):
            for e in v:
                if isinstance(e, dict) and e:
                    del e[next(iter(e))]
                    return d
    return d


def mut_retype(doc):
    d = copy.deepcopy(doc)
    # locate both leaves BEFORE mutating (the string we turn into 42 would
    # otherwise be found as the first int)
    s = first_leaf(d, lambda v: isinstance(v, str))
    i = first_leaf(d, lambda v: isinstance(v, int) and not isinstance(v, bool))
    if s:
        s[0][s[1]] = 42
    if i and i != s:
        i[0][i[1]] = "retyped"
    return d


def mut_unknown(doc):
    d = copy.deepcopy(doc)
    d["zzz_unknown_top"] = "metadata is not intent"
    for v in d.values():
        if isinstance(v, dict):
            v["zzz_unknown_nested"] = 1
            break
    return d


def mut_array_shrink(doc):
    d = copy.deepcopy(doc)
    hit = first_array(d)
    if not hit or len(hit[0][hit[1]]) <= 1:
        return None
    hit[0][hit[1]].pop()
    return d


def mut_array_grow(doc):
    d = copy.deepcopy(doc)
    hit = first_array(d)
    if not hit:
        return None
    arr = hit[0][hit[1]]
    arr.append(copy.deepcopy(arr[0]) if arr else "grown")
    return d


def mut_int_big(doc):
    hit = first_leaf(doc, lambda v: isinstance(v, int) and not isinstance(v, bool))
    if not hit:
        return None
    d = copy.deepcopy(doc)
    hit = first_leaf(d, lambda v: isinstance(v, int) and not isinstance(v, bool))
    hit[0][hit[1]] = 2147483648
    return d


def mut_int_neg(doc):
    hit = first_leaf(doc, lambda v: isinstance(v, int) and not isinstance(v, bool))
    if not hit:
        return None
    d = copy.deepcopy(doc)
    hit = first_leaf(d, lambda v: isinstance(v, int) and not isinstance(v, bool))
    hit[0][hit[1]] = -2147483649
    return d


def mut_format_strings(doc):
    """Corrupt every `id` / `version` string (vocabulary: dotted, semver)."""
    d = copy.deepcopy(doc)
    found = []

    def walk(node):
        if isinstance(node, dict):
            for k, v in node.items():
                if k in ("id", "version") and isinstance(v, str):
                    node[k] = "Bad Value!!"
                    found.append(k)
                walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)

    walk(d)
    return d if found else None


MUTATIONS = (mut_delete, mut_retype, mut_unknown, mut_array_shrink,
             mut_array_grow, mut_int_big, mut_int_neg, mut_format_strings)

BASE_FILES = [
    "examples/hero.knight.json",
    "examples/x1_showcase.json",
    "registry/archetypes/bat_flyer.archetype.json",
    "registry/recipes/enemy/bat_fangs.recipe.json",
    "registry/styles/" + sorted(p.name for p in (ROOT / "registry" / "styles").glob("*.style.json"))[0],
]
mut_jobs = 0
for base in BASE_FILES:
    doc = A.load_json(ROOT / base)
    for mut in MUTATIONS:
        mutated = mut(doc)
        if mutated is None:
            continue
        jobs.append({"schema": file_schema(Path(base)), "base": "",
                     "doc": mutated})
        mut_jobs += 1
check("mutations applied", mut_jobs >= 30, f"{mut_jobs} jobs")


# --- hand-written oneOf coverage jobs ---------------------------------------
jobs.append({
    "schema": "asset.schema.json", "base": "",
    "doc": {"arcforge": "2.0",
            "asset": {"id": "x.y", "type": "tile", "version": "1.0.0"},
            "target": {"profile": "arcade1-draft", "canvas": "auto",
                       "clip_policy": "error"},
            "style_pack": "blue", "archetype": "bat_flyer",
            "features": [42, {}, {"recipe": "ok", "zzz": 1}]},
})
jobs.append({   # oneOf: both branches invalid at the value itself
    "schema": "asset.schema.json", "base": "",
    "doc": {"arcforge": "2.0",
            "asset": {"id": "x.y", "type": "tile", "version": "1.0.0"},
            "target": {"profile": "arcade1-draft", "canvas": {"mode": "auto",
                                                              "zzz": 1},
                       "clip_policy": "error"},
            "style_pack": "blue", "archetype": "bat_flyer"},
})

check("job batch built", len(jobs) >= 150, f"{len(jobs)} jobs")


# --- 3. expectations from exactly the text C reads --------------------------
jobs_text = A.dump_json(jobs, 2)
jobs_path = ROOT / "build" / "schema_parity_jobs.json"
jobs_path.write_text(jobs_text)
jobs_rt = json.loads(jobs_text)              # dump_json rounds floats: re-parse
expected = []
for j in jobs_rt:
    doc = A.load_json(j["file"]) if "file" in j else j["doc"]
    expected.append(A.schema_diags(doc, j["schema"], j.get("base", "")))
exp_body = A.dump_json(expected, 2)   # C output minus its one trailing newline
n_diags = sum(len(d) for d in expected)
check("expectations non-trivial", n_diags >= 100, f"{n_diags} diagnostics")


def strip1(s):
    return s[:-1] if s.endswith("\n") else s


def first_diff(got, want, limit=4):
    gl, wl = got.split("\n"), want.split("\n")
    out = []
    for i in range(max(len(gl), len(wl))):
        g = gl[i] if i < len(gl) else "<EOF>"
        w = wl[i] if i < len(wl) else "<EOF>"
        if g != w:
            out.append(f"line {i + 1}:")
            out.append(f"  py: {w[:200]}")
            out.append(f"   c: {g[:200]}")
            if len(out) >= limit * 3:
                break
    return " | ".join(out) if out else ""


# --- 4. plain / sanitized / deterministic runs ------------------------------
def batch(binpath):
    return run([str(binpath), "--schema-check", str(jobs_path)], cwd=str(ROOT))


p1 = batch(BIN)
check("C --schema-check exits 0", p1.returncode == 0,
      p1.stderr[-400:] or f"{len(p1.stdout)} bytes")
got = strip1(p1.stdout)
check(f"schema-check byte parity ({len(jobs_rt)} jobs, {n_diags} diags)",
      p1.returncode == 0 and got == exp_body,
      first_diff(got, exp_body) if got != exp_body else "")

p2 = batch(BIN)
check("C output is byte-deterministic across runs",
      p1.returncode == 0 and p2.stdout == p1.stdout,
      first_diff(strip1(p2.stdout), strip1(p1.stdout)))

p3 = batch(BIN_SAN)
check("sanitized batch exits 0 with identical bytes (ASan/UBSan)",
      p3.returncode == 0 and strip1(p3.stdout) == exp_body,
      (p3.stderr[-400:] or first_diff(strip1(p3.stdout), exp_body)))

try:
    p1.stdout.encode("ascii")
    ascii_ok = True
except UnicodeEncodeError:
    ascii_ok = False
check("output is 7-bit ASCII", ascii_ok)


# --- 5. registry view -------------------------------------------------------
reg_exp = A.dump_json(A.load_registry(), 2) + "\n"
pr = run([str(BIN), "--registry-list"], cwd=str(ROOT))
check("C --registry-list exits 0", pr.returncode == 0, pr.stderr[-300:])
check("registry-list byte parity (load_registry view)",
      pr.stdout == reg_exp,
      first_diff(strip1(pr.stdout), strip1(reg_exp)) if pr.stdout != reg_exp else "")
pr2 = run([str(BIN), "--registry-list"], cwd=str(ROOT))
check("registry-list deterministic", pr.stdout == pr2.stdout)
prs = run([str(BIN_SAN), "--registry-list"], cwd=str(ROOT))
check("sanitized registry-list byte parity",
      prs.returncode == 0 and prs.stdout == reg_exp, prs.stderr[-300:])
reg = A.load_registry()
check("registry counts carried",
      len(reg["archetypes"]) == pr.stdout.count('"archetype": '),
      f"{sum(len(g) for g in reg.values())} entries")


# --- 6. coverage: the batch must exercise what it claims --------------------
codes = [d["code"] for dd in expected for d in dd]
msgs = [d["message"] for dd in expected for d in dd]
paths = [d["path"] for dd in expected for d in dd]

check("coverage: E_SCHEMA present", "E_SCHEMA" in codes)
check("coverage: E_UNKNOWN_FIELD from mutations + branch",
      codes.count("E_UNKNOWN_FIELD") >= 5,
      f"{codes.count('E_UNKNOWN_FIELD')}")
check("coverage: E_INT_RANGE at both ends",
      codes.count("E_INT_RANGE") >= 5,
      f"{codes.count('E_INT_RANGE')}")
check("coverage: vocabulary (format) messages",
      any("is not a valid" in m for m in msgs))
check("coverage: required-property messages",
      any("is a required property" in m for m in msgs))
check("coverage: oneOf merged shapes (array|object, string|object)",
      "expected array or object, got string" in msgs
      and "expected string or object, got integer" in msgs)
check("coverage: oneOf intended form names the missing key",
      any(p == "/features/1" and m == "'recipe' is a required property"
          for p, m in zip(paths, msgs)))
check("coverage: unknown field inside a oneOf branch",
      any(p == "/features/2/zzz" for p in paths))
check("coverage: enum/const style messages",
      any(m.startswith("expected one of [") or m.startswith('expected one of {"')
          or m.startswith('expected one of "') for m in msgs))

print("ALL GREEN" if fails == 0 else f"{fails} FAILURES")
sys.exit(1 if fails else 0)
