#!/usr/bin/env python3
"""Generate the walkdemo sprite sheet: a 40x56 4-bit character, 1 idle + 6 walk frames.

This is the "premade spritesheet" half of the walkdemo experiment
(docs/experiments/walkdemo.md). The machine has no asset pipeline: art reaches
the ROM only as data compiled from source. This script is the master artwork —
it renders the sheet parametrically (no PIL, no image files, deterministic) and
emits a C header the guest includes. The committed header is the source of
truth for builds; regenerate with:

    python3 tools/gen_walk_sheet.py

Options:
    --out PATH     header to write (default programs/experimental/walkdemo_sheet.h)
    --preview PATH also write a PPM contact sheet (all frames side by side)
    --ascii N      dump frame N as text art to stdout and exit
"""
import argparse
import sys

W, H = 40, 56          # frame size in pixels (even width: 4-bit, 2 px/byte)
N_WALK = 6             # walk-cycle frames
FRAMES = 1 + N_WALK    # frame 0 = idle, 1..6 = the cycle
BANK = 3               # palette bank the guest uploads from (entries 48..63)

# 16-entry palette as direct RGB565 components (r,b: 0..31; g: 0..63).
# Index 0 is transparent. Anything out of range is a bug, not a rounding
# decision — rgb565() rejects it (this list once had r=40 for hair, which
# `& 31` would have silently turned into dark green in the ROM palette).
PAL = [
    None,            # 0  transparent
    (2, 2, 4),       # 1  outline
    (22, 14, 6),     # 2  hair
    (12, 8, 3),      # 3  hair shade
    (28, 44, 18),    # 4  skin
    (20, 32, 12),    # 5  skin shade
    (31, 63, 31),    # 6  eye white
    (1, 1, 6),       # 7  pupil
    (5, 36, 31),     # 8  shirt
    (3, 22, 20),     # 9  shirt shade
    (12, 58, 31),    # 10 shirt highlight
    (31, 52, 4),     # 11 belt
    (14, 16, 24),    # 12 trousers
    (8, 9, 16),      # 13 trousers shade
    (22, 44, 24),    # 14 shoe
    (2, 5, 8),       # 15 shadow
]

ASCII_CH = ".#Hh@%o*BbCYTtS-"


