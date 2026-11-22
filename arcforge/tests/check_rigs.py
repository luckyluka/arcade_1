#!/usr/bin/env python3
"""Rig regression: every rigs/*.rig.json must build clean, and the knight rig must reproduce the hand-written
knight_front archetype's golden frames bit for bit."""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import arcforge_ref as A
import rig_build as R

fail = 0
for p in sorted((ROOT / "rigs").glob("*.rig.json")):
    try:
        arch = R.expand(json.loads(p.read_text()))
        sd = A.schema_diags(arch, "archetype.schema.json")
        reg = A.load_registry()
        reg["archetypes"][arch["archetype"]] = arch
        d, ir = A.compile_asset(R.example_asset(arch, "shaded_15", "blue"), reg, ROOT)
        errs = [x for x in sd + d if x["severity"] == "error"]
        if ir is not None:
            _, d2 = A.run_tests(ir, A.render_all(ir))
            errs += [x for x in d2 if x["severity"] == "error"]
        ok = not errs
        print(("PASS " if ok else "FAIL ") + f"rig {p.name}" + ("" if ok else f" {[e['code'] for e in errs][:4]}"))
        fail += not ok
        if p.name == "knight_front.rig.json" and ir is not None:
            g = json.loads((ROOT / "tests" / "golden" / "hero.knight_blue.json").read_text())["frames"]
            h = A.frame_hashes(A.render_all(ir))
            bad = [k for k in g if g[k] != h.get(k)]
            print(("PASS " if not bad else "FAIL ") + f"rig knight_front reproduces golden ({len(g)} frames)")
            fail += bool(bad)
    except Exception as e:  # a rig that crashes the builder is a failure too
        print(f"FAIL rig {p.name}: {type(e).__name__}: {e}")
        fail += 1
print("RIGS GREEN" if not fail else "RIGS FAILED")
sys.exit(1 if fail else 0)
