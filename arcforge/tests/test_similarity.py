#!/usr/bin/env python3
"""P0.6 conformance vectors: the similarity spec.

docs/AI_INTERFACE.md (Diagnostic contract) specifies two numbers the C
compiler (M4) must reproduce exactly:

  * suggestions -- close(name, options) is difflib.get_close_matches(
    str(name), list(options), n=3, cutoff=0.6): seq1 = candidate, seq2 =
    query, Ratcliff/Obershelp ratio, keep ratio >= 0.6, at most 3,
    ties by lexicographically larger string, order-independent;
  * the E_UNKNOWN_RECIPE fix threshold -- exactly 1 suggestion, or
    SequenceMatcher(None, name, suggestion).ratio() > 0.85 (strict >,
    query as seq1, i.e. the OPPOSITE argument order of
    get_close_matches).

tests/similarity_vectors.json pins both: pure close() vectors (cutoff
boundary, tie order, n=3 cap, order independence), pinned ratios
(including an asymmetric pair and the exact 0.85 / 0.6 boundaries), and
four end-to-end cases driven through the real compile_asset path on a
real asset so the fix expression itself is exercised, not re-implemented.

Source hygiene keeps the spec single-sourced: every close() call site
uses the documented defaults, the 0.85 literal appears exactly once,
and the fix check's argument order is pinned in the source.

Exit code 0 = all vectors hold.
"""
import difflib
import json
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


VEC = json.loads((ROOT / "tests" / "similarity_vectors.json").read_text())

# --- 1. close() vectors ----------------------------------------------------
for i, v in enumerate(VEC["close"]):
    got = A.close(v["name"], v["options"])
    check(f"close [{i}] {v['name']!r}", got == v["want"],
          "" if got == v["want"] else f"got {got} want {v['want']}")
    if len(v["options"]) > 1:
        rev = A.close(v["name"], list(reversed(v["options"])))
        check(f"close [{i}] order-independent", rev == v["want"],
              "" if rev == v["want"] else f"reversed got {rev} want {v['want']}")

# defaults the spec documents are the defaults difflib ships
sig = difflib.get_close_matches.__defaults__
check("get_close_matches defaults are (3, 0.6)", sig == (3, 0.6), str(sig))

# --- 2. pinned ratios ------------------------------------------------------
for i, v in enumerate(VEC["ratio"]):
    fwd = difflib.SequenceMatcher(None, v["a"], v["b"]).ratio()
    check(f"ratio [{i}] ({v['a']!r}, {v['b']!r})", fwd == v["ratio"],
          "" if fwd == v["ratio"] else f"got {fwd!r} want {v['ratio']!r}")
    rev = difflib.SequenceMatcher(None, v["b"], v["a"]).ratio()
    check(f"ratio [{i}] reversed", rev == v["reverse"],
          "" if rev == v["reverse"] else f"got {rev!r} want {v['reverse']!r}")

# --- 3. end-to-end fix threshold through compile_asset ---------------------
fix_spec = VEC["fix_threshold"]
base = json.loads((ROOT / fix_spec["base_asset"]).read_text())
import copy                                          # noqa: E402
reg = A.load_registry()
for case in fix_spec["cases"]:
    doc = copy.deepcopy(base)
    f0 = doc["features"][0]
    if isinstance(f0, str):
        doc["features"][0] = case["recipe"]
    else:
        doc["features"][0] = dict(f0, recipe=case["recipe"])
    diags, ir = A.compile_asset(doc, reg, str(ROOT))
    hits = [d for d in diags if d["code"] == "E_UNKNOWN_RECIPE"]
    ok = len(hits) == 1
    detail = "" if ok else f"got {len(hits)} E_UNKNOWN_RECIPE diagnostics: {[d['code'] for d in diags][:5]}"
    check(f"fix [{case['recipe']}] one diagnostic", ok, detail)
    if not ok:
        continue
    d = hits[0]
    sug = d.get("suggestions")
    check(f"fix [{case['recipe']}] suggestions", sug == case["want_suggestions"],
          "" if sug == case["want_suggestions"] else f"got {sug} want {case['want_suggestions']}")
    fix = d.get("fix")
    check(f"fix [{case['recipe']}] fix", fix == case["want_fix"],
          "" if fix == case["want_fix"] else f"got {fix} want {case['want_fix']}")

# --- 4. source hygiene: the spec is single-sourced --------------------------
src = (ROOT / "tools" / "arcforge_ref.py").read_text()

# every close() call site uses the documented defaults (n=3, cutoff=0.6)
call_lines = [ln.strip() for ln in src.splitlines()
              if "close(" in ln
              and not ln.strip().startswith("def close")
              and "get_close_matches" not in ln
              and not ln.strip().startswith("#")]
bad_calls = [ln for ln in call_lines if "n=" in ln.split("close(")[-1]
             or "cutoff=" in ln.split("close(")[-1]]
check(f"all {len(call_lines)} close() call sites use default n/cutoff",
      not bad_calls, str(bad_calls))

# the adapter is explicit about the defaults
check("close() adapter pins n=n, cutoff=cutoff",
      "difflib.get_close_matches(str(name), list(options), n=n, cutoff=cutoff)" in src)

# the 0.85 fix threshold exists exactly once, with the query as seq1
check("0.85 appears exactly once (single-sourced threshold)",
      src.count("0.85") == 1, f"count={src.count('0.85')}")
check("fix check is SequenceMatcher(None, name, sug[0]) (query as seq1)",
      src.count("SequenceMatcher(None, name, sug[0])") == 1,
      f"count={src.count('SequenceMatcher(None, name, sug[0])')}")

# the 64-character identifier cap (P0.2) keeps every similarity input under
# difflib's 200-character autojunk threshold, so Python and C agree
names = list(reg["recipes"]) + list(reg["archetypes"]) + list(reg["styles"])
check("all registry names < 200 chars (autojunk never applies)",
      all(len(n) < 200 for n in names),
      str([n for n in names if len(n) >= 200]))
max_opt = max((len(o) for v in VEC["close"] for o in v["options"]), default=0)
check("all vector options < 200 chars", max_opt < 200, f"max={max_opt}")

print("ALL GREEN" if fails == 0 else f"{fails} FAILURES")
sys.exit(1 if fails else 0)
