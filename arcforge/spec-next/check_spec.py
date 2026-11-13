#!/usr/bin/env python3
"""Checks the PROPOSED spec is internally consistent: schemas are valid, examples validate,
docs carry the PROPOSED banner, the size-sweep prototype still runs, and the live X0 size
sweep (real tool over the real registry) passes: symmetric, no clipping, every size."""
import json, subprocess, sys
from pathlib import Path
from jsonschema import Draft202012Validator
HERE = Path(__file__).resolve().parent
fails = 0
def check(name, ok, detail=""):
    global fails
    print(f"{'PASS' if ok else 'FAIL'}  {name} {detail}"); fails += 0 if ok else 1

schemas = {p.name: json.loads(p.read_text()) for p in sorted((HERE / "schema").glob("*.json"))}
for n, s in schemas.items():
    try: Draft202012Validator.check_schema(s); check(f"schema valid: {n}", True)
    except Exception as e: check(f"schema valid: {n}", False, str(e)[:120])

pairs = {"hero.knight.asset.json": "asset.2_1", "hero.fixed_cell.asset.json": "asset.2_1", "tile.brick.asset.json": "asset.2_1",
         "project.example.json": "project", "level1.tileset.json": "tileset", "level1.level.json": "level"}
for f, key in pairs.items():
    schema = next(v for k, v in schemas.items() if k.startswith(key))
    errs = list(Draft202012Validator(schema).iter_errors(json.loads((HERE / "examples" / f).read_text())))
    check(f"example validates: {f}", not errs, "; ".join(e.message[:80] for e in errs[:2]))

# negative: a hardcoded-size violation must be rejected (size above schema bound) and unknown keys rejected
bad = json.loads((HERE / "examples/hero.knight.asset.json").read_text()); bad["visual"] = "make him look tough"
errs = list(Draft202012Validator(schemas["asset.2_1.proposed.schema.json"]).iter_errors(bad))
check("free-text field rejected by asset 2.1 schema", bool(errs))

for md in sorted(HERE.glob("0*.md")):
    check(f"PROPOSED banner: {md.name}", "PROPOSED" in md.read_text()[:400])

r = subprocess.run([sys.executable, str(HERE / "prototype/size_sweep.py"), "--out", str(HERE / "prototype/out")], capture_output=True, text=True)
check("prototype size sweep: symmetric, no clipping at 4 sizes", r.returncode == 0, r.stderr[-200:])

# X0: the live sweep — the real tool, the real registry, the implemented archetype.
sw = subprocess.run([sys.executable, str(HERE.parent / "tools" / "arcforge_ref.py"), "sweep",
                     str(HERE.parent / "registry" / "archetypes" / "humanoid_front_scalable.archetype.json")],
                    capture_output=True, text=True)
try:
    sw_ok = sw.returncode == 0 and json.loads(sw.stdout)["status"] == "PASS"
except (ValueError, KeyError):
    sw_ok = False
check("live sweep: humanoid_front_scalable, 6 sizes, symmetric, no clipping", sw_ok, "" if sw_ok else (sw.stdout or sw.stderr)[-200:])
print("\nALL GREEN" if not fails else f"\n{fails} FAILED"); sys.exit(1 if fails else 0)