class Canvas:
    def __init__(self):
        self.p = [[0] * W for _ in range(H)]

    def px(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.p[y][x] = c

    def box(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.px(x, y, c)

    def limb(self, x0, y0, x1, y1, w, c):
        """A thick near-vertical limb from (x0,y0) to (x1,y1), width w."""
        n = max(1, abs(y1 - y0))
        for i in range(n + 1):
            t = i / n
            x = int(round(x0 + (x1 - x0) * t))
            y = int(round(y0 + (y1 - y0) * t))
            for k in range(w):
                self.px(x + k, y, c)


# Walk cycle: (front leg dx, back leg dx, front lift, back lift, body bob).
# Side view facing right: the "front" limbs are the near-side (right) ones.
# A lift raises the foot by that many rows (the passing pose).
CYCLE = [
    (+4, -4, 0, 0, 0),   # 1 contact: front leg forward
    (+3, -3, 0, 0, 1),   # 2 down:   weight low
    (0, 0, 0, 2, 0),     # 3 pass:   back foot lifted
    (-4, +4, 0, 0, 0),   # 4 contact: back leg forward
    (-3, +3, 2, 0, 1),   # 5 down:   front foot lifted, weight low
    (0, 0, 2, 0, 0),     # 6 pass:   front foot lifted
]


def draw_frame(step):
    """step 0 = idle, 1..6 = CYCLE[step-1]."""
    fdx, bdx, flift, blift, bob = (0, 0, 0, 0, 0) if step == 0 else CYCLE[step - 1]
    c = Canvas()

    # --- ground shadow: widens a little with the stride -------------------
    c.box(11 + min(fdx, 0), 54, 28 + max(fdx, 0), 55, 15)

    # --- back (far-side) arm and leg, drawn first --------------------------
    adx = -int(0.75 * fdx) if step else 0
    c.limb(15, 21, 13 + adx, 33, 3, 9)                  # back arm
    c.box(12 + adx, 33, 14 + adx, 34, 5)                # back hand (skin)
    c.limb(15, 37, 15 + bdx, 51 - blift, 4, 13)         # back leg
    c.box(14 + bdx, 51 - blift, 18 + bdx, 52 - blift, 14)  # back shoe
    c.box(13 + bdx, 53 - blift, 18 + bdx, 53 - blift, 1)   # sole outline

    # --- torso --------------------------------------------------------------
    c.box(12, 19 + bob, 27, 37 + bob, 1)                # outline
    c.box(13, 20 + bob, 26, 35 + bob, 8)                # shirt
    c.box(13, 20 + bob, 13, 35 + bob, 10)               # highlight column
    c.box(26, 20 + bob, 26, 35 + bob, 9)                # shade column
    c.box(13, 34 + bob, 26, 35 + bob, 11)               # belt

    # --- head: side view facing right (bobs with the torso) ----------------
    c.box(13, 4 + bob, 26, 18 + bob, 1)                 # head outline
    c.box(14, 5 + bob, 25, 8 + bob, 2)                  # hair cap
    c.box(14, 9 + bob, 25, 17 + bob, 4)                 # face
    c.box(14, 9 + bob, 16, 13 + bob, 3)                 # hair at the back
    c.box(17, 15 + bob, 24, 17 + bob, 4)                # jaw
    c.box(21, 10 + bob, 24, 10 + bob, 1)                # brow
    c.box(22, 11 + bob, 23, 12 + bob, 6)                # eye white
    c.px(23, 11 + bob, 7); c.px(23, 12 + bob, 7)        # pupil (looks right)
    c.box(23, 15 + bob, 24, 15 + bob, 1)                # mouth
    c.box(18, 18 + bob, 21, 19 + bob, 5)                # neck

    # --- front (near-side) arm and leg, drawn last --------------------------
    adx = -int(0.75 * fdx) if step else 0
    c.limb(24, 21, 27 + adx, 33, 3, 8)                  # front arm
    c.box(27 + adx, 33, 29 + adx, 34, 4)                # front hand
    c.limb(20, 37, 20 + fdx, 51 - flift, 4, 12)         # front leg
    c.box(19 + fdx, 51 - flift, 23 + fdx, 52 - flift, 14)  # front shoe
    c.box(19 + fdx, 53 - flift, 24 + fdx, 53 - flift, 1)   # sole outline

    return c.p


def pack4(grid):
    """4-bit pack, 2 px per byte, high nibble = left pixel."""
    out = bytearray()
    for y in range(H):
        for x in range(0, W, 2):
            out.append(((grid[y][x] & 0xF) << 4) | (grid[y][x + 1] & 0xF))
    return bytes(out)


def rgb565(comp):
    r, g, b = comp
    if not (0 <= r <= 31 and 0 <= g <= 63 and 0 <= b <= 31):
        raise ValueError("palette component out of 5/6/5 range: %r" % (comp,))
    return (r << 11) | (g << 5) | b


def emit_header(path):
    stride = (W // 2) * H
    sheet = b"".join(pack4(draw_frame(s)) for s in range(FRAMES))
    with open(path, "w") as f:
        f.write(
            "/* Generated by tools/gen_walk_sheet.py — do not edit by hand.\n"
            " * 40x56 4-bit character, frame 0 = idle, 1..6 = walk cycle,\n"
            " * %d bytes per frame, %d-byte stride (2 px/byte, high nibble first).\n"
            " * Palette: %d entries, guest uploads them to bank %d (entries %d..%d). */\n"
            "#ifndef WALKDEMO_SHEET_H\n#define WALKDEMO_SHEET_H\n\n"
            "#define WALK_FRAME_W %d\n#define WALK_FRAME_H %d\n"
            "#define WALK_STRIDE %d\n#define WALK_FRAMES %d\n#define WALK_BANK %d\n\n"
            "static const unsigned short walk_pal[16] = {\n"
            % (stride, len(sheet), 16, BANK, BANK * 16, BANK * 16 + 15,
               W, H, stride, FRAMES, BANK)
        )
        for i, comp in enumerate(PAL):
            f.write("    0x%04X,%s\n" % (rgb565(comp) if comp else 0,
                                         "  /* %d */" % i if i else "  /* transparent */"))
        f.write("};\n\nstatic const unsigned char walk_sheet[%d] = {\n" % len(sheet))
        for off in range(0, len(sheet), 16):
            f.write("    " + " ".join("0x%02X," % b for b in sheet[off:off + 16]) + "\n")
        f.write("};\n\n#endif /* WALKDEMO_SHEET_H */\n")
    return len(sheet)


def write_preview(path):
    scale = 2
    pad = 4
    cols = FRAMES
    cw, ch = W * scale + pad, H * scale + pad
    img = bytearray(b"\x20\x20\x40" * ((cols * cw + pad) * (ch + pad)))
    for s in range(FRAMES):
        grid = draw_frame(s)
        for y in range(H):
            for x in range(W):
                comp = PAL[grid[y][x]] or (8, 10, 20)
                r, g, b = (comp[0] * 255 // 31, comp[1] * 255 // 63, comp[2] * 255 // 31)
                for dy in range(scale):
                    for dx in range(scale):
                        px = pad + s * cw + x * scale + dx
                        py = pad + y * scale + dy
                        o = (py * (cols * cw + pad) + px) * 3
                        img[o:o + 3] = bytes((r, g, b))
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (cols * cw + pad, ch + pad))
        f.write(bytes(img))


def ascii_frame(step):
    grid = draw_frame(step)
    return "\n".join("".join(ASCII_CH[v] for v in row) for row in grid)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="programs/experimental/walkdemo_sheet.h")
    ap.add_argument("--preview", metavar="PATH")
    ap.add_argument("--ascii", type=int, metavar="N", choices=range(FRAMES))
    a = ap.parse_args()

    if a.ascii is not None:
        print(ascii_frame(a.ascii))
        return 0

    n = emit_header(a.out)
    print("gen_walk_sheet: %s (%d frames, %d bytes)" % (a.out, FRAMES, n))
    if a.preview:
        write_preview(a.preview)
        print("gen_walk_sheet: preview %s" % a.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
