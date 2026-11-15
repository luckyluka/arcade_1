#!/usr/bin/env python3
"""PETAL BLADE — art generator for the ARCADE-1 round-4 samurai platformer.

Renders every piece of art procedurally (no PIL, stdlib only, fully
deterministic) as palette-index PNG strips, then writes:

    art/*.png          horizontal sprite strips (assetc's contract)
    assets.json        the arc1-assetc manifest (palettes, sprites, anims, maps)
    level1.txt         the ASCII level grid (tilemap + entity legend)
    level.h            C data the game needs that assetc does not emit
                       (sky gradient bands, collision kinds, entity spawns)

Compile the header with the documented pipeline:

    python3 tools/arc1_assetc.py --assets round4/assets.json --out-dir round4 --report

Options:
    --dir PATH    output directory (default: this script's directory)
    --preview P   write a contact sheet PNG of every sprite
    --sprite N    write only sprite N, upscaled 4x, and exit
    --ascii N     dump sprite N as text art to stdout and exit
"""
import argparse
import json
import math
import os
import struct
import sys
import zlib

# --------------------------------------------------------------------------
# RGB565 palette components. Everything is authored as (r5, g6, b5) — the
# device's native 5/6/5 — and expanded to #RRGGBB for the PNG, which assetc
# maps back to the same components exactly (expansion is injective).
# --------------------------------------------------------------------------


def comp(r5, g6, b5):
    if not (0 <= r5 <= 31 and 0 <= g6 <= 63 and 0 <= b5 <= 31):
        raise ValueError("palette component out of 5/6/5 range: %r" % ((r5, g6, b5),))
    return (r5, g6, b5)


def hx(c):
    """(r5,g6,b5) -> #RRGGBB the way the device expands it on scanout."""
    r5, g6, b5 = c
    return "#%02X%02X%02X" % (((r5 << 3) | (r5 >> 2)),
                              ((g6 << 2) | (g6 >> 4)),
                              ((b5 << 3) | (b5 >> 2)))


def rgb565(c):
    r5, g6, b5 = c
    return (r5 << 11) | (g6 << 5) | b5


def h2c(s):
    """'#RRGGBB' -> (r5,g6,b5), rounded down like a naive quantizer."""
    s = s.lstrip("#")
    r, g, b = int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)
    return (r >> 3, g >> 2, b >> 3)


def H(s):
    return h2c(s)


# --------------------------------------------------------------------------
# Palettes: name -> (bank, [index0 key, c1..c15]). Index 0 is the
# transparent key (#000000); the art never paints index 0.
# --------------------------------------------------------------------------

