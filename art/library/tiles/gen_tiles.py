#!/usr/bin/env python3
"""gen_tiles.py — the library's `tiles` pack.

The surfaces a game stands on: twelve seamless 16x16 tiles in one tileset —
grass, dirt, stone, brick, wood, sand, water, roof, floor, road, snow, lava.

**Seamless** is the whole point, and it is the mistake the rounds made
twice: a border or a seam in a tileset repeats in EVERY cell of the map
(the round-6 field was a striped quilt; the round-7 cliff was a row of
boxes). So no tile has a border, and every pattern is drawn to wrap.

The tileset is ONE frame spanning the whole tileset image — the map
importer's rule, and the one that cost the round-7 agent an hour.

Deterministic: fixed seeds, no clock, sorted iteration.

    python3 art/library/tiles/gen_tiles.py --out-dir art/library/tiles

It writes:

    strips/tiles.png     the tileset: 4x3 tiles of 16x16, ONE frame, 64x48
    tiles.json           the manifest fragment
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from arc1_art import canvas as C                              # noqa: E402

TILE = 16
COLS = 4
ROWS = 3

# The palette: one bank for the whole set (a tileset is one group).
PALETTE = "terrain16"
BANK = 12

# The palette's indices, named so the tile code reads like art.
OUTLINE = 1
GREEN_D = 2
GREEN = 3
GREEN_L = 4
BROWN_D = 5
BROWN = 6
TAN = 7
GREY_D = 8
GREY = 9
GREY_L = 10
RED_D = 11
RED = 12
BLUE_D = 13
BLUE = 14
CREAM = 15


def _base(colour):
    c = C.Canvas(TILE, TILE)
    c.rect(0, 0, TILE, TILE, colour)
    return c


def grass(seed):
    """Green with blades — the pattern must not read as rows."""
    c = _base(GREEN)
    r = C.Rng(seed)
    for _ in range(26):
        x, y = r.rand(TILE), r.rand(TILE)
        c.px(x, y, GREEN_L)
        c.px(x, (y + 1) % TILE, GREEN_L)
    for _ in range(10):
        x, y = r.rand(TILE), r.rand(TILE)
        c.px(x, y, GREEN_D)
    return c


def dirt(seed):
    c = _base(BROWN)
    r = C.Rng(seed)
    for _ in range(22):
        x, y = r.rand(TILE), r.rand(TILE)
        c.px(x, y, BROWN_D)
    for _ in range(8):
        x, y = r.rand(TILE), r.rand(TILE)
        c.px(x, y, TAN)
        c.px((x + 1) % TILE, y, TAN)
    return c


def stone(seed):
    """Cobble: cells of 4x4 with a dark joint."""
    c = _base(GREY)
    r = C.Rng(seed)
    for by in range(0, TILE, 4):
        for bx in range(0, TILE, 4):
            v = GREY if r.rand(2) else GREY_L
            c.rect(bx, by, 4, 4, v)
    for y in range(TILE):
        for x in range(TILE):
            if x % 4 == 0 or y % 4 == 0:
                c.px(x, y, GREY_D)
    return c


def brick(seed):
    """Bricks in a running bond, the joints in the dark tone."""
    c = _base(RED)
    r = C.Rng(seed)
    for row in range(0, TILE, 4):
        offset = 0 if (row // 4) % 2 == 0 else 4
        for x in range(-8, TILE, 8):
            c.rect(x + offset, row, 7, 3, RED)
    for y in range(TILE):
        for x in range(TILE):
            if y % 4 == 0:
                c.px(x, y, RED_D)
    return c


def wood(seed):
    """Vertical planks: the seam is a column, and it repeats by design."""
    c = _base(BROWN)
    r = C.Rng(seed)
    for x in range(0, TILE, 4):
        v = BROWN if r.rand(2) else TAN
        c.rect(x, 0, 3, TILE, v)
        for _ in range(6):
            c.px(x + r.rand(3), r.rand(TILE), BROWN_D)
    for x in range(0, TILE, 4):
        c.px(x, 0, BROWN_D)
    return c


def sand(seed):
    c = _base(TAN)
    r = C.Rng(seed)
    for _ in range(30):
        x, y = r.rand(TILE), r.rand(TILE)
        c.px(x, y, CREAM)
    for _ in range(12):
        c.px(r.rand(TILE), r.rand(TILE), BROWN)
    return c


def water(seed):
    """A wave: two sine rows, wrapped."""
    c = _base(BLUE)
    r = C.Rng(seed)
    for y in range(TILE):
        for x in range(TILE):
            wave = ((x + y // 2) % 8)
            if wave in (0, 1):
                c.px(x, y, BLUE_D)
            elif wave == 4 and r.rand(3) == 0:
                c.px(x, y, CREAM)
    return c


def roof(seed):
    """Shingles: rows of overlapping tiles."""
    c = _base(RED_D)
    r = C.Rng(seed)
    for row in range(0, TILE, 4):
        for x in range(0, TILE, 4):
            v = RED if r.rand(3) else BROWN
            c.rect(x, row, 4, 3, v)
    return c


def floor(seed):
    """Boards across, with the nails."""
    c = _base(BROWN)
    r = C.Rng(seed)
    for row in range(0, TILE, 8):
        v = BROWN if r.rand(2) else TAN
        c.rect(0, row, TILE, 7, v)
        c.px(1, row + 1, GREY_D)
        c.px(TILE - 2, row + 1, GREY_D)
    for y in range(0, TILE, 8):
        c.px(0, y, BROWN_D)
    return c


def road(seed):
    """Cobble, larger and rounder than the stone."""
    c = _base(GREY_D)
    r = C.Rng(seed)
    for by in range(0, TILE, 8):
        for bx in range(0, TILE, 8):
            v = GREY if r.rand(2) else GREY_L
            c.rect(bx + 1, by + 1, 6, 6, v)
    return c


def snow(seed):
    c = _base(CREAM)
    r = C.Rng(seed)
    for _ in range(18):
        x, y = r.rand(TILE), r.rand(TILE)
        c.px(x, y, GREY_L)
    for _ in range(6):
        c.px(r.rand(TILE), r.rand(TILE), BLUE_D)
    return c


def lava(seed):
    """Dark rock with bright cracks — the cracks wrap."""
    c = _base(RED_D)
    r = C.Rng(seed)
    for _ in range(14):
        x, y = r.rand(TILE), r.rand(TILE)
        c.px(x, y, RED)
        c.px((x + 1) % TILE, y, RED)
        c.px(x, (y + 1) % TILE, RED)
        if r.rand(2):
            c.px(x, y, CREAM)
    return c


TILES = [
    ("grass", grass), ("dirt", dirt), ("stone", stone), ("brick", brick),
    ("wood", wood), ("sand", sand), ("water", water), ("roof", roof),
    ("floor", floor), ("road", road), ("snow", snow), ("lava", lava),
]

SEED = 20261021


def main():
    out_dir = HERE
    args = sys.argv[1:]
    if "--out-dir" in args:
        out_dir = os.path.abspath(args[args.index("--out-dir") + 1])
    strips = os.path.join(out_dir, "strips")
    os.makedirs(strips, exist_ok=True)

    with open(os.path.join(ROOT, "art", "palettes", "%s.json" % PALETTE)) as f:
        preset = json.load(f)

    # the tileset is ONE frame: the tiles sit side by side in one canvas
    sheet = C.Canvas(TILE * COLS, TILE * ROWS)
    for i, (name, make) in enumerate(TILES):
        tile = make(SEED + i)
        sheet.stamp(tile, (i % COLS) * TILE, (i // COLS) * TILE)
        print("tiles: %-6s %dx%d" % (name, TILE, TILE))
    out = os.path.join(strips, "tiles.png")
    C.to_strip([sheet], preset["colors"], out)
    print("tiles: %s (%dx%d, %d tiles)"
          % (out, sheet.w, sheet.h, len(TILES)))

    fragment = {
        "format": 1,
        "description": "Twelve seamless 16x16 surface tiles in one tileset: "
                       "grass, dirt, stone, brick, wood, sand, water, roof, "
                       "floor, road, snow, lava.",
        "palettes": {
            PALETTE: {"bank": BANK, "colors": list(preset["colors"])},
        },
        "roles": {"tiles": "tileset"},
        "sprites": [{
            "name": "tiles",
            "file": "strips/tiles.png",
            "palette": PALETTE,
            "frames": 1,
            "origin": [0, 0],
            "provenance": {
                "tool": "procedural",
                "model": "",
                "prompt": "the tiles pack: art/library/tiles/gen_tiles.py",
                "seed": SEED,
                "licence": "this repository",
            },
        }],
        "animations": [],
        "maps": [],
    }
    fragment_path = os.path.join(out_dir, "tiles.json")
    with open(fragment_path, "w", encoding="utf-8") as f:
        json.dump(fragment, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("tiles: %s" % fragment_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
