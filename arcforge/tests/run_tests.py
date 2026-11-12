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
for name, codes in exp.items():
    rc, r = run("validate", str(ROOT / "tests/invalid" / f"{name}.json"))
    got = sorted({d["code"] for d in r["diagnostics"] if d["severity"] == "error"})
    check(f"invalid/{name}", r["status"] == "FAIL" and got == sorted(codes), f"expected {codes} got {got}")
print(f"\n{'ALL GREEN' if not fails else str(fails) + ' FAILED'}")
sys.exit(1 if fails else 0)
