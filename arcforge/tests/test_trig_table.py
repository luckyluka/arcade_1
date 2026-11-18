#!/usr/bin/env python3
"""P0.4 conformance vectors: the generated trig table.

Three views of one formula -- int(round(cos|sin(deg) * 1e6)), deg
-180..180, round-half-even as Python's round():

  * the C header  c/gen/trig_table.h   (the C build never calls libm),
  * the JSON      tests/trig_table.json,
  * the oracle    arcforge_ref.COS / arcforge_ref.SIN.

All three must agree exactly, both files must be byte-identical to a
fresh regeneration (no stale artifact ships), and the table's shape,
pinned reference angles and the "no exact .5 tie" fact are pinned here
so a formula or layout change is a deliberate, visible one.

Exit code 0 = all vectors hold.
"""
import json
import math
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))
import arcforge_ref as A          # noqa: E402
import gen_trig_table as G        # noqa: E402

fails = 0


def check(name, ok, detail=""):
    global fails
    print(f"{'PASS' if ok else 'FAIL'}  {name} {detail}")
    fails += 0 if ok else 1


DEGS = list(range(G.DEG_MIN, G.DEG_MAX + 1))

# --- 1. checked-in artifacts are exactly a fresh regeneration -------------
fresh = G.artifacts()
for path, data in fresh.items():
    rel = path.relative_to(ROOT)
    current = path.read_bytes() if path.exists() else None
    check(f"not stale: {rel}", current == data,
          "" if current == data else "regenerate: python3 tools/gen_trig_table.py")

# CLI contract: --check verifies without writing and exits 0
before = {p: d for p, d in fresh.items()}
p = subprocess.run([sys.executable, str(ROOT / "tools" / "gen_trig_table.py"), "--check"],
                   capture_output=True, text=True)
check("--check exits 0 on a fresh tree", p.returncode == 0, p.stdout.strip())
check("--check writes nothing",
      all(pth.read_bytes() == data for pth, data in before.items()))

# --- 2. determinism: regeneration is a pure function of the formula -------
check("generator is byte-deterministic", G.artifacts() == fresh)

# --- 3. JSON == oracle ------------------------------------------------------
doc = json.loads(fresh[G.JSON_PATH])
check("JSON header fields",
      (doc["scale"], doc["deg_min"], doc["deg_max"]) == (G.SCALE, G.DEG_MIN, G.DEG_MAX),
      str({k: doc[k] for k in ("scale", "deg_min", "deg_max")}))
check("JSON rows are 361 entries", len(doc["cos"]) == G.COUNT and len(doc["sin"]) == G.COUNT)

mismatch = [(d, doc["cos"][d - G.DEG_MIN], A.COS[d]) for d in DEGS
            if doc["cos"][d - G.DEG_MIN] != A.COS[d]]
check("JSON cos == oracle COS for every degree", not mismatch, str(mismatch[:3]))
mismatch = [(d, doc["sin"][d - G.DEG_MIN], A.SIN[d]) for d in DEGS
            if doc["sin"][d - G.DEG_MIN] != A.SIN[d]]
check("JSON sin == oracle SIN for every degree", not mismatch, str(mismatch[:3]))

# --- 4. header text == JSON (parsed back out of the C) ---------------------
header = fresh[G.HEADER_PATH].decode("ascii")


def c_array(name):
    m = re.search(rf"static const int32_t {name}\[TRIG_COUNT\] = \{{(.*?)\}};", header, re.S)
    if not m:
        return None
    # one entry per line: value then `,  /* degree */` -- the degree lives
    # in the comment, so only a number followed by a comma is an entry
    return [int(v) for v in re.findall(r"(-?\d+),", m.group(1))]


check("header parses into two arrays", c_array("trig_cos") is not None
      and c_array("trig_sin") is not None)
check("header trig_cos == JSON cos", c_array("trig_cos") == doc["cos"])
check("header trig_sin == JSON sin", c_array("trig_sin") == doc["sin"])
check("header defines TRIG_SCALE 1000000", f"#define TRIG_SCALE {G.SCALE}" in header)
check("header guard present", "#ifndef ARCFORGE_TRIG_TABLE_H" in header
      and "#endif  /* ARCFORGE_TRIG_TABLE_H */" in header)

# --- 5. pinned reference angles -------------------------------------------
PINNED = {
    -180: (-1000000, 0), -90: (0, -1000000), -37: (798636, -601815),
    0: (1000000, 0), 30: (866025, 500000), 37: (798636, 601815),
    45: (707107, 707107), 60: (500000, 866025), 90: (0, 1000000),
    180: (-1000000, 0),
}
for d, (c, s) in sorted(PINNED.items()):
    got = (doc["cos"][d - G.DEG_MIN], doc["sin"][d - G.DEG_MIN])
    check(f"pinned cos/sin({d})", got == (c, s), f"got {got} want {(c, s)}")

# --- 6. table invariants ---------------------------------------------------
check("all values within +/- scale",
      all(abs(v) <= G.SCALE for v in doc["cos"] + doc["sin"]))
check("index = deg + 180 (entry 0 is -180, entry 360 is 180)",
      doc["cos"][0] == A.COS[-180] and doc["cos"][-1] == A.COS[180])

# round-half-even is specified for completeness; prove it never decides:
# no product lands on an exact .5 (fixed table -- this holds forever or
# the vector fails loudly).
ties = [d for d in DEGS
        for raw in (math.cos(math.radians(d)) * 1e6, math.sin(math.radians(d)) * 1e6)
        if raw % 1 == 0.5]
check("no exact .5 tie in the table", not ties, str(ties))

print("ALL GREEN" if fails == 0 else f"{fails} FAILURES")
sys.exit(1 if fails else 0)
