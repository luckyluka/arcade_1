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




# --------------------------------------------------------------------------
# The sky
# --------------------------------------------------------------------------

# The dusk sky's bands, top to horizon: the night's indigo, the violets,
# the crimson and the gold. Each band is one tile, dithered against its
# neighbour, so seventeen bands read as one smooth gradient.
SKY_BANDS = [
    (2, 2), (3, 3), (3, 4), (4, 4), (4, 5), (5, 5), (5, 6), (6, 6),
    (7, 7), (7, 8), (8, 9), (10, 10), (10, 11), (11, 12), (12, 13),
    (13, 14), (14, 15),
]
SKY_TILE = 16
SKY_COLS = 30
SKY_ROWS = len(SKY_BANDS)

# The DAY sky: the same seventeen bands over the day ramp — the deep blue
# at the top, the pale blue in the middle, the warm haze at the horizon.
DAY_PALETTE = "day16"
DAY_BANDS = [
    (2, 2), (3, 3), (3, 4), (4, 4), (4, 5), (5, 5), (5, 6), (6, 6),
    (6, 7), (7, 7), (7, 8), (8, 9), (9, 9), (9, 10), (10, 11), (11, 12),
    (13, 14),
]


def sky_bands(bands=None):
    """The band tiles: 16x16 each, dithered between two palette indices.

    The dither is what makes the banding disappear: a solid band would
    show seventeen steps, a dithered one shows a gradient.
    """
    tiles = []
    for (a, b) in (bands or SKY_BANDS):
        t = C.Canvas(SKY_TILE, SKY_TILE)
        for y in range(SKY_TILE):
            for x in range(SKY_TILE):
                t.px(x, y, b if C.ordered_dither(x, y, 4) > 0.5 else a)
        tiles.append(t)
    sheet = C.Canvas(SKY_TILE * len(tiles), SKY_TILE)
    for i, t in enumerate(tiles):
        sheet.stamp(t, i * SKY_TILE, 0)
    return sheet


def sky_map():
    """The map that lays the bands out: 30x17 cells, one row per band."""
    rows = []
    for r in range(SKY_ROWS):
        rows.append("".join(chr(ord("a") + r) for _ in range(SKY_COLS)))
    return rows


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
                 {"stars": "layer", "cloud": "layer", "moon": "layer",
                  "sky_bands": "tileset", "sky_bands_day": "tileset"},
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

    # --- the sky: a band tileset and the map that lays it out -----------
    bands = sky_bands()
    out = os.path.join(strips, "sky_bands.png")
    C.to_strip([bands], preset["colors"], out)
    fragment["sprites"].append({
        "name": "sky_bands",
        "file": "strips/sky_bands.png",
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
    print("backgrounds: sky_bands   %dx%d (%d band(s))"
          % (bands.w, bands.h, len(SKY_BANDS)))

    maps = os.path.join(out_dir, "maps")
    os.makedirs(maps, exist_ok=True)
    with open(os.path.join(maps, "sky_dusk.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(sky_map()) + "\n")
    fragment["maps"].append({
        "name": "sky_dusk",
        "file": "maps/sky_dusk.txt",
        "tileset": "sky_bands",
        "tile_w": SKY_TILE,
        "tile_h": SKY_TILE,
        "legend": dict((chr(ord("a") + r), r) for r in range(SKY_ROWS)),
        "provenance": {
            "tool": "procedural", "model": "",
            "prompt": "the backgrounds pack: "
                      "art/library/backgrounds/gen_backgrounds.py",
            "seed": SEED, "licence": "this repository",
        },
    })
    print("backgrounds: sky_dusk    %dx%d cells" % (SKY_COLS, SKY_ROWS))

    # --- the day sky: the same bands, another ramp -----------------------
    with open(os.path.join(ROOT, "art", "palettes",
                           "%s.json" % DAY_PALETTE)) as f:
        day_preset = json.load(f)
    fragment["palettes"][DAY_PALETTE] = {"bank": BANK + 1,
                                         "colors": list(day_preset["colors"])}
    day_bands = sky_bands(DAY_BANDS)
    out = os.path.join(strips, "sky_bands_day.png")
    C.to_strip([day_bands], day_preset["colors"], out)
    fragment["sprites"].append({
        "name": "sky_bands_day",
        "file": "strips/sky_bands_day.png",
        "palette": DAY_PALETTE,
        "frames": 1,
        "origin": [0, 0],
        "provenance": {
            "tool": "procedural", "model": "",
            "prompt": "the backgrounds pack: "
                      "art/library/backgrounds/gen_backgrounds.py",
            "seed": SEED, "licence": "this repository",
        },
    })
    print("backgrounds: sky_bands_day %dx%d (%d band(s))"
          % (day_bands.w, day_bands.h, len(DAY_BANDS)))
    with open(os.path.join(maps, "sky_day.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(sky_map()) + "\n")
    fragment["maps"].append({
        "name": "sky_day",
        "file": "maps/sky_day.txt",
        "tileset": "sky_bands_day",
        "tile_w": SKY_TILE,
        "tile_h": SKY_TILE,
        "legend": dict((chr(ord("a") + r), r) for r in range(SKY_ROWS)),
        "provenance": {
            "tool": "procedural", "model": "",
            "prompt": "the backgrounds pack: "
                      "art/library/backgrounds/gen_backgrounds.py",
            "seed": SEED, "licence": "this repository",
        },
    })
    print("backgrounds: sky_day      %dx%d cells" % (SKY_COLS, SKY_ROWS))

    fragment_path = os.path.join(out_dir, "backgrounds.json")
    with open(fragment_path, "w", encoding="utf-8") as f:
        json.dump(fragment, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("backgrounds: %s" % fragment_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
