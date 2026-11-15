#!/usr/bin/env python3
"""preview.py — a look at the round 6 art, for the visual review.

Not a game asset: a scratch tool that draws the map the manifest describes
(30x16 cells of 16x16) with the farmer, the slime and the four pickups on
it, so the art can be judged by eye (art/CRITIQUE.md: "a sprite can pass
every check and still be ugly").

    python3 round6/art/preview.py            # -> round6/art/preview_map.png
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import gen_round6 as G                          # noqa: E402
from arc1_art import canvas as C               # noqa: E402

OUT = os.path.join(HERE, "preview_map.png")

# Where the things stand in the village (tile coordinates).
FARMER_AT = (5, 12)
SLIME_AT = (25, 3)
PICKUPS_AT = [(13, 8), (22, 8), (13, 10), (22, 10)]
PICKUP_KINDS = [G.pickup_coin, G.pickup_apple, G.pickup_carrot,
                G.pickup_cabbage]

LEGEND = {"g": 0, "p": 1, "w": 2}


def rgb_of(palette, index):
    """The 8-bit RGB of one palette index (None = transparent)."""
    if index == 0:
        return (0, 0, 0)
    v = int(palette[index].lstrip("#"), 16)
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def compose():
    """The 480x270 preview: the map, the sprites, and a 2x copy."""
    tiles = G.draw_tileset()
    ground_pal = G.PALETTES["ground"]

    W = G.MAP_COLS * G.TILE
    H = G.MAP_ROWS * G.TILE
    px = [[(0, 0, 0)] * W for _ in range(H)]

    for r in range(G.MAP_ROWS):
        for c in range(G.MAP_COLS):
            tile = LEGEND[G.MAP_ROWS_TEXT[r][c]]
            tx = (tile % 3) * G.TILE
            ty = (tile // 3) * G.TILE
            for y in range(G.TILE):
                for x in range(G.TILE):
                    px[r * G.TILE + y][c * G.TILE + x] = \
                        rgb_of(ground_pal, tiles.p[ty + y][tx + x])

    def stamp(canvas, palette, tx, ty):
        for y in range(canvas.h):
            for x in range(canvas.w):
                v = canvas.p[y][x]
                if not v:
                    continue
                X = tx * G.TILE + x
                Y = ty * G.TILE + y
                if 0 <= X < W and 0 <= Y < H:
                    px[Y][X] = rgb_of(palette, v)

    farmer = G.farmer_frame(0)
    stamp(farmer, G.PALETTES["farmer"], FARMER_AT[0], FARMER_AT[1])
    slime = G.slime_frame(False)
    stamp(slime, G.PALETTES["slime"], SLIME_AT[0], SLIME_AT[1])
    for (at, kind) in zip(PICKUPS_AT, PICKUP_KINDS):
        stamp(kind(), G.PALETTES["props"], at[0], at[1])

    # the preview is the map at 2x, so the eye can judge it
    scale = 2
    raw = bytearray()
    for y in range(H):
        for _ in range(scale):
            raw.append(0)
            for x in range(W):
                raw += bytes(px[y][x]) * scale
    C.write_indexed_png(OUT, raw, W * scale, H * scale)

    # and the same map at 1x — the size the machine actually shows
    raw1 = bytearray()
    for y in range(H):
        raw1.append(0)
        for x in range(W):
            raw1 += bytes(px[y][x])
    C.write_indexed_png(os.path.join(HERE, "preview_map_1x.png"),
                        raw1, W, H)
    return OUT


def sheet():
    """A 4x sheet of every sprite, for the eye (the human half of the
    review)."""
    items = [(G.farmer_frame(0), G.PALETTES["farmer"]),
             (G.farmer_frame(1), G.PALETTES["farmer"]),
             (G.slime_frame(False), G.PALETTES["slime"]),
             (G.slime_frame(True), G.PALETTES["slime"]),
             (G.pickup_coin(), G.PALETTES["props"]),
             (G.pickup_apple(), G.PALETTES["props"]),
             (G.pickup_carrot(), G.PALETTES["props"]),
             (G.pickup_cabbage(), G.PALETTES["props"])]
    scale = 4
    cw = max(it[0].w for it in items) * scale + 8
    ch = max(it[0].h for it in items) * scale + 8
    W = cw * len(items)
    raw = bytearray()
    for y in range(ch):
        raw.append(0)
        for x in range(W):
            i = x // cw
            lx = (x % cw - 4) // scale
            ly = (y - 4) // scale
            canvas, palette = items[i]
            v = canvas.p[ly][lx] if (0 <= lx < canvas.w and
                                     0 <= ly < canvas.h) else 0
            raw += bytes(rgb_of(palette, v) if v else (24, 24, 32))
    path = os.path.join(HERE, "preview_sprites.png")
    C.write_indexed_png(path, raw, W, ch)
    return path


if __name__ == "__main__":
    print(compose())
    print(sheet())
