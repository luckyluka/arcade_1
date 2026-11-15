#!/usr/bin/env python3
"""gen_vectoroids_assets.py -- generate vectoroids_assets.h for the Vectoroids game.

Everything here is 1-bit (wireframe): gfx_sprite() draws set bits in one RGB565
colour and leaves clear bits transparent, which is exactly the look we want for
a vector game. The generator rasterises:

  * the ship triangle at 16 rotations (16x16)
  * four rock outlines at 3 sizes x 16 rotations
  * a 5x7 font (also 1-bit)

and emits integer sin/cos tables for the guest (no floating point on RV32).
"""
import math
import sys

FONT = {
 '0': [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
 '1': ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
 '2': [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
 '3': ["####.", "....#", "....#", ".###.", "....#", "....#", "####."],
 '4': ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
 '5': ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
 '6': [".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."],
 '7': ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
 '8': [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
 '9': [".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."],
 'A': ["..#..", ".#.#.", "#...#", "#...#", "#####", "#...#", "#...#"],
 'B': ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
 'C': [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
 'D': ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
 'E': ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
 'F': ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
 'G': [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."],
 'H': ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
 'I': [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
 'J': ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
 'K': ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
 'L': ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
 'M': ["#...#", "##.##", "#.#.#", "#...#", "#...#", "#...#", "#...#"],
 'N': ["#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"],
 'O': [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
 'P': ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
 'Q': [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
 'R': ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
 'S': [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
 'T': ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
 'U': ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
 'V': ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
 'W': ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"],
 'X': ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
 'Y': ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
 'Z': ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
}

# rock outline shapes: unit radius per vertex (8 vertices, closed)
ROCK_SHAPES = [
    [1.00, 0.75, 1.00, 0.80, 1.00, 0.70, 0.95, 0.85],
    [1.00, 0.60, 0.90, 1.00, 0.70, 0.95, 0.65, 1.00],
    [0.90, 1.00, 0.70, 0.85, 1.00, 0.60, 0.95, 0.90],
    [1.00, 0.85, 0.65, 1.00, 0.80, 0.70, 1.00, 0.90],
]
ROCK_RADII = [16, 10, 6]          # bitmap is 2R x 2R


def pack1(name, rows, w):
    nb = (w + 7) // 8
    body = []
    for r in rows:
        assert len(r) == w, (name, len(r), w, r)
        bs = []
        for b in range(nb):
            v = 0
            for bit in range(8):
                idx = b * 8 + bit
                if idx < w and r[idx] == '1':
                    v |= 1 << (7 - bit)
            bs.append(v)
        body.append("    " + ", ".join("0x%02X" % x for x in bs) + ",")
    return "static const unsigned char %s[%d] = {\n%s\n};\n" % (
        name, len(rows) * nb, "\n".join(body))


def blank(w, h):
    return [['0'] * w for _ in range(h)]


def draw_line(bm, w, h, x0, y0, x1, y1):
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        if 0 <= x0 < w and 0 <= y0 < h:
            bm[y0][x0] = '1'
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def emit_rows(bm):
    return ["".join(r) for r in bm]


def ship_rows(rot):
    R = 8
    D = 16
    a0 = 2 * math.pi * rot / 16.0

    def pt(rr, rel):
        a = a0 + rel
        return (R + rr * math.cos(a), R + rr * math.sin(a))

    nose = pt(7.0, 0.0)
    left = pt(6.0, math.radians(140))
    right = pt(6.0, -math.radians(140))
    bm = blank(D, D)
    for (x0, y0), (x1, y1) in ((nose, left), (left, right), (right, nose)):
        draw_line(bm, D, D, int(round(x0)), int(round(y0)),
                  int(round(x1)), int(round(y1)))
    return emit_rows(bm)


def rock_rows(R, shape_id, rot):
    n = len(ROCK_SHAPES[shape_id])
    pts = []
    for k in range(n):
        ang = 2 * math.pi * (k / n + rot / 16.0)
        rr = R * 0.82 * ROCK_SHAPES[shape_id][k]
        pts.append((R + rr * math.cos(ang), R + rr * math.sin(ang)))
    bm = blank(2 * R, 2 * R)
    for k in range(n):
        x0, y0 = pts[k]
        x1, y1 = pts[(k + 1) % n]
        draw_line(bm, 2 * R, 2 * R, int(round(x0)), int(round(y0)),
                  int(round(x1)), int(round(y1)))
    return emit_rows(bm)


def glyph_rows(ch):
    pat = FONT[ch] if ch in FONT else ["....."] * 7
    rows = []
    for y in range(8):
        row = pat[y] if y < 7 else "....."
        rows.append(row.replace(".", "0").replace("#", "1"))
    return rows


def main():
    out = ["/* vectoroids_assets.h -- GENERATED by gen_vectoroids_assets.py. */\n",
           "#ifndef VECTOROIDS_ASSETS_H\n#define VECTOROIDS_ASSETS_H\n\n"]

    # trig tables: 16 angles, scaled by 256
    cos_t = [int(round(math.cos(2 * math.pi * a / 16.0) * 256)) for a in range(16)]
    sin_t = [int(round(math.sin(2 * math.pi * a / 16.0) * 256)) for a in range(16)]
    out.append("static const int costab[16] = { %s };\n" % ", ".join(map(str, cos_t)))
    out.append("static const int sintab[16] = { %s };\n\n" % ", ".join(map(str, sin_t)))
    out.append("#define ROCK_N_SIZE 3\n#define ROCK_N_SHAPE 4\n#define ROCK_N_ROT 16\n")
    out.append("static const int rock_rad[3] = { %s };\n" % ", ".join(map(str, ROCK_RADII)))
    out.append("static const int rock_wh[3] = { %s };\n\n"
               % ", ".join(str(2 * R) for R in ROCK_RADII))

    # ship
    for r in range(16):
        out.append(pack1("ship_%d" % r, ship_rows(r), 16) + "\n")
    out.append("static const unsigned char *const ship_img[16] = { %s };\n\n"
               % ", ".join("ship_%d" % r for r in range(16)))

    # rocks
    names = {}
    for s, R in enumerate(ROCK_RADII):
        for sh in range(4):
            for rot in range(16):
                nm = "rk_%d_%d_%d" % (s, sh, rot)
                names[(s, sh, rot)] = nm
                out.append(pack1(nm, rock_rows(R, sh, rot), 2 * R) + "\n")
    out.append("static const unsigned char *const rock_img[3][4][16] = {\n")
    for s in range(3):
        out.append("  {\n")
        for sh in range(4):
            out.append("    { %s },\n"
                       % ", ".join(names[(s, sh, rot)] for rot in range(16)))
        out.append("  },\n")
    out.append("};\n\n")

    # font
    for ch in "0123456789":
        out.append(pack1("glyph_%s" % ch, glyph_rows(ch), 5) + "\n")
    out.append(pack1("glyph_space", ["00000"] * 8, 5) + "\n")
    alpha = []
    for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        out.append(pack1("glyph_%s" % ch, glyph_rows(ch), 5) + "\n")
        alpha.append("glyph_%s" % ch)
    out.append("static const unsigned char *const digit_img[10] = { %s };\n\n"
               % ", ".join("glyph_%s" % d for d in "0123456789"))
    out.append("static const unsigned char *const alpha_img[26] = { %s };\n\n"
               % ", ".join(alpha))
    out.append("#endif /* VECTOROIDS_ASSETS_H */\n")

    text = "".join(out)
    path = sys.argv[1] if len(sys.argv) > 1 else "vectoroids_assets.h"
    with open(path, "w") as f:
        f.write(text)
    print("wrote %s (%d bytes)" % (path, len(text)))


if __name__ == "__main__":
    main()