PALETTES = {
    # --- the samurai: indigo armour, crimson lacing, dusk-warm skin --------
    "hero": (0, [None,
                 H("#10101B"), H("#212543"), H("#313D73"), H("#495AA3"),
                 H("#B1393B"), H("#71252B"), H("#E1B18B"), H("#B17D63"),
                 H("#10152B"), H("#E1C15B"), H("#A17D33"), H("#21515B"),
                 H("#31797B"), H("#E9F5FB"), H("#89A9C3")]),
    # --- oni: crimson hide, bone horns, gold eyes, indigo cloth ------------
    "oni": (1, [None,
                H("#1A0E14"), H("#6B1F22"), H("#A33430"), H("#CE5B47"),
                H("#E88B63"), H("#E6D6AE"), H("#A89066"), H("#F5CE4A"),
                H("#201018"), H("#2C3E6E"), H("#46598F"), H("#3A2A22"),
                H("#9A8B7A"), H("#F2E6C8"), H("#C9A03A")]),
    # --- kuwagata beetle: lacquer-green shell -------------------------------
    "beetle": (2, [None,
                   H("#0C1410"), H("#1E3A22"), H("#2F6031"), H("#4E9440"),
                   H("#86C45E"), H("#5A4028"), H("#D8442E"), H("#F2A882"),
                   H("#14201A"), H("#26402C"), H("#E8F0E0"), H("#C8B040"),
                   H("#16301C"), H("#101C14"), H("#7A6A48")]),
    # --- terrain: dusk earth, moss, iron ------------------------------------
    "terrain": (3, [None,
                    H("#150F18"), H("#2E2028"), H("#46303A"), H("#5E4046"),
                    H("#24402C"), H("#35603A"), H("#4E8448"), H("#79A85A"),
                    H("#3A3440"), H("#555058"), H("#757078"), H("#8E8070"),
                    H("#6A6474"), H("#A9A2B4"), H("#A87A50")]),
    # --- the far range: cool haze with a warm sunset rim --------------------
    "ridge": (4, [None,
                  H("#12102C"), H("#1A1638"), H("#241F4A"), H("#2E2857"),
                  H("#393266"), H("#443C74"), H("#504784"), H("#5D5494"),
                  H("#6B61A4"), H("#7A70B2"), H("#8A80C0"), H("#9A90CC"),
                  H("#B0A8D0"), H("#E0A878"), H("#C98A66")]),
    # --- moon and clouds ----------------------------------------------------
    "sky": (5, [None,
                H("#F4E9C8"), H("#DCCFA8"), H("#BFAF8A"), H("#A89878"),
                H("#E8C0A0"), H("#D8A898"), H("#4E4460"), H("#6C5E7C"),
                H("#93849E"), H("#C0A8B0"), H("#E0B49C"), H("#C9907E"),
                H("#FFFFFF"), H("#201A30"), H("#7E6C8E")]),
    # --- mid hills: violet silhouettes, warm rim ----------------------------
    "mid": (6, [None,
                H("#101028"), H("#15122C"), H("#1C1836"), H("#231E40"),
                H("#2A244A"), H("#322B56"), H("#3A3362"), H("#443C70"),
                H("#4E4680"), H("#5A5190"), H("#6E4A50"), H("#8A5A54"),
                H("#A06A58"), H("#0C0C20"), H("#6660A0")]),
    # --- near treeline: the darkest silhouettes ------------------------------
    "near": (7, [None,
                 H("#0A0A18"), H("#0E0C1E"), H("#141224"), H("#1A1830"),
                 H("#201C3A"), H("#262244"), H("#2E2A4E"), H("#383258"),
                 H("#4A3438"), H("#704440"), H("#8E5A4A"), H("#12101F"),
                 H("#443C68"), H("#2A2448"), H("#5C5478")]),
    # --- FX and pickups: gold, petal pink, steel, hearts ---------------------
    "fx": (8, [None,
               H("#1A1424"), H("#8A6A24"), H("#C79A34"), H("#EFD06A"),
               H("#FFF3B0"), H("#8E3A5A"), H("#C46A88"), H("#EFA0B4"),
               H("#FFE0E8"), H("#EEF6FF"), H("#9FB6D0"), H("#C43A46"),
               H("#7E2430"), H("#FFFFFF"), H("#3A2E44")]),
    # --- the goal: vermilion torii, weathered stone --------------------------
    "goal": (9, [None,
                 H("#1A1018"), H("#8A2E20"), H("#B8412A"), H("#D6603A"),
                 H("#E88B58"), H("#241A22"), H("#D9B45C"), H("#3E3A44"),
                 H("#5C5660"), H("#807A84"), H("#A6A0AA"), H("#C8B078"),
                 H("#A8E0D8"), H("#F0EAE2"), H("#120C14")]),
}

# Convenience handles into the palettes (index into PALETTES[name][1]).
P = {name: idx_map for name, (bank, lst) in PALETTES.items()
     for idx_map in [{}]}
# fill name -> {color_number: comp} so art code reads HERO[3] etc.
for _n, (_b, _lst) in PALETTES.items():
    P[_n] = _lst


def C(name, i):
    """Palette colour i of palette `name` as (r5,g6,b5)."""
    return PALETTES[name][1][i]


# --------------------------------------------------------------------------
# Canvas: a grid of palette indices. 0 == transparent.
# --------------------------------------------------------------------------

BAYER4 = [[0, 8, 2, 10],
          [12, 4, 14, 6],
          [3, 11, 1, 9],
          [15, 7, 13, 5]]


def bayer(x, y):
    """Ordered-dither threshold in [0, 1)."""
    return (BAYER4[y & 3][x & 3] + 0.5) / 16.0


