#!/usr/bin/env python3
"""Generate ASTEROID RUN's placeholder art (Wave 5 / Game #1).

This is the PLACEHOLDER art master — clearly marked, per the owner's
decision (no designer art was provided for Game #1). Everything is
parametrically rendered (stdlib only, deterministic) and written into
programs/experimental/:

    asteroid_run_player.png    64x16 — 4 frames of 16x16 ship:
                               0 idle, 1 boost (short flame),
                               2 boost (long flame), 3 fire (muzzle flash)
    asteroid_run_enemies.png   48x16 — 3 frames of 16x16:
                               0/1 asteroid spin pair, 2 comet (two types)
    asteroid_run_tiles.png     48x16 — ONE frame of three 16x16 tiles:
                               0 sparse stars, 1 nebula, 2 star cluster
    asteroid_run_level.txt     30x40 ASCII map: the scrolling level
                               (480x640 world; '.' empty, 's' stars,
                               'n' nebula, 'N' nebula flipped, 'c' cluster)

The manifest (asteroid_run_sheet.json) is hand-authored and references
these files; tools/arc1_assetc.py compiles everything (sprites, the
Wave 3 animations, the Wave 4 map) into asteroid_run_sheet.h. Regenerate
with `make game1-sheet`. If the manifest's palette ever drifts from the
list printed here, assetc's "quantize": "exact" fails loudly.
"""
import struct
import sys
import zlib

PLAYER_W, PLAYER_H, PLAYER_FRAMES = 64, 16, 4
FOE_W, FOE_H, FOE_FRAMES = 48, 16, 3
TILE_W, TILE_H, TILE_FRAMES = 48, 16, 1
LEVEL_COLS, LEVEL_ROWS = 30, 40

# Palette as direct RGB565 components (r,b: 0..31; g: 0..63); index 0 is
# transparent (key #000000 in the PNGs).
PAL = [
    None,             # 0  transparent
    (31, 63, 31),     # 1  white        hull light, stars
    (10, 50, 31),     # 2  cyan         hull panels, comet glow
    (6, 18, 28),      # 3  deep blue    hull dark / wings
    (31, 40, 4),      # 4  orange       flame
    (31, 60, 8),      # 5  yellow       flame hot, muzzle flash
    (20, 42, 18),     # 6  rock grey    asteroid body
    (10, 20, 10),     # 7  rock dark    craters, rim
    (31, 55, 10),     # 8  comet core
    (18, 10, 28),     # 9  nebula purple
    (8, 22, 30),      # 10 nebula blue
]


def hex8(comp):
    """The device's 5/6/5 -> 8/8/8 expansion as #RRGGBB (arc1-assetc's
    rgb565() maps it back exactly — same trick as gen_map_tiles.py)."""
    r5, g6, b5 = comp
    r8 = (r5 << 3) | (r5 >> 2)
    g8 = (g6 << 2) | (g6 >> 4)
    b8 = (b5 << 3) | (b5 >> 2)
    assert ((r8 >> 3, g8 >> 2, b8 >> 3) == (r5, g6, b5)), comp
    return "#%02X%02X%02X" % (r8, g8, b8)


def grid():
    return [[0] * 16 for _ in range(16)]


def fill_row(g, y, x0, x1, v):
    for x in range(x0, x1 + 1):
        g[y][x] = v


def ship(g, y0):
    """The hull shared by every player frame, drawn at rows y0..y0+11."""
    fill_row(g, y0 + 2, 7, 8, 1)
    fill_row(g, y0 + 3, 7, 8, 1)
    fill_row(g, y0 + 4, 6, 9, 2)
    fill_row(g, y0 + 5, 6, 9, 2)
    fill_row(g, y0 + 6, 5, 10, 2)
    fill_row(g, y0 + 7, 5, 10, 2)
    fill_row(g, y0 + 8, 4, 11, 3)
    fill_row(g, y0 + 9, 2, 13, 3)
    fill_row(g, y0 + 10, 1, 14, 3)
    fill_row(g, y0 + 11, 0, 15, 3)
    fill_row(g, y0 + 13, 6, 9, 3)   # engine


def player_frame(index):
    g = grid()
    ship(g, 1)
    if index == 1:                       # boost: short flame
        fill_row(g, 14, 6, 9, 4)
        fill_row(g, 15, 7, 8, 5)
    elif index == 2:                     # boost: long flame
        fill_row(g, 14, 5, 10, 4)
        fill_row(g, 15, 6, 9, 5)
    elif index == 3:                     # fire: muzzle flash above the nose
        fill_row(g, 1, 7, 8, 5)
        fill_row(g, 0, 6, 9, 4)
    return g


