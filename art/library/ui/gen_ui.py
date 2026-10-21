#!/usr/bin/env python3
"""gen_ui.py — the library's `ui` pack.

The interface layer of a game: a panel (in three sizes), a cursor and a
pip for counters.

A **panel** is a tileset of nine pieces — the corners, the edges and the
fill — and a *map* per size that lays them out. The device cannot stretch
a tile, so the panels are fixed sizes (2x2, 3x3, 4x4 cells), not a
9-slice that stretches.

Deterministic: fixed seeds, no clock, sorted iteration.

    python3 art/library/ui/gen_ui.py --out-dir art/library/ui

It writes:

    strips/panel_tiles.png   the nine panel pieces, 3x3 of 16x16, ONE frame
    strips/cursor.png        the pointer, 7x9
    strips/pip.png           the counter dot, 5x5
    maps/panel_small.txt     the 2x2 panel
    maps/panel_mid.txt       the 3x3 panel
    maps/panel_big.txt       the 4x4 panel
    ui.json                  the manifest fragment
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
PALETTE = "ui16"
BANK = 14

DARK = 1        # the outline
PANEL_D = 2
PANEL = 3
PANEL_L = 4
EDGE = 5
EDGE_L = 6
TEXT = 7
TEXT_L = 8
WHITE = 9
BRIGHT = 10
RED = 11
AMBER = 12
GREEN = 13
BLUE = 14

SEED = 20261021


def _panel_tiles():
    """The nine pieces of a panel, laid out 3x3 in ONE frame.

    The order is the map's legend: c(0) h(1) v(2) f(3) and the corners
    that repeat them.
    """
    sheet = C.Canvas(TILE * 3, TILE * 3)

    def piece(x0, y0, w, h, fill, edge, corner):
        c = C.Canvas(TILE, TILE)
        c.rect(0, 0, TILE, TILE, fill)
        # the border
        for x in range(TILE):
            c.px(x, 0, edge)
            c.px(x, TILE - 1, edge)
        for y in range(TILE):
            c.px(0, y, edge)
            c.px(TILE - 1, y, edge)
        if corner:
            c.rect(0, 0, 3, 3, corner)
            c.rect(TILE - 3, 0, 3, 3, corner)
            c.rect(0, TILE - 3, 3, 3, corner)
            c.rect(TILE - 3, TILE - 3, 3, 3, corner)
        return c

    pieces = [
        piece(0, 0, 0, 0, PANEL, EDGE, EDGE_L),      # the corner
        piece(0, 0, 0, 0, PANEL, EDGE, None),        # the horizontal edge
        piece(0, 0, 0, 0, PANEL, EDGE, None),        # the vertical edge
        piece(0, 0, 0, 0, PANEL, PANEL_D, None),     # the fill
        piece(0, 0, 0, 0, PANEL, EDGE, EDGE_L),      # corner
        piece(0, 0, 0, 0, PANEL, EDGE, None),        # edge
        piece(0, 0, 0, 0, PANEL, EDGE, None),        # edge
        piece(0, 0, 0, 0, PANEL, EDGE, EDGE_L),      # corner
        piece(0, 0, 0, 0, PANEL, PANEL_D, None),     # fill
    ]
    for i, piece_canvas in enumerate(pieces):
        sheet.stamp(piece_canvas, (i % 3) * TILE, (i // 3) * TILE)
    return sheet


def cursor():
    """The pointer: an 8x8 arrow with a dark edge and a white body.

    The shape is SOLID (one piece, no orphan pixels) and its edge is the
    outline colour, so it reads as a pointer at 1x and passes the sprite
    checks like any other.
    """
    c = C.Canvas(8, 8)
    for y in range(8):
        for x in range(8):
            if x <= y:
                c.px(x, y, DARK)
    for y in range(1, 8):
        for x in range(1, y):
            c.px(x, y, WHITE)
    c.px(0, 0, BRIGHT)
    return c


def pip():
    """A counter dot: an 8x8 disc with a dark rim and a red centre."""
    c = C.Canvas(8, 8)
    for y in range(8):
        for x in range(8):
            d = (x - 3.5) ** 2 + (y - 3.5) ** 2
            if d <= 15.0:
                c.px(x, y, DARK)
    for y in range(8):
        for x in range(8):
            d = (x - 3.5) ** 2 + (y - 3.5) ** 2
            if d <= 7.0:
                c.px(x, y, RED if d <= 2.0 else WHITE)
    return c


PANELS = [("panel_small", 2), ("panel_mid", 3), ("panel_big", 4)]
# the legend of a panel map: c=corner h=edge v=edge f=fill, in the
# tileset's scan order (the 3x3 pieces)
LEGEND = {"c": 0, "h": 1, "v": 2, "f": 3, "C": 4, "H": 5, "V": 6, "F": 7}


def main():
    out_dir = HERE
    args = sys.argv[1:]
    if "--out-dir" in args:
        out_dir = os.path.abspath(args[args.index("--out-dir") + 1])
    strips = os.path.join(out_dir, "strips")
    maps = os.path.join(out_dir, "maps")
    os.makedirs(strips, exist_ok=True)
    os.makedirs(maps, exist_ok=True)

    with open(os.path.join(ROOT, "art", "palettes", "%s.json" % PALETTE)) as f:
        preset = json.load(f)

    fragment = {
        "format": 1,
        "description": "The interface kit: a panel in three sizes (a nine-"
                       "piece tileset plus a map per size), a cursor and a "
                       "counter pip.",
        "palettes": {
            PALETTE: {"bank": BANK, "colors": list(preset["colors"])},
        },
        "roles": {"panel_tiles": "tileset", "cursor": "sprite",
                   "pip": "sprite"},
        "sprites": [],
        "animations": [],
        "maps": [],
    }

    tiles = _panel_tiles()
    out = os.path.join(strips, "panel_tiles.png")
    C.to_strip([tiles], preset["colors"], out)
    fragment["sprites"].append({
        "name": "panel_tiles",
        "file": "strips/panel_tiles.png",
        "palette": PALETTE,
        "frames": 1,
        "origin": [0, 0],
        "provenance": {
            "tool": "procedural", "model": "",
            "prompt": "the ui pack: art/library/ui/gen_ui.py",
            "seed": SEED, "licence": "this repository",
        },
    })
    print("ui: panel_tiles (3x3 pieces of 16x16, one frame)")

    cur = cursor()
    out = os.path.join(strips, "cursor.png")
    C.to_strip([cur], preset["colors"], out)
    fragment["sprites"].append({
        "name": "cursor",
        "file": "strips/cursor.png",
        "palette": PALETTE,
        "frames": 1,
        "origin": [0, 0],
        "provenance": {
            "tool": "procedural", "model": "",
            "prompt": "the ui pack: art/library/ui/gen_ui.py",
            "seed": SEED, "licence": "this repository",
        },
    })
    print("ui: cursor (8x8)")

    dot = pip()
    out = os.path.join(strips, "pip.png")
    C.to_strip([dot], preset["colors"], out)
    fragment["sprites"].append({
        "name": "pip",
        "file": "strips/pip.png",
        "palette": PALETTE,
        "frames": 1,
        "origin": [0, 0],
        "provenance": {
            "tool": "procedural", "model": "",
            "prompt": "the ui pack: art/library/ui/gen_ui.py",
            "seed": SEED, "licence": "this repository",
        },
    })
    print("ui: pip (8x8)")

    for name, size in PANELS:
        rows = []
        for r in range(size):
            row = ""
            for c in range(size):
                if r in (0, size - 1) and c in (0, size - 1):
                    row += "c"
                elif r in (0, size - 1):
                    row += "h"
                elif c in (0, size - 1):
                    row += "v"
                else:
                    row += "f"
            rows.append(row)
        path = os.path.join(maps, "%s.txt" % name)
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(rows) + "\n")
        fragment["maps"].append({
            "name": name,
            "file": "maps/%s.txt" % name,
            "tileset": "panel_tiles",
            "tile_w": TILE,
            "tile_h": TILE,
            "legend": {"c": 0, "h": 1, "v": 2, "f": 3},
            "provenance": {
                "tool": "procedural", "model": "",
                "prompt": "the ui pack: art/library/ui/gen_ui.py",
                "seed": SEED, "licence": "this repository",
            },
        })
        print("ui: %s (%dx%d cells)" % (name, size, size))

    fragment_path = os.path.join(out_dir, "ui.json")
    with open(fragment_path, "w", encoding="utf-8") as f:
        json.dump(fragment, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("ui: %s" % fragment_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