class G:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.p = [[0] * w for _ in range(h)]

    # --- primitives -------------------------------------------------------
    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = c

    def px(self, x, y, c):
        if c and 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = c

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.p[y][x]
        return 0

    def box(self, x0, y0, x1, y1, c):
        for y in range(max(0, y0), min(self.h - 1, y1) + 1):
            for x in range(max(0, x0), min(self.w - 1, x1) + 1):
                if c:
                    self.p[y][x] = c

    def rect(self, x, y, w, h, c):
        self.box(x, y, x + w - 1, y + h - 1, c)

    def hline(self, y, x0, x1, c):
        self.box(x0, y, x1, y, c)

    def vline(self, x, y0, y1, c):
        self.box(x, y0, x, y1, c)

    def line(self, x0, y0, x1, y1, c, t=1):
        n = max(abs(x1 - x0), abs(y1 - y0), 1)
        r = max(0, t // 2)
        for i in range(n + 1):
            x = round(x0 + (x1 - x0) * i / n)
            y = round(y0 + (y1 - y0) * i / n)
            if r:
                self.disc(x, y, r, c)
            else:
                self.px(x, y, c)

    def disc(self, cx, cy, r, c):
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + r:
                    self.px(x, y, c)

    # --- compositing ------------------------------------------------------
    def blit(self, other, dx, dy):
        for y in range(other.h):
            for x in range(other.w):
                c = other.p[y][x]
                if c:
                    self.px(dx + x, dy + y, c)

    def outline(self, c, only=0):
        """Paint `c` into transparent pixels touching a filled pixel."""
        todo = []
        for y in range(self.h):
            for x in range(self.w):
                if self.p[y][x] != only:
                    continue
                if (self.get(x - 1, y) or self.get(x + 1, y) or
                        self.get(x, y - 1) or self.get(x, y + 1)):
                    todo.append((x, y))
        for x, y in todo:
            self.p[y][x] = c

    def edge_light(self, c, dx=-1, dy=-1):
        """Highlight pixels whose neighbour in (dx,dy) is transparent."""
        todo = []
        for y in range(self.h):
            for x in range(self.w):
                if not self.p[y][x]:
                    continue
                if not self.get(x + dx, y + dy):
                    todo.append((x, y))
        for x, y in todo:
            self.p[y][x] = c

    # --- shading ----------------------------------------------------------
    def grad_v(self, x0, x1, y0, y1, stops, dither=True):
        """Vertical ramp through `stops` (palette indices) over y0..y1."""
        n = len(stops) - 1
        for y in range(max(0, y0), min(self.h - 1, y1) + 1):
            t = 0.0 if n == 0 else (y - y0) / max(1, (y1 - y0)) * n
            i = min(int(t), n - 1) if n else 0
            frac = 0.0 if n == 0 else t - i
            for x in range(max(0, x0), min(self.w - 1, x1) + 1):
                if not n:
                    c = stops[0]
                elif (frac > (bayer(x, y) if dither else 0.5)):
                    c = stops[i + 1]
                else:
                    c = stops[i]
                if c:
                    self.p[y][x] = c

    def hmirror(self):
        self.p = [list(reversed(row)) for row in self.p]

    def copy(self):
        g = G(self.w, self.h)
        g.p = [row[:] for row in self.p]
        return g


# --------------------------------------------------------------------------
# PNG output (8-bit RGB, no dependencies)
# --------------------------------------------------------------------------

def write_sheet(path, frames, pal_name, scale=3, bg=(16, 16, 28)):
    """Contact sheet: every frame of one sprite side by side, upscaled."""
    w, h = frames[0].w, frames[0].h
    cell_w, cell_h = w * scale, h * scale
    W, H_ = cell_w * len(frames), cell_h
    def pix(v):
        c = PALETTES[pal_name][1][v]
        if c is None:
            return None
        return (((c[0] << 3) | (c[0] >> 2)),
                ((c[1] << 2) | (c[1] >> 4)),
                ((c[2] << 3) | (c[2] >> 2)))
    raw = bytearray()
    for y in range(H_):
        raw.append(0)
        fi = y * 0
        for x in range(W):
            fi = x // cell_w
            lx = (x - fi * cell_w) // scale
            ly = y // scale
            col = pix(frames[fi].p[ly][lx])
            raw += bytes(col if col else bg)
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + \
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", W, H_, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def write_strip(path, frames, pal_name, w, h):
    """Write a horizontal strip: FRAMES frames of w x h, each pixel authored
    in its palette's expanded 8-bit colour (key #000000 for transparent)."""
    cols = PALETTES[pal_name][1]
    exp = [None if c is None else
           (((c[0] << 3) | (c[0] >> 2)),
            ((c[1] << 2) | (c[1] >> 4)),
            ((c[2] << 3) | (c[2] >> 2))) for c in cols]
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for g in frames:
            for x in range(w):
                v = g.p[y][x]
                raw += bytes(exp[v] if exp[v] else (0, 0, 0))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + \
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w * len(frames), h,
                                      8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def ascii_art(g):
    ch = ".123456789abcdef"
    return "\n".join("".join(ch[v] if v < 16 else "?" for v in row)
                     for row in g.p)


# ==========================================================================
# ART. Every sprite is a function returning its frames (palette-index grids).
# ==========================================================================

# Palette indices, spelled out so the drawing code reads like a sketch.
(O, AD, AM, AL, RED, REDD, SK, SKS, HR, GD, GDD, RB, RBL, ST, STM) = range(1, 16)

HERO_W, HERO_H = 28, 40


def h_head(g, oy, turn=0):
    """Kabuto + face. (oy = bob of the whole upper body, turn = head x shift)"""
    t = turn
    # hair mass behind the helmet
    g.box(9 + t, 8 + oy, 18 + t, 17 + oy, HR)
    # face
    g.box(11 + t, 11 + oy, 18 + t, 17 + oy, SK)
    g.vline(18 + t, 12 + oy, 17 + oy, SKS)
    g.box(11 + t, 17 + oy, 18 + t, 17 + oy, SKS)
    g.box(12 + t, 15 + oy, 14 + t, 15 + oy, SKS)        # cheek shade
    # brow + eye (faces right)
    g.box(14 + t, 12 + oy, 17 + t, 12 + oy, AD)
    g.box(15 + t, 13 + oy, 17 + t, 14 + oy, SKS)
    g.px(16 + t, 13 + oy, O)
    g.px(17 + t, 14 + oy, O)
    g.px(16 + t, 16 + oy, SKS)                          # mouth
    # helmet dome
    g.box(10 + t, 6 + oy, 18 + t, 11 + oy, AM)
    g.box(11 + t, 5 + oy, 17 + t, 6 + oy, AM)
    g.box(11 + t, 6 + oy, 16 + t, 7 + oy, AL)
    g.hline(11 + oy, 10 + t, 18 + t, AD)
    # mabizashi (front brim)
    g.box(16 + t, 10 + oy, 20 + t, 11 + oy, AD)
    # shikoro (neck guard, flared to the back)
    g.box(7 + t, 10 + oy, 12 + t, 14 + oy, AD)
    g.box(6 + t, 11 + oy, 12 + t, 15 + oy, AD)
    g.hline(12 + oy, 7 + t, 12 + t, AM)
    g.hline(14 + oy, 6 + t, 12 + t, AM)
    # maedate: slim gold crescent
    g.line(12 + t, 5 + oy, 14 + t, 1 + oy, GD, 1)
    g.px(14 + t, 3 + oy, GDD)
    g.line(15 + t, 1 + oy, 17 + t, 5 + oy, GD, 1)
    g.px(15 + t, 3 + oy, GDD)
    g.px(14 + t, 5 + oy, GDD)
    g.px(15 + t, 5 + oy, GDD)


def h_torso(g, oy, lean=0):
    """Do (armour) + sode + obi. lean shifts the chest forward (x)."""
    x = lean
    # far-side sode (behind the body)
    g.box(6 + x, 17 + oy, 9 + x, 23 + oy, AD)
    g.vline(6 + x, 18 + oy, 22 + oy, AM)
    # cuirass
    g.box(9 + x, 17 + oy, 19 + x, 27 + oy, AM)
    g.vline(9 + x, 17 + oy, 27 + oy, AL)
    g.vline(19 + x, 18 + oy, 27 + oy, AD)
    # odoshi lacing rows (crimson)
    for ly in (19 + oy, 22 + oy, 25 + oy):
        g.hline(ly, 10 + x, 18 + x, RED)
    g.hline(20 + oy, 10 + x, 18 + x, AD)
    g.hline(23 + oy, 10 + x, 18 + x, AD)
    g.hline(26 + oy, 10 + x, 18 + x, REDD)
    # near-side sode (in front)
    g.box(18 + x, 17 + oy, 22 + x, 23 + oy, AM)
    g.box(19 + x, 17 + oy, 21 + x, 18 + oy, AL)
    g.vline(22 + x, 18 + oy, 23 + oy, AD)
    for ly in (19 + oy, 21 + oy):
        g.hline(ly, 18 + x, 22 + x, RED)
    # obi sash
    g.box(9 + x, 27 + oy, 20 + x, 29 + oy, REDD)
    g.hline(27 + oy, 9 + x, 20 + x, RED)
    g.px(15 + x, 28 + oy, GD)


def h_arm(g, sx, sy, ex, ey, hx, hy, back=False):
    g.line(sx, sy, ex, ey, AD, 3)
    g.line(ex, ey, hx, hy, AD if back else AM, 3)
    g.disc(hx, hy, 1, SKS if back else SK)


def h_leg(g, hipx, hipy, knee, foot, dark=False):
    """One leg: hip -> knee -> foot, hakama cloth + tabi foot."""
    cloth = RB if dark else RBL
    fx, fy = foot
    g.line(hipx, hipy, knee[0], knee[1], cloth, 5)
    g.line(knee[0], knee[1], fx, fy - 2, cloth, 4)
    if not dark:
        g.line(hipx - 1, hipy, knee[0] - 1, knee[1], RB, 2)
    g.box(fx - 3, fy - 2, fx + 2, fy, AD)
    g.hline(fy, fx - 3, fx + 2, O)


def h_sheath(g):
    """Katana in its saya at the far hip: tip back-left, hilt forward."""
    g.line(19, 30, 6, 36, AD, 2)
    g.line(18, 31, 7, 36, AM, 1)
    g.px(6, 35, O)
    g.px(7, 37, O)
    g.box(18, 29, 19, 31, GD)                    # tsuba
    g.line(20, 31, 22, 33, AD, 2)                # tsuka (dark wrap)
    g.px(22, 33, GD)                             # kashira
    g.px(21, 32, RED)


def h_head_fallen(g):
    """Bespoke head for the death pose: helmet knocked askew, eyes shut."""
    g.box(3, 26, 10, 33, HR)                     # hair spilled on the dirt
    g.box(4, 27, 10, 32, SK)                     # face, turned down-left
    g.hline(32, 4, 10, SKS)
    g.vline(4, 27, 32, SKS)
    g.hline(29, 5, 8, AD)                        # closed eye
    g.px(9, 31, SKS)                             # slack mouth
    g.box(3, 23, 11, 26, AM)                     # kabuto, knocked askew
    g.box(4, 22, 10, 23, AM)
    g.box(4, 22, 8, 23, AL)
    g.hline(26, 3, 11, AD)
    g.line(11, 23, 13, 20, GD, 1)                # broken maedate
    g.px(14, 21, GDD)


def h_blade(g, pts, wide=1):
    """Sword: a bright steel edge along pts with a darker spine."""
    for i in range(len(pts) - 1):
        g.line(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], ST, wide)
    for i in range(len(pts) - 1):
        g.line(pts[i][0], pts[i][1] + 1, pts[i + 1][0], pts[i + 1][1] + 1,
               STM, 1)