def asteroid_frame(variant):
    """A 16x16 rock disc; the two spin variants move the craters."""
    g = grid()
    for y in range(16):
        for x in range(16):
            dx, dy = x - 7, y - 7
            if dx * dx + dy * dy <= 49:
                g[y][x] = 6
            if dx * dx + dy * dy >= 44:
                g[y][x] = 7                       # dark rim
    craters = ((-2, -1), (3, 2)) if variant == 0 else ((0, -3), (-3, 2))
    for cx, cy in craters:
        for y in range(16):
            for x in range(16):
                dx, dy = x - (7 + cx), y - (7 + cy)
                if dx * dx + dy * dy <= 3:
                    g[y][x] = 7
    return g


def comet_frame():
    """A bright core low, a rising tapering tail — it falls down-screen."""
    g = grid()
    fill_row(g, 11, 6, 9, 8)
    fill_row(g, 12, 6, 9, 8)
    fill_row(g, 13, 7, 8, 8)
    fill_row(g, 14, 7, 8, 2)
    for y in range(2, 10):                       # tail, rising to the left
        g[y][9 - (y - 2) // 2] = 1
        if y >= 5:
            g[y][10 - (y - 2) // 2] = 2
    return g


def tile_stars():
    g = grid()
    for (x, y, v) in ((2, 3, 1), (11, 1, 2), (6, 8, 1), (14, 12, 2),
                      (4, 14, 1), (9, 11, 1)):
        g[y][x] = v
    return g


def tile_nebula():
    g = grid()
    for y in range(16):
        half = [0, 0, 1, 3, 4, 5, 6, 6, 6, 6, 5, 4, 3, 1, 0, -1][y]
        for x in range(8 - half, 8 + half + 1):
            g[y][x] = 9 if (x + y) % 2 else 10
    return g


def tile_cluster():
    g = grid()
    for (x, y) in ((3, 2), (12, 3), (5, 11), (13, 12)):
        g[y][x] = 8
    g[7][8] = 1
    g[6][9] = 1
    g[9][7] = 2
    return g


def level_rows():
    """The scrolling level: a deterministic field with a lane variety of
    stars, clusters and nebulae (both the nebula AND its flipX twin)."""
    seed = 0xBEEF1

    def rng():
        nonlocal seed
        seed ^= (seed << 13) & 0xFFFFFFFF
        seed ^= seed >> 17
        seed ^= (seed << 5) & 0xFFFFFFFF
        return seed

    rows = []
    for _ in range(LEVEL_ROWS):
        cells = []
        for _ in range(LEVEL_COLS):
            r = rng() % 100
            if r < 55:
                cells.append('.')
            elif r < 85:
                cells.append('s')
            elif r < 93:
                cells.append('c')
            elif r < 97:
                cells.append('n')
            else:
                cells.append('N')
        rows.append(''.join(cells))
    return rows


def write_png(path, w, h, tiles):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + \
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    colors = [None if comp is None else
              ((comp[0] << 3) | (comp[0] >> 2),
               (comp[1] << 2) | (comp[1] >> 4),
               (comp[2] << 3) | (comp[2] >> 2)) for comp in PAL]
    raw = b""
    for y in range(h):
        row = b""
        for g in tiles:
            for x in range(16):
                row += bytes(colors[g[y][x]] or (0, 0, 0))
        raw += b"\x00" + row
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "programs/experimental/"
    write_png(d + "asteroid_run_player.png", PLAYER_W, PLAYER_H,
              [player_frame(i) for i in range(PLAYER_FRAMES)])
    write_png(d + "asteroid_run_enemies.png", FOE_W, FOE_H,
              [asteroid_frame(0), asteroid_frame(1), comet_frame()])
    write_png(d + "asteroid_run_tiles.png", TILE_W, TILE_H,
              [tile_stars(), tile_nebula(), tile_cluster()])
    with open(d + "asteroid_run_level.txt", "w") as f:
        f.write("\n".join(level_rows()) + "\n")
    print("gen_game1_art: player %dx%d, foes %dx%d, tiles %dx%d, level %dx%d"
          % (PLAYER_W, PLAYER_H, FOE_W, FOE_H, TILE_W, TILE_H,
             LEVEL_COLS, LEVEL_ROWS))
    print("gen_game1_art: palette colours (must match the manifest order): "
          + " ".join(hex8(c) for c in PAL[1:]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
