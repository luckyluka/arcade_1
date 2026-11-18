#!/usr/bin/env python3
"""Run the whole suite: registry lint, example assertions, golden hashes, negative fixtures."""
import json, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
TOOL = [sys.executable, str(ROOT / "tools" / "arcforge_ref.py")]
fails = 0

def run(*args):
    p = subprocess.run(TOOL + list(args), capture_output=True, text=True)
    try:
        return p.returncode, json.loads(p.stdout)
    except json.JSONDecodeError:
        return p.returncode, {"status": "FAIL", "diagnostics": [{"code": "E_TOOL", "severity": "error", "path": "/", "message": p.stderr[-400:]}]}

def check(name, ok, detail=""):
    global fails
    print(f"{'PASS' if ok else 'FAIL'}  {name} {detail}")
    fails += 0 if ok else 1

rc, r = run("lint")
check("registry lint", r["status"] == "PASS", "" if r["status"] == "PASS" else json.dumps(r["diagnostics"])[:300])
for ex in sorted((ROOT / "examples").glob("*.json")):
    rc, r = run("test", ex)
    check(f"example assertions ({ex.name})", r["status"] == "PASS",
          "" if r["status"] == "PASS" else json.dumps(r["diagnostics"])[:300])
    rc, r = run("golden", ex)
    check(f"example golden hashes ({ex.name})", r["status"] == "PASS",
          "" if r["status"] == "PASS" else json.dumps(r["diagnostics"])[:300])
exp = json.loads((ROOT / "tests/invalid/expectations.json").read_text())
for name, spec in exp.items():
    if isinstance(spec, list):
        # legacy form: run `validate`, match the exact error set
        rc, r = run("validate", str(ROOT / "tests/invalid" / f"{name}.json"))
        got = sorted({d["code"] for d in r["diagnostics"] if d["severity"] == "error"})
        check(f"invalid/{name}", r["status"] == "FAIL" and got == sorted(spec), f"expected {spec} got {got}")
        continue
    # object form: {"run": "validate"|"test"|"sweep", "errors": [...], "warnings": [...]}
    run_kind = spec.get("run", "validate")
    fname = f"{name}.archetype.json" if run_kind == "sweep" else f"{name}.json"
    rc, r = run(run_kind, str(ROOT / "tests/invalid" / fname))
    got_e = sorted({d["code"] for d in r["diagnostics"] if d["severity"] == "error"})
    got_w = sorted({d["code"] for d in r["diagnostics"] if d["severity"] == "warning"})
    want_e, want_w = sorted(spec.get("errors", [])), sorted(spec.get("warnings", []))
    status_ok = (r["status"] == "FAIL") == bool(want_e)
    check(f"invalid/{name}", status_ok and got_e == want_e and got_w == want_w,
          f"expected errors {want_e} warnings {want_w}, got errors {got_e} warnings {got_w}")
# P0.1: E_SCHEMA/E_UNKNOWN_FIELD wording and ordering are ours, not the
# jsonschema library's -- the C compiler must reproduce them byte-for-byte.
p = subprocess.run([sys.executable, str(ROOT / "tests" / "test_schema_messages.py")],
                   capture_output=True, text=True)
check("schema diagnostics messages (P0.1)", p.returncode == 0,
      "" if p.returncode == 0 else (p.stdout + p.stderr)[-600:])
# P0.2: the pattern vocabulary is checked by hand (no regex engine in C).
p = subprocess.run([sys.executable, str(ROOT / "tests" / "test_pattern_vocab.py")],
                   capture_output=True, text=True)
check("pattern vocabulary (P0.2)", p.returncode == 0,
      "" if p.returncode == 0 else (p.stdout + p.stderr)[-600:])
# P0.3: int32 range at parse time and floor semantics for negatives.
p = subprocess.run([sys.executable, str(ROOT / "tests" / "test_int_range.py")],
                   capture_output=True, text=True)
check("integer range + floor semantics (P0.3)", p.returncode == 0,
      "" if p.returncode == 0 else (p.stdout + p.stderr)[-600:])
# P0.4: trig table artifacts stay byte-identical to the formula and the oracle.
p = subprocess.run([sys.executable, str(ROOT / "tests" / "test_trig_table.py")],
                   capture_output=True, text=True)
check("trig table artifacts (P0.4)", p.returncode == 0,
      "" if p.returncode == 0 else (p.stdout + p.stderr)[-600:])
# P0.5: JSON output format spec (docs/OUTPUT_FORMAT.md) vs reference writer.
p = subprocess.run([sys.executable, str(ROOT / "tests" / "test_output_format.py")],
                   capture_output=True, text=True)
check("JSON output format (P0.5)", p.returncode == 0,
      "" if p.returncode == 0 else (p.stdout + p.stderr)[-600:])
# P0.6: similarity spec — close() defaults and the 0.85 fix threshold.
p = subprocess.run([sys.executable, str(ROOT / "tests" / "test_similarity.py")],
                   capture_output=True, text=True)
check("similarity spec (P0.6)", p.returncode == 0,
      "" if p.returncode == 0 else (p.stdout + p.stderr)[-600:])
# P0.7: registry iteration order + no hash-order leakage in any report.
p = subprocess.run([sys.executable, str(ROOT / "tests" / "test_iteration_order.py")],
                   capture_output=True, text=True)
check("iteration order + hashseed determinism (P0.7)", p.returncode == 0,
      "" if p.returncode == 0 else (p.stdout + p.stderr)[-600:])
# M4.1a: bundled C JSON layer vs the Python oracle (vectors + corpus bytes).
p = subprocess.run([sys.executable, str(ROOT / "tests" / "test_c_json.py")],
                   capture_output=True, text=True)
check("C JSON layer parity (M4.1a)", p.returncode == 0,
      "" if p.returncode == 0 else (p.stdout + p.stderr)[-600:])
# M4.1b: table-driven C schema validator + registry loader vs the oracle.
p = subprocess.run([sys.executable, str(ROOT / "tests" / "test_schema_parity.py")],
                   capture_output=True, text=True)
check("C schema validator parity (M4.1b)", p.returncode == 0,
      "" if p.returncode == 0 else (p.stdout + p.stderr)[-600:])
print(f"\n{'ALL GREEN' if not fails else str(fails) + ' FAILED'}")
sys.exit(1 if fails else 0)