def _pose(parts, rim=True):
    """Assemble one hero frame from layer-ordered (fn, args) steps."""
    g = G(HERO_W, HERO_H)
    for fn, args in parts:
        fn(g, *args)
    g.outline(O)
    if rim:
        # warm sunset rim on the back (left) and top silhouette
        lit = []
        for y in range(g.h):
            for x in range(g.w):
                if not g.p[y][x]:
                    continue
                if g.get(x - 1, y) == 0 or g.get(x, y - 1) == 0:
                    lit.append((x, y))
        for x, y in lit:
            g.p[y][x] = GDD
        # a few brighter glints on the upper-back edge
        glint = []
        for y in range(g.h):
            for x in range(g.w):
                if g.p[y][x] == GDD and g.get(x - 1, y) == 0 and y < 26:
                    glint.append((x, y))
        for x, y in glint:
            g.p[y][x] = GD
    return g


def hero_frames():
    """11 frames: idle, run x3, jump, fall, slash x3, hurt, dead."""
    F = {}

    # ---- idle: feet planted, hand resting on the hilt --------------------
    F["idle"] = _pose([
        (lambda g: h_arm(g, 8, 19, 7, 24, 9, 27, back=True), ()),
        (lambda g: h_leg(g, 12, 29, (11, 34), (11, 39), True), ()),
        (h_sheath, ()),
        (lambda g: h_torso(g, 0), ()),
        (lambda g: h_leg(g, 15, 29, (17, 34), (17, 39), False), ()),
        (lambda g: h_arm(g, 19, 19, 21, 24, 21, 29), ()),
        (lambda g: h_head(g, 0), ()),
    ])

    # ---- run cycle: contact / passing / contact (other leg) -------------
    runs = [
        dict(bob=0, ff=(22, 39), bf=(7, 39), fk=(20, 33), bk=(8, 33),
             ha=(24, 29), hb=(5, 27)),
        dict(bob=1, ff=(16, 36), bf=(11, 39), fk=(18, 34), bk=(11, 34),
             ha=(21, 31), hb=(8, 29)),
        dict(bob=0, ff=(7, 39), bf=(21, 39), fk=(9, 33), bk=(20, 33),
             ha=(5, 27), hb=(24, 29)),
    ]
    for i, r in enumerate(runs):
        F["run%d" % i] = _pose([
            (lambda g, r=r: h_arm(g, 8, 19 + r["bob"], r["hb"][0] + 2,
                                  r["hb"][1] - 4, r["hb"][0], r["hb"][1],
                                  back=True), ()),
            (lambda g, r=r: h_leg(g, 12, 29 + r["bob"], r["bk"], r["bf"],
                                  True), ()),
            (h_sheath, ()),
            (lambda g, r=r: h_torso(g, r["bob"]), ()),
            (lambda g, r=r: h_leg(g, 15, 29 + r["bob"], r["fk"], r["ff"],
                                  False), ()),
            (lambda g, r=r: h_arm(g, 20, 19 + r["bob"], r["ha"][0] - 2,
                                  r["ha"][1] - 5, r["ha"][0], r["ha"][1]), ()),
            (lambda g, r=r: h_head(g, r["bob"]), ()),
        ])

    # ---- jump: knees tucked, robe flared --------------------------------
    F["jump"] = _pose([
        (lambda g: h_arm(g, 8, 18, 6, 23, 7, 28, back=True), ()),
        (lambda g: h_leg(g, 12, 29, (10, 33), (14, 36), True), ()),
        (h_sheath, ()),
        (lambda g: h_torso(g, -1), ()),
        (lambda g: h_leg(g, 15, 29, (21, 32), (17, 35), False), ()),
        (lambda g: h_arm(g, 20, 18, 23, 22, 24, 27), ()),
        (lambda g: h_head(g, -1), ()),
    ])

    # ---- falling: legs reaching down, robe lifting ----------------------
    F["fall"] = _pose([
        (lambda g: h_arm(g, 8, 19, 5, 21, 5, 26, back=True), ()),
        (lambda g: h_leg(g, 12, 29, (11, 34), (12, 38), True), ()),
        (h_sheath, ()),
        (lambda g: h_torso(g, 0), ()),
        (lambda g: h_leg(g, 15, 29, (19, 34), (20, 37), False), ()),
        (lambda g: h_arm(g, 20, 19, 24, 20, 26, 24), ()),
        (lambda g: h_head(g, 0), ()),
    ])

    # ---- slash 0: windup, blade up and back -----------------------------
    F["slash0"] = _pose([
        (lambda g: h_arm(g, 8, 19, 14, 15, 18, 12, back=True), ()),
        (lambda g: h_leg(g, 12, 29, (11, 34), (10, 39), True), ()),
        (lambda g: h_torso(g, 0, lean=-1), ()),
        (lambda g: h_leg(g, 15, 29, (18, 34), (18, 39), False), ()),
        (lambda g: h_arm(g, 19, 19, 22, 15, 21, 11), ()),
        (lambda g: h_head(g, 0, turn=-1), ()),
        (lambda g: (h_blade(g, [(21, 10), (17, 4), (15, 0)], 1),
                    g.box(21, 11, 22, 13, GD),
                    g.line(22, 14, 24, 16, RED, 2), g.px(24, 17, AD)), ()),
    ])

    # ---- slash 1: the cut, blade sweeping forward -----------------------
    F["slash1"] = _pose([
        (lambda g: h_arm(g, 9, 20, 17, 22, 23, 24, back=True), ()),
        (lambda g: h_leg(g, 12, 30, (13, 35), (12, 39), True), ()),
        (lambda g: h_torso(g, 1, lean=2), ()),
        (lambda g: h_leg(g, 16, 30, (19, 35), (20, 39), False), ()),
        (lambda g: h_arm(g, 21, 20, 24, 22, 26, 23), ()),
        (lambda g: h_head(g, 1, turn=2), ()),
        (lambda g: (h_blade(g, [(23, 27), (27, 21), (27, 12)], 1),
                    g.box(22, 26, 23, 28, GD),
                    g.line(20, 29, 22, 27, RED, 2), g.px(20, 30, AD)), ()),
    ])

    # ---- slash 2: follow-through, blade low forward ---------------------
    F["slash2"] = _pose([
        (lambda g: h_arm(g, 9, 20, 18, 27, 23, 31, back=True), ()),
        (lambda g: h_leg(g, 12, 29, (11, 34), (11, 39), True), ()),
        (lambda g: h_torso(g, 0, lean=1), ()),
        (lambda g: h_leg(g, 16, 29, (19, 34), (19, 39), False), ()),
        (lambda g: h_arm(g, 20, 20, 24, 27, 25, 32), ()),
        (lambda g: h_head(g, 0, turn=1), ()),
        (lambda g: (h_blade(g, [(23, 31), (27, 37)], 1),
                    g.box(22, 30, 23, 32, GD),
                    g.line(20, 28, 22, 30, RED, 2), g.px(20, 29, AD)), ()),
    ])

    # ---- hurt: knocked back, shoulder up, head snapped back -------------
    F["hurt"] = _pose([
        (lambda g: h_arm(g, 6, 19, 3, 23, 3, 27, back=True), ()),
        (lambda g: h_leg(g, 12, 29, (9, 34), (7, 39), True), ()),
        (h_sheath, ()),
        (lambda g: h_torso(g, 0, lean=-3), ()),
        (lambda g: h_leg(g, 15, 29, (18, 34), (19, 39), False), ()),
        (lambda g: h_arm(g, 16, 19, 19, 16, 21, 12), ()),
        (lambda g: h_head(g, -1, turn=-3), ()),
    ])

    # ---- dead: pitched onto the dirt, sword dropped in front ------------
    F["dead"] = _pose([
        (lambda g: h_arm(g, 16, 27, 10, 32, 3, 35, back=True), ()),  # far arm
        (lambda g: (g.box(16, 32, 27, 36, RBL), g.box(18, 37, 26, 38, RB),
                    g.hline(32, 16, 27, RB), g.hline(38, 18, 26, RB),
                    g.box(24, 34, 27, 37, AD), g.hline(37, 24, 27, O),
                    g.px(25, 35, AM)), ()),                            # hakama
        (lambda g: h_torso(g, 6, lean=4), ()),                         # pitched
        (lambda g: h_arm(g, 24, 28, 26, 32, 27, 35), ()),              # near arm
        (lambda g: h_head_fallen(g), ()),                              # bowed
        (lambda g: (g.line(1, 36, 14, 37, ST, 1), g.line(1, 37, 14, 38, STM, 1),
                    g.box(15, 36, 16, 38, GD),
                    g.line(17, 37, 19, 38, AD, 2), g.px(20, 38, GD)), ()),
    ])

    return F


