#!/usr/bin/env python3
"""gen_backgrounds.py — the library's `backgrounds` pack.

What is behind the game: three parallax ridge strips, a tileable star
field, a cloud and a moon.

The ridges are **horizontal** strips (a silhouette that repeats), because
that is how a parallax layer scrolls: the game draws the strip twice, at
the camera's offset and at the camera's offset plus the strip's width. The
round-5 game's three layers are the model.

Deterministic: fixed seeds, no clock, sorted iteration.

    python3 art/library/backgrounds/gen_backgrounds.py \\
        --out-dir art/library/backgrounds

It writes:

    strips/ridge_far.png    64x16, the far ridge
    strips/ridge_mid.png    64x16, the middle ridge
    strips/ridge_near.png   64x16, the near ridge
    strips/stars.png        32x32, a tileable star field
    strips/cloud.png        32x16
    strips/moon.png         16x16
    backgrounds.json        the manifest fragment
"""

import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from arc1_art import canvas as C                              # noqa: E402

PALETTE = "dusk16"
BANK = 15

# The dusk palette's indices (see art/palettes/dusk16.json).
NIGHT = 1        # the outline
DEEP = 2
INDIGO = 3
VIOLET = 4
MID = 5
LIGHT = 6
PALE = 7
CRIMSON = 8
RED = 9
EMBER = 10
AMBER = 11
GOLD = 12
PALE_GOLD = 13
CREAM = 14
STAR = 15

SEED = 20261021
W = 64
H = 16


def _ridge(seed, base, height, colour, colour2):
    """A seamless silhouette: a sum of sines, so the ends match."""
    c = C.Canvas(W, H)
    r = C.Rng(seed)
    p1, p2, p3 = (r.rand(1000) / 1000.0 for _ in range(3))
    profile = []
    for x in range(W):
        y = base - int(height * (0.55 * math.sin(2 * math.pi * (x / W + p1)) +
                            0.30 * math.sin(2 * math.pi * (2 * x / W + p2)) +
                            0.15 * math.sin(2 * math.pi * (3 * x / W + p3))))
        profile.append(max(0, min(H - 1, y)))
    for x in range(W):
        top = profile[x]
        for y in range(top, H):
            c.px(x, y, colour)
        # a lit edge along the ridge
        c.px(x, top, colour2)
    return c


def stars(seed):
    """A tileable star field: 32x32, drawn twice so it wraps."""
    c = C.Canvas(32, 32)
    r = C.Rng(seed)
    for _ in range(26):
        x, y = r.rand(32), r.rand(32)
        c.px(x, y, STAR)
        if r.rand(4) == 0:
            c.px((x + 1) % 32, y, PALE)
    return c


def cloud():
    """A soft cloud, 32x16."""
    c = C.Canvas(32, 16)
    for (cx, cy, r) in ((10, 9, 4), (16, 8, 5), (22, 9, 4)):
        for y in range(16):
            for x in range(32):
                d = (x - cx) ** 2 + (y - cy) ** 2
                if d <= r * r:
                    c.px(x, y, PALE if d < (r - 1) ** 2 else LIGHT)
    return c


def moon():
    """A crescent moon, 16x16."""
    c = C.Canvas(16, 16)
    for y in range(16):
        for x in range(16):
            d1 = (x - 9) ** 2 + (y - 8) ** 2
            d2 = (x - 12) ** 2 + (y - 6) ** 2
            if d1 <= 30 and d2 > 24:
                c.px(x, y, CREAM if d1 < 20 else PALE)
    return c


RIDGES = [
    ("ridge_far", 15, 9, INDIGO, VIOLET),
    ("ridge_mid", 13, 7, DEEP, INDIGO),
    ("ridge_near", 12, 5, NIGHT, DEEP),
]


def main():
    out_dir = HERE
    args = sys.argv[1:]
    if "--out-dir" in args:
        out_dir = os.path.abspath(args[args.index("--out-dir") + 1])
    strips = os.path.join(out_dir, "strips")
    os.makedirs(strips, exist_ok=True)

    with open(os.path.join(ROOT, "art", "palettes", "%s.json" % PALETTE)) as f:
        preset = json.load(f)

    fragment = {
        "format": 1,
        "description": "What is behind the game: three seamless parallax ridge "
                       "strips, a tileable star field, a cloud and a moon.",
        "palettes": {
            PALETTE: {"bank": BANK, "colors": list(preset["colors"])},
        },
        "roles": {name: "layer" for name, _b, _h, _c, _c2 in RIDGES} |
                 {"stars": "layer", "cloud": "layer", "moon": "layer"},
        "sprites": [],
        "animations": [],
        "maps": [],
    }

    def add(name, canvas, note):
        out = os.path.join(strips, "%s.png" % name)
        C.to_strip([canvas], preset["colors"], out)
        fragment["sprites"].append({
            "name": name,
            "file": "strips/%s.png" % name,
            "palette": PALETTE,
            "frames": 1,
            "origin": [0, 0],
            "provenance": {
                "tool": "procedural", "model": "",
                "prompt": "the backgrounds pack: "
                          "art/library/backgrounds/gen_backgrounds.py",
                "seed": SEED, "licence": "this repository",
            },
        })
        print("backgrounds: %-11s %dx%d" % (name, canvas.w, canvas.h))

    for i, (name, base, height, colour, colour2) in enumerate(RIDGES):
        add(name, _ridge(SEED + i, base, height, colour, colour2), "")
    add("stars", stars(SEED + 9), "")
    add("cloud", cloud(), "")
    add("moon", moon(), "")

    fragment_path = os.path.join(out_dir, "backgrounds.json")
    with open(fragment_path, "w", encoding="utf-8") as f:
        json.dump(fragment, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("backgrounds: %s" % fragment_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
