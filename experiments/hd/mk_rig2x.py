#!/usr/bin/env python3
"""experiments/hd/mk_rig2x.py — the hero's rig preset at twice the height.

H3 wants the Petal Blade hero "via the rig animator at twice the height".
The animator (tools/arc1-art rig) takes the preset FILE as its size
parameter and rasterises analytically — "rotation is exact at any angle, no
resampling" (tools/arc1_art/rig.py) — so a doubled preset renders crisply
at 2x with no generator change. This script writes that preset:

    art/rigs/samurai.json  ->  experiments/hd/rig/samurai_2x.json

What doubles, and why, taken from how tools/arc1_art/rig.py actually reads
each field:

  width, height     doubled: the canvas grows with the character.
  origin            doubled: it is a continuous coordinate — the preset's
                    [14, 40] with height 40 sits exactly one row past the
                    last row, so [28, 80] still does at height 80.
  baseline          NOT doubled. rig.py defaults it to height - 1 and uses
                    it as a ROW index (shift = self.baseline - lowest), so
                    39 -> 79. A blind *2 would give 78 and park the feet a
                    row above the bottom edge, which art/CRITIQUE.md's
                    "the feet stay on the ground" would catch.
  parts[].pivot,    doubled: pixel geometry.
    size, points
  pose.*.offset     doubled: pixel offsets.

Everything else is copied verbatim — palette, bank, outline, shape, colour,
shade, z, joint angles, frame counts, keyframe times. outline stays 1 px on
purpose: art/CRITIQUE.md asks for a 1 px outline, so the 2x hero's outline
is proportionally finer than the 1x one, and that is a fact to review rather
than to paper over.

Deterministic: no clock, no randomness, key order preserved from the source.

    python3 experiments/hd/mk_rig2x.py [src.json] [dst.json]
"""

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "art", "rigs", "samurai.json")
DST = os.path.join(ROOT, "experiments", "hd", "rig", "samurai_2x.json")

# Every key whose value is a pixel quantity. Reached only through keys that
# are containers (parts, animations, keyframes, pose), which the walk
# descends into below.
SPATIAL = {"width", "height", "origin", "baseline", "pivot", "size", "points",
           "offset"}


def _double(v):
    """Double every number inside a spatial value (scalar or nested list)."""
    if isinstance(v, list):
        return [_double(x) for x in v]
    if isinstance(v, dict):
        return {k: _double(x) for k, x in v.items()}
    if isinstance(v, bool):
        return v
    if isinstance(v, (int, float)):
        return v * 2
    return v


def _walk(node):
    """Recursively rebuild. Spatial keys double their value; every other key
    is copied and descended into, because that is where parts, animations,
    keyframes and pose live."""
    if isinstance(node, list):
        return [_walk(x) for x in node]
    if isinstance(node, dict):
        out = {}
        for k, v in node.items():
            if k in SPATIAL:
                out[k] = _double(v)
            else:
                out[k] = _walk(v)
        return out
    return node


def make_rig2x(src=SRC, dst=DST):
    with open(src) as f:
        rig = json.load(f)

    out = _walk(rig)

    # baseline is a row index (height - 1), not a length: fix it after the
    # generic doubling, which would have produced 78 instead of 79.
    out["baseline"] = out["height"] - 1

    # sanity: the origin must still sit one row past the bottom edge, and the
    # 2x canvas must be exactly twice the 1x one.
    assert out["width"] == rig["width"] * 2 and out["height"] == rig["height"] * 2
    assert out["origin"][0] == rig["origin"][0] * 2
    assert out["origin"][1] == out["height"]
    assert out["baseline"] == out["height"] - 1
    assert out["outline"] == rig["outline"] and out["outline"] == 1
    assert out["palette"] == rig["palette"] and out["bank"] == rig["bank"]
    assert out["parts"] and len(out["parts"]) == len(rig["parts"])
    assert out["animations"].keys() == rig["animations"].keys()

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1, ensure_ascii=False)
        f.write("\n")
    return dst


def main(argv):
    src = argv[1] if len(argv) > 1 else SRC
    dst = argv[2] if len(argv) > 2 else DST
    path = make_rig2x(src, dst)
    with open(path) as f:
        rig = json.load(f)
    print("mk_rig2x: %s -> %s (%dx%d, baseline %d, origin %s, %d parts, "
          "%d animations)"
          % (src, os.path.relpath(path, ROOT), rig["width"], rig["height"],
             rig["baseline"], rig["origin"], len(rig["parts"]),
             len(rig["animations"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