# ==========================================================================
# Sprite registry: name -> dict(pal, frames[...], origin, collision, anims)
# ==========================================================================

HERO_ORDER = ["idle", "run0", "run1", "run2", "jump", "fall",
              "slash0", "slash1", "slash2", "hurt", "dead"]

SPRITES = {}


def register_sprites():
    SPRITES["hero"] = dict(
        pal="hero", frames=[hero_frames()[n] for n in HERO_ORDER],
        origin=[14, 40], collision=[8, 12, 12, 28],
        anims=[("run", [1, 2, 3], 90), ("slash", [6, 7, 8], 45)])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dir", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--preview", metavar="PATH")
    ap.add_argument("--sprite", metavar="NAME")
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--ascii", metavar="NAME")
    a = ap.parse_args()

    register_sprites()

    if a.ascii:
        g = SPRITES[a.ascii]["frames"][0]
        print(ascii_art(g))
        return 0
    if a.sprite:
        s = SPRITES[a.sprite]
        write_sheet(os.path.join(a.dir, "preview_%s.png" % a.sprite),
                    s["frames"], s["pal"], scale=a.scale)
        print("preview: preview_%s.png (%d frames %dx%d)" %
              (a.sprite, len(s["frames"]), s["frames"][0].w, s["frames"][0].h))
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())


