#!/usr/bin/env python3
"""Generate the map_demo tileset art: four 16x16 tiles in one 64x16 strip.

This is the art master for the map-import experiment (ROADMAP_CREATION_FLOW
Wave 4, docs/experiments/arc1-assetc.md). The script renders the tileset
parametrically (no PIL, deterministic) and writes

    programs/experimental/mapdemo_tiles.png

The manifest (mapdemo_tiles.json) and the ASCII maps are hand-authored
committed files that reference it; tools/arc1_assetc.py compiles all of
them into mapdemo_tiles.h (tiles blob + cells arrays). Regenerate with:

    python3 tools/gen_map_tiles.py
    python3 tools/arc1_assetc.py \
        --assets programs/experimental/mapdemo_tiles.json \
        --out-dir programs/experimental

(`make mapdemo-sheet` runs the pair.)

The four tiles, left to right in scan order:
  0  solid brick   — the M29 lesson's field tile, one tile serves a field
  1  marker        — a 2 px column on the LEFT over transparency, so a
                     flipX in the map's cells is visible as a mirror
  2  stripe        — a second solid colour, for exact pixel counts
  3  brick again   — BYTE-IDENTICAL to tile 0: dedup must collapse it
"""
import struct
import sys
import zlib

TILE = 16          # tile_w x tile_h (even width: the 4-bit blit's rule)
N_TILES = 4
W, H = TILE * N_TILES, TILE

# Palette as direct RGB565 components (r,b: 0..31; g: 0..63); index 0 is
# transparent. The manifest must list exactly these expanded colours —
# arc1-assetc's "quantize": "exact" fails loudly if they ever drift.
PAL = [
    None,        # 0  transparent (key #000000 in the PNG)
    (24, 12, 6),  # 1  brick brown
    (6, 60, 10),  # 2  marker green
    (8, 24, 30),  # 3  stripe blue
]


def hex8(comp):
    """The device's 5/6/5 -> 8/8/8 expansion as #RRGGBB; assetc's rgb565()
    maps it back to `comp` exactly (injective), so the strip compiles to
    the same nibbles a direct index grid would."""
    r5, g6, b5 = comp
    r8 = (r5 << 3) | (r5 >> 2)
    g8 = (g6 << 2) | (g6 >> 4)
    b8 = (b5 << 3) | (b5 >> 2)
    assert ((r8 >> 3, g8 >> 2, b8 >> 3) == (r5, g6, b5)), comp
    return "#%02X%02X%02X" % (r8, g8, b8)


def draw_tile(index):
    """The pixel indices of tile `index` (a TILE x TILE grid)."""
    grid = [[0] * TILE for _ in range(H)]
    if index in (0, 3):
        for y in range(TILE):
            for x in range(TILE):
                grid[y][x] = 1              # tile 3 duplicates tile 0
    elif index == 1:
        for y in range(TILE):               # 2 px column on the left only
            grid[y][0] = 2
            grid[y][1] = 2
    elif index == 2:
        for y in range(TILE):
            for x in range(TILE):
                grid[y][x] = 3
    return grid


def write_png(path, tiles):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + \
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    colors = [None if comp is None else
              ((comp[0] << 3) | (comp[0] >> 2),
               (comp[1] << 2) | (comp[1] >> 4),
               (comp[2] << 3) | (comp[2] >> 2)) for comp in PAL]
    raw = b""
    for y in range(H):
        row = b""
        for g in tiles:
            for x in range(TILE):
                row += bytes(colors[g[y][x]] or (0, 0, 0))
        raw += b"\x00" + row
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else \
        "programs/experimental/mapdemo_tiles.png"
    tiles = [draw_tile(i) for i in range(N_TILES)]
    write_png(out, tiles)
    print("gen_map_tiles: %s (%dx%d, %d tiles)" % (out, W, H, N_TILES))
    print("gen_map_tiles: manifest colours (must match the palette order): "
          + " ".join(hex8(c) for c in PAL[1:]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
