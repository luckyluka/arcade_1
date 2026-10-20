#!/usr/bin/env python3
"""PETAL BLADE — art generator for the ARCADE-1 round-5 samurai platformer.

Renders every piece of art procedurally (no PIL, stdlib only, fully
deterministic) as palette-index PNG strips, then writes:

    art/*.png          horizontal sprite strips (assetc's contract)
    assets.json        the arc1-assetc manifest (palettes, sprites, anims, maps)
    level1.txt         the ASCII level grid (tilemap + entity legend)
    level.h            C data the game needs that assetc does not emit
                        (sky gradient bands, collision kinds, entity spawns)

Compile the header with the documented pipeline:

    python3 tools/arc1_assetc.py --assets round5/assets.json --out-dir round5 --report

Options:
    --dir PATH    output directory (default: this script's directory)
    --preview P   write a contact sheet PNG of every sprite
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
                H("#0E0E24"), H("#161632"), H("#1E1A3C"), H("#262246"),
                H("#2E2A50"), H("#36325A"), H("#3E3A64"), H("#46426E"),
                H("#4E4A78"), H("#565282"), H("#6E4A50"), H("#8A5A54"),
                H("#A06A58"), H("#080818"), H("#6660A0")]),
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
# ONI — the crimson demon. Horns, gold eyes, fangs, tiger-skin trousers and a
# spiked kanabo. Frames: walk x2, attack x2. 28x36.
# ==========================================================================

ONI_W, ONI_H = 28, 36
(OD, OM, OL, ORED, OREDL, OBONE, OBONED, OEYE, OIND, OINDL, OOUT, OSKIN,
 OBLIGHT, OFANG, OREDD) = range(1, 16)


def o_horns(g, ox, oy, big=True):
    """Two curved bone horns above the brow."""
    r = 2 if big else 1
    g.line(8 + ox, 8 + oy, 4 + ox, 2 + oy, OBONE, r)
    g.line(12 + ox, 8 + oy, 16 + ox, 2 + oy, OBONE, r)
    g.px(4 + ox, 1 + oy, OBONED)
    g.px(16 + ox, 1 + oy, OBONED)


def o_head(g, oy, mouth=0):
    # crimson skull
    g.box(8, 8 + oy, 20, 17 + oy, ORED)
    g.box(8, 8 + oy, 20, 9 + oy, OREDL)         # crown highlight
    g.box(8, 15 + oy, 20, 17 + oy, OREDD)       # jaw shade
    # brow ridge
    g.box(8, 10 + oy, 20, 11 + oy, OOUT)
    # gold eyes (angry slant)
    g.box(11, 12 + oy, 13, 13 + oy, OEYE)
    g.box(16, 12 + oy, 18, 13 + oy, OEYE)
    g.px(12, 12 + oy, OOUT)
    g.px(17, 12 + oy, OOUT)
    # snarl / fangs
    if mouth:
        g.box(10, 15 + oy, 18, 17 + oy, OOUT)   # open mouth
        g.box(11, 16 + oy, 12, 17 + oy, OFANG)
        g.box(15, 16 + oy, 16, 17 + oy, OFANG)
    else:
        g.hline(16 + oy, 10, 18, OOUT)
        g.px(12, 16 + oy, OFANG)
        g.px(16, 16 + oy, OFANG)
    o_horns(g, 0, oy)


def o_torso(g, oy):
    # broad crimson body
    g.box(6, 18 + oy, 22, 30 + oy, ORED)
    g.box(6, 18 + oy, 22, 19 + oy, OREDL)
    g.box(6, 28 + oy, 22, 30 + oy, OREDD)
    # pectoral / belly shading
    g.hline(23 + oy, 7, 21, OREDD)
    g.hline(26 + oy, 8, 20, OREDD)
    # tiger-skin trousers
    g.box(6, 30 + oy, 22, 34 + oy, OOUT)
    g.hline(31 + oy, 6, 22, OREDL)
    g.px(9, 32 + oy, OREDL)
    g.px(15, 33 + oy, OREDL)
    g.px(19, 32 + oy, OREDL)


def o_leg(g, hipx, foot, oy, dark=False):
    fx, fy = foot
    c = OOUT if dark else OIND
    g.line(hipx, 31 + oy, hipx, fy - 2, c, 3)
    g.box(fx - 2, fy - 2, fx + 3, fy, OREDL)    # clawed foot
    g.px(fx + 3, fy - 1, OBONE)
    g.px(fx + 4, fy - 1, OBONE)


def o_club(g, x0, y0, x1, y1):
    """A spiked kanabo: a tapering club with bone studs."""
    g.line(x0, y0, x1, y1, OOUT, 4)
    g.line(x0, y0 + 1, x1, y1 + 1, OIND, 3)
    for t in (0.3, 0.55, 0.8):
        x = round(x0 + (x1 - x0) * t)
        y = round(y0 + (y1 - y0) * t)
        g.disc(x, y, 1, OBONE)


def o_arm(g, sx, sy, ex, ey, hx, hy):
    g.line(sx, sy, ex, ey, OOUT, 3)
    g.line(ex, ey, hx, hy, ORED, 3)
    g.disc(hx, hy, 2, OREDL)
    g.px(hx + 1, hy, OBONE)
    g.px(hx - 1, hy, OBONE)


def _oni_pose(parts):
    g = G(ONI_W, ONI_H)
    for fn, args in parts:
        fn(g, *args)
    g.outline(OOUT)
    return g


def oni_frames():
    F = {}
    # walk 0: left foot forward
    F["walk0"] = _oni_pose([
        (lambda g: o_arm(g, 7, 20, 4, 24, 3, 27), ()),
        (lambda g: o_leg(g, 11, (9, 35), 0, True), ()),
        (lambda g: o_torso(g, 0), ()),
        (lambda g: o_leg(g, 16, (18, 35), 0, False), ()),
        (lambda g: o_arm(g, 20, 20, 23, 24, 25, 27), ()),
        (lambda g: o_head(g, 0, mouth=0), ()),
    ])
    # walk 1: right foot forward
    F["walk1"] = _oni_pose([
        (lambda g: o_arm(g, 7, 20, 5, 25, 4, 28), ()),
        (lambda g: o_leg(g, 11, (17, 35), 0, True), ()),
        (lambda g: o_torso(g, 1), ()),
        (lambda g: o_leg(g, 16, (10, 35), 0, False), ()),
        (lambda g: o_arm(g, 20, 20, 24, 23, 26, 26), ()),
        (lambda g: o_head(g, 1, mouth=0), ()),
    ])
    # attack 0: windup, club raised
    F["atk0"] = _oni_pose([
        (lambda g: o_arm(g, 7, 20, 6, 16, 5, 12), ()),
        (lambda g: o_leg(g, 11, (8, 35), 0, True), ()),
        (lambda g: o_torso(g, 0), ()),
        (lambda g: o_leg(g, 16, (18, 35), 0, False), ()),
        (lambda g: o_arm(g, 20, 20, 22, 14, 24, 10), ()),
        (lambda g: o_head(g, 0, mouth=1), ()),
        (lambda g: o_club(g, 24, 12, 30, 2), ()),
    ])
    # attack 1: the swing, club forward
    F["atk1"] = _oni_pose([
        (lambda g: o_arm(g, 7, 21, 5, 25, 4, 28), ()),
        (lambda g: o_leg(g, 11, (9, 35), 0, True), ()),
        (lambda g: o_torso(g, 0), ()),
        (lambda g: o_leg(g, 16, (18, 35), 0, False), ()),
        (lambda g: o_arm(g, 20, 20, 24, 24, 27, 26), ()),
        (lambda g: o_head(g, 0, mouth=1), ()),
        (lambda g: o_club(g, 26, 28, 32, 20), ()),
    ])
    return F


# ==========================================================================
# KUWAGATA BEETLE — a lacquer-green stag beetle. Frames: crawl x2. 20x12.
# ==========================================================================

BE_W, BE_H = 20, 12
(BD, BDD, BS, BSL, BG, BGD, BBR, BRE, BSL2, BPALE, BTAN, BD2, BD3, BD4, BRNG) = range(1, 16)


def beetle_frames():
    F = {}
    for f in (0, 1):
        g = G(BE_W, BE_H)
        # shell body
        g.box(6, 2, 16, 9, BS)
        g.box(6, 2, 16, 3, BSL)                  # shell highlight
        g.box(6, 8, 16, 9, BDD)                  # shell shade
        # elytra split
        g.vline(11, 3, 8, BDD)
        g.vline(11, 3, 8, BSL)
        # head
        g.box(15, 3, 18, 7, BGD)
        g.box(15, 3, 18, 4, BSL)
        # mandibles / antennae
        g.line(18, 4, 19, 2, BGD, 1)
        g.px(19, 1, BRNG)
        g.line(18, 6, 19, 8, BGD, 1)
        g.px(19, 9, BRNG)
        # red eyes
        g.px(16, 4, BRE)
        g.px(17, 5, BRE)
        # legs (alternate with frame)
        if f == 0:
            g.line(8, 9, 7, 11, BD, 1)
            g.line(11, 9, 10, 11, BD, 1)
            g.line(14, 9, 14, 11, BD, 1)
        else:
            g.line(8, 9, 8, 11, BD, 1)
            g.line(11, 9, 12, 11, BD, 1)
            g.line(14, 9, 15, 11, BD, 1)
        g.outline(BD)
        F["crawl%d" % f] = g
    return F


# ==========================================================================
# TILESET — 16x16 tiles in a 64x64 (4x4) sheet. tile 0 is the empty tile;
# tiles 1..15 are the art. Terrain palette.
# ==========================================================================

TILE_W, TILE_H = 16, 16
TS_COLS, TS_ROWS = 4, 4
(TR1, TR2, TR3, TR4, TR5, TR6, TR7, TR8, TR9, TR10, TR11, TR12, TR13, TR14,
 TR15) = range(1, 16)


def _tx(i):
    """Tile i's top-left in the 64x64 sheet."""
    return ((i % TS_COLS) * TILE_W, (i // TS_COLS) * TILE_H)


def _hash(x, y, s=0):
    h = (x * 374761393 + y * 668265263 + s * 2246822519) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65536.0


def _grass_blades(g, ox, oy, y0=1, n=4, seed=1):
    for k in range(n):
        x = ox + 2 + k * 4
        h = 3 + int(_hash(x, oy, seed) * 3)
        g.line(x, oy + y0, x, oy + y0 - h, TR8, 1)
        g.px(x + 1, oy + y0 - 1, TR7)


def _dirt_tex(g, ox, oy, w=TILE_W, h=TILE_H):
    g.grad_v(ox, ox + w - 1, oy, oy + h - 1, [TR3, TR4], dither=True)
    for yy in range(oy + 2, oy + h, 4):
        for xx in range(ox + 1, ox + w - 1):
            r = _hash(xx, yy, 7)
            if r < 0.16:
                g.px(xx, yy, TR2)
            elif r > 0.9:
                g.px(xx, yy, TR12)


def tile_empty(g):
    pass


def tile_grass_top(g):
    ox, oy = _tx(1)
    _dirt_tex(g, ox, oy)
    g.grad_v(ox, ox + 15, oy, oy + 4, [TR7, TR8], dither=True)
    for xx in range(ox, ox + 16):
        g.px(xx, oy + 5, TR6)
    _grass_blades(g, ox, oy, 3, 4, 2)
    g.px(ox + 3, oy + 2, TR8)
    g.px(ox + 12, oy + 2, TR8)


def tile_dirt(g):
    ox, oy = _tx(2)
    _dirt_tex(g, ox, oy)
    g.grad_v(ox, ox + 15, oy, oy + 2, [TR4, TR3], dither=False)


def tile_dirt_dark(g):
    ox, oy = _tx(3)
    g.grad_v(ox, ox + 15, oy, oy + 15, [TR2, TR3], dither=True)
    for yy in range(oy + 3, oy + 16, 5):
        for xx in range(ox + 1, ox + 15):
            if _hash(xx, yy, 9) < 0.2:
                g.px(xx, yy, TR1)


def tile_stone(g):
    ox, oy = _tx(4)
    g.grad_v(ox, ox + 15, oy, oy + 15, [TR10, TR11], dither=True)
    g.hline(oy, ox, ox + 15, TR9)
    g.hline(oy + 15, ox, ox + 15, TR9)
    # cracks
    g.line(ox + 4, oy + 3, ox + 7, oy + 9, TR9, 1)
    g.line(ox + 7, oy + 9, ox + 5, oy + 13, TR9, 1)
    g.line(ox + 11, oy + 2, ox + 9, oy + 8, TR9, 1)
    g.px(ox + 6, oy + 2, TR14)
    g.px(ox + 12, oy + 6, TR14)


def tile_brick(g):
    ox, oy = _tx(5)
    g.grad_v(ox, ox + 15, oy, oy + 15, [TR11, TR12], dither=False)
    for yy in (oy + 5, oy + 10):
        g.hline(yy, ox, ox + 15, TR9)
    for r, xx in ((0, ox + 7), (1, ox + 3), (2, ox + 11)):
        g.vline(xx, oy + r * 5, oy + r * 5 + 4, TR9)
    g.px(ox + 5, oy + 2, TR14)
    g.px(ox + 13, oy + 8, TR14)


def tile_wood(g):
    ox, oy = _tx(6)
    g.grad_v(ox, ox + 15, oy, oy + 15, [TR15, TR12], dither=False)
    for yy in (oy + 5, oy + 10):
        g.hline(yy, ox, ox + 15, TR2)
    for xx in range(ox, ox + 16):
        if _hash(xx, oy + 3, 4) < 0.3:
            g.px(xx, oy + 3, TR2)
        if _hash(xx, oy + 8, 5) < 0.3:
            g.px(xx, oy + 8, TR2)
    g.hline(oy, ox, ox + 15, TR4)


def tile_bamboo(g):
    ox, oy = _tx(7)
    g.grad_v(ox, ox + 15, oy, oy + 15, [TR6, TR7], dither=True)
    for xx in (ox + 5, ox + 11):
        g.vline(xx, oy, oy + 15, TR8)
    for yy in (oy + 5, oy + 11):
        g.hline(yy, ox, ox + 15, TR5)
        g.hline(yy + 1, ox, ox + 15, TR8)


def tile_grass_tuft(g):
    ox, oy = _tx(8)
    _grass_blades(g, ox + 2, oy + 15, 14, 6, 3)


def tile_flower(g):
    ox, oy = _tx(9)
    g.line(ox + 8, oy + 15, ox + 8, oy + 8, TR6, 1)
    g.px(ox + 7, oy + 11, TR7)
    g.px(ox + 9, oy + 12, TR7)
    for dx, dy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
        g.px(ox + 8 + dx, oy + 6 + dy, TR8)
    g.px(ox + 8, oy + 6, TR4)


def tile_rock(g):
    ox, oy = _tx(10)
    g.disc(ox + 5, oy + 10, 4, TR10)
    g.disc(ox + 11, oy + 11, 3, TR11)
    g.px(ox + 3, oy + 8, TR14)
    g.px(ox + 4, oy + 7, TR14)
    g.px(ox + 9, oy + 9, TR14)


def tile_moss_stone(g):
    ox, oy = _tx(11)
    g.grad_v(ox, ox + 15, oy, oy + 15, [TR10, TR11], dither=True)
    g.hline(oy, ox, ox + 15, TR9)
    g.line(ox + 3, oy + 2, ox + 6, oy + 8, TR9, 1)
    # moss creeping over the top
    for xx in range(ox, ox + 16):
        h = 2 + int(_hash(xx, oy, 6) * 4)
        g.vline(xx, oy + 1, oy + 1 + h, TR6 if _hash(xx, 1, 6) > 0.5 else TR5)


def tile_pebbles(g):
    ox, oy = _tx(12)
    _dirt_tex(g, ox, oy)
    for k in range(6):
        x = ox + 2 + int(_hash(k, 1, 8) * 12)
        y = oy + 3 + int(_hash(k, 2, 8) * 10)
        g.disc(x, y, 1, TR10 if k % 2 else TR11)
        g.px(x - 1, y - 1, TR14)


def tile_lantern(g):
    ox, oy = _tx(13)
    # stone lantern: base, shaft, fire box, roof
    g.box(ox + 4, oy + 13, ox + 11, oy + 15, TR9)
    g.box(ox + 6, oy + 8, ox + 9, oy + 13, TR11)
    g.box(ox + 4, oy + 4, ox + 11, oy + 8, TR10)
    g.box(ox + 6, oy + 5, ox + 9, oy + 7, TR4)      # glowing window
    g.box(ox + 3, oy + 1, ox + 12, oy + 3, TR9)
    g.px(ox + 7, oy, TR14)
    g.px(ox + 8, oy, TR14)


def tile_tall_grass(g):
    ox, oy = _tx(14)
    for k in range(7):
        x = ox + 1 + k * 2
        h = 6 + int(_hash(x, oy, 10) * 8)
        g.line(x, oy + 15, x + (k % 3) - 1, oy + 15 - h, TR7, 1)
    g.px(ox + 5, oy + 6, TR8)
    g.px(ox + 10, oy + 5, TR8)


def tile_vine(g):
    ox, oy = _tx(15)
    g.line(ox + 8, oy, ox + 8, oy + 15, TR6, 1)
    for yy in range(ox + 1, oy + 15, 3):
        g.px(ox + 7, yy, TR7)
        g.px(ox + 9, yy + 1, TR7)


def tileset_frames():
    g = G(TS_COLS * TILE_W, TS_ROWS * TILE_H)
    draw = [tile_empty, tile_grass_top, tile_dirt, tile_dirt_dark, tile_stone,
            tile_brick, tile_wood, tile_bamboo, tile_grass_tuft, tile_flower,
            tile_rock, tile_moss_stone, tile_pebbles, tile_lantern,
            tile_tall_grass, tile_vine]
    for i, fn in enumerate(draw):
        fn(g)
    return [g]


# ==========================================================================
# BACKGROUNDS — seamless 240-wide strips for parallax.
# ==========================================================================
R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15 = range(1, 16)
M1, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11, M12, M13, M14, M15 = range(1, 16)
N1, N2, N3, N4, N5, N6, N7, N8, N9, N10, N11, N12, N13, N14, N15 = range(1, 16)
S1, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, S12, S13, S14, S15 = range(1, 16)
F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15 = range(1, 16)
G1, G2, G3, G4, G5, G6, G7, G8, G9, G10, G11, G12, G13, G14, G15 = range(1, 16)


def _profile(x, W, base, terms):
    v = base
    for (k, a, ph) in terms:
        v += a * math.sin(2.0 * math.pi * x / W * k + ph)
    return v


def bg_ridge():
    W, Hh = 240, 60
    g = G(W, Hh)
    for x in range(W):
        top = int(_profile(x, W, 18, [(1, 13, 0.4), (2, 8, 1.3),
                                      (5, 4, 2.1), (9, 2, 0.7)]))
        top = max(3, min(Hh - 4, top))
        for y in range(top, Hh):
            t = (y - top) / max(1, (Hh - top))
            g.p[y][x] = R11 if t < 0.2 else (R10 if t < 0.4 else (R9 if t < 0.65 else R8))
        if top > 1 and g.p[top - 1][x] == 0:
            g.p[top - 1][x] = R13
    return [g]


def _pagoda(g, cx, base):
    for tier, (w, h) in enumerate([(12, 8), (9, 8), (6, 8)]):
        y0 = base - (tier + 1) * 8
        g.box(cx - w - 3, y0, cx + w + 3, y0 + 2, M5)
        g.box(cx - w, y0 + 3, cx + w, y0 + h - 1, M6)
        g.box(cx - w + 1, y0 + 3, cx - w + 2, y0 + h - 1, M7)
    g.vline(cx, base - 33, base - 24, M5)
    g.px(cx, base - 34, M5)


def bg_mid():
    W, Hh = 240, 80
    g = G(W, Hh)
    for x in range(W):
        top = int(_profile(x, W, 40, [(2, 10, 0.2), (4, 6, 1.0), (8, 3, 2.5)]))
        top = max(12, min(Hh - 8, top))
        for y in range(top, Hh):
            t = (y - top) / max(1, (Hh - top))
            g.p[y][x] = M9 if t < 0.25 else (M8 if t < 0.5 else (M7 if t < 0.8 else M6))
        if top > 1 and g.p[top - 1][x] == 0:
            g.p[top - 1][x] = M14
    for px in (60, 60 - W, 60 + W):
        _pagoda(g, px, 40)
    return [g]


def _pine(g, cx, base, h, c):
    for i in range(h):
        w = 1 + i
        y = base - i
        g.box(cx - w, y, cx + w, y, c)
    g.box(cx - 1, base - h - 2, cx + 1, base - h, c)


def _cherry(g, cx, base, h, c):
    g.vline(cx, base - h, base, c)
    for i in range(3):
        g.disc(cx + (i - 1) * 3, base - h - 2 + (i % 2), 3, c)


def bg_near():
    W, Hh = 240, 100
    g = G(W, Hh)
    base = Hh - 24
    g.grad_v(0, W - 1, base, Hh - 1, [N3, N2], dither=True)
    for x in range(0, W, 17):
        h = 20 + int(_hash(x, 3, 11) * 22)
        _pine(g, x + 8, base + 2, h, N3 if (x // 17) % 2 else N4)
    for x in range(8, W, 29):
        h = 14 + int(_hash(x, 5, 12) * 14)
        _cherry(g, x + 6, base + 3, h, N6 if (x // 29) % 2 else N7)
    return [g]


def moon_frames():
    g = G(24, 24)
    g.disc(12, 12, 9, S8)
    g.disc(12, 12, 7, S9)
    g.disc(9, 10, 3, S10)
    g.disc(14, 13, 2, S10)
    g.disc(11, 15, 1, S10)
    g.disc(10, 9, 1, S12)
    g.disc(13, 8, 1, S12)
    g.outline(S1)
    return [g]


def slash_frames():
    F = []
    for i in range(3):
        g = G(24, 24)
        for k in range(6):
            a = math.radians(20 + i * 50 + k * 8)
            x = int(12 + 9 * math.cos(a))
            y = int(12 + 9 * math.sin(a))
            c = F14 if k < 2 else (F10 if k < 4 else F4)
            g.disc(x, y, 1, c)
        for k in range(4):
            a = math.radians(15 + i * 50 + k * 14)
            x = int(12 + 5 * math.cos(a))
            y = int(12 + 5 * math.sin(a))
            g.px(x, y, F5)
        F.append(g)
    return F


def fx_frames():
    F = []
    g = G(16, 16)
    g.line(8, 2, 8, 13, F4, 1)
    g.line(2, 8, 13, 8, F4, 1)
    g.px(8, 8, F14)
    for dx, dy in ((5, 5), (11, 5), (5, 11), (11, 11)):
        g.px(dx, dy, F4)
    F.append(g)
    g = G(16, 16)
    g.disc(7, 7, 3, F7)
    g.disc(8, 8, 3, F8)
    g.px(6, 6, F9); g.px(9, 9, F9)
    g.px(7, 9, F6); g.px(8, 6, F6)
    F.append(g)
    g = G(16, 16)
    g.disc(8, 10, 3, F11)
    g.disc(6, 9, 2, F10)
    g.disc(10, 9, 2, F10)
    g.px(7, 7, F14); g.px(9, 7, F14)
    F.append(g)
    g = G(16, 16)
    g.disc(8, 8, 4, F12)
    g.disc(8, 8, 2, F14)
    for dx, dy in ((0, -6), (0, 6), (-6, 0), (6, 0),
                   (-4, -4), (4, 4), (-4, 4), (4, -4)):
        g.px(8 + dx, 8 + dy, F4)
    F.append(g)
    return F


def coin_frames():
    F = []
    for f in range(2):
        g = G(12, 12)
        w = 6 if f == 0 else 4
        g.disc(6, 6, w, F3)
        g.disc(6, 6, w - 2, F4)
        g.px(4, 4, F5); g.px(8, 4, F5)
        g.px(4, 8, F2); g.px(8, 8, F2)
        g.outline(F2)
        F.append(g)
    return F


def goal_frames():
    g = G(48, 56)
    g.box(2, 48, 45, 55, G8)
    g.box(2, 48, 45, 49, G9)
    g.box(4, 50, 8, 54, G10)
    g.box(39, 50, 43, 54, G10)
    g.box(8, 12, 13, 48, G2)
    g.box(8, 12, 13, 14, G3)
    g.box(8, 44, 13, 48, G2)
    g.box(34, 12, 39, 48, G2)
    g.box(34, 12, 39, 14, G3)
    g.box(34, 44, 39, 48, G2)
    g.box(2, 6, 45, 11, G2)
    g.box(2, 6, 45, 7, G3)
    g.box(2, 11, 45, 12, G1)
    g.box(4, 16, 43, 20, G2)
    g.box(4, 16, 43, 17, G3)
    g.box(20, 13, 27, 20, G6)
    g.box(21, 14, 26, 19, G7)
    g.box(16, 22, 18, 25, G4)
    g.box(29, 22, 31, 25, G4)
    g.outline(G1)
    return [g]


# ==========================================================================
# Sprite registry: name -> dict(pal, frames[...], origin, collision, anims)
# ==========================================================================

HERO_ORDER = ["idle", "run0", "run1", "run2", "jump", "fall",
              "slash0", "slash1", "slash2", "hurt", "dead"]
ONI_ORDER = ["walk0", "walk1", "atk0", "atk1"]
BEETLE_ORDER = ["crawl0", "crawl1"]

SPRITES = {}

# Level legend: character -> tileset tile index (0 = empty). The tileset is a
# 4x4 grid of 16px tiles (64x64): 0 empty, 1 grass_top, 2 dirt, 3 dirt_dark,
# 4 stone, 5 brick, 6 wood, 7 bamboo, 8 grass_tuft, 9 flower, 10 rock,
# 11 moss_stone, 12 pebbles, 13 lantern, 14 tall_grass, 15 vine.
LEGEND = {
    ".": None,
    "#": 1, "d": 2, "D": 3, "s": 4, "b": 5, "w": 6, "B": 7,
    "g": 8, "f": 9, "r": 10, "m": 11, "p": 12, "l": 13, "t": 14, "v": 15,
}

# Solid tiles (the collision query in game.c mirrors this set).
SOLID = set([1, 2, 3, 4, 5, 6, 7, 10, 11, 12])


def build_level():
    COLS, ROWS = 130, 17
    G = 12
    grid = [["."] * COLS for _ in range(ROWS)]

    def fill(x0, x1, y0, y1, ch):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if 0 <= x < COLS and 0 <= y < ROWS:
                    grid[y][x] = ch

    # base ground: grass surface, dirt below, stone at the very bottom
    for x in range(COLS):
        grid[G][x] = "#"
        for y in range(G + 1, ROWS):
            grid[y][x] = "d" if y < ROWS - 1 else "s"
    # pits (falling in is lethal) — 2 tiles wide, clearable with a jump
    for (a, b) in [(10, 11), (32, 33), (54, 55), (88, 89)]:
        fill(a, b, G, ROWS - 1, ".")
    # a stepped rise to the goal plateau at the end
    for i in range(4):
        fill(112 + i, 112 + i, G - 1 - i, G - 1, "#")
    fill(116, 126, G - 4, G - 4, "#")
    for x in range(116, 127):
        for y in range(G - 3, ROWS):
            grid[y][x] = "d" if y < ROWS - 1 else "s"
    # decoration sits in the air cell above the surface so it never opens a hole
    for x in range(4, COLS, 7):
        if grid[G][x] == "#":
            grid[G - 1][x] = "g"
    for x in range(9, COLS, 13):
        if grid[G][x] == "#":
            grid[G - 1][x] = "f"
    grid[G - 1][14] = "l"
    grid[G - 5][120] = "l"
    return ["".join(row) for row in grid]


def register_sprites():
    hf = hero_frames()
    of = oni_frames()
    bf = beetle_frames()
    SPRITES["hero"] = dict(
        file="hero", pal="hero", frames=[hf[n] for n in HERO_ORDER],
        origin=[14, 40], collision=[8, 12, 12, 28],
        anims=[("run", [1, 2, 3], 90), ("heroslash", [6, 7, 8], 45)])
    SPRITES["oni"] = dict(
        file="oni", pal="oni", frames=[of[n] for n in ONI_ORDER],
        origin=[14, 36], collision=[6, 18, 16, 18],
        anims=[("oniwalk", [0, 1], 140), ("oniattack", [2, 3], 90)])
    SPRITES["beetle"] = dict(
        file="beetle", pal="beetle", frames=[bf[n] for n in BEETLE_ORDER],
        origin=[10, 12], collision=[5, 2, 14, 10],
        anims=[("crawl", [0, 1], 150)])
    SPRITES["tiles"] = dict(file="tiles", pal="terrain",
                            frames=tileset_frames())
    SPRITES["ridge"] = dict(file="bg_ridge", pal="ridge", frames=bg_ridge())
    SPRITES["mid"] = dict(file="bg_mid", pal="mid", frames=bg_mid())
    SPRITES["near"] = dict(file="bg_near", pal="near", frames=bg_near())
    SPRITES["moon"] = dict(file="moon", pal="sky", frames=moon_frames())
    SPRITES["slash"] = dict(file="slash", pal="fx", frames=slash_frames())
    SPRITES["fx"] = dict(file="fx", pal="fx", frames=fx_frames())
    SPRITES["coin"] = dict(file="coin", pal="fx", frames=coin_frames())
    SPRITES["goal"] = dict(file="goal", pal="goal", frames=goal_frames())


def make_assets():
    doc = {"format": 1, "palettes": {}, "sprites": [], "animations": [],
           "maps": []}
    for name, (bank, colors) in PALETTES.items():
        doc["palettes"][name] = {
            "bank": bank,
            "colors": ["#000000"] + [hx(c) for c in colors[1:]]}
    for name, s in SPRITES.items():
        e = {"name": name, "file": "art/%s.png" % s["file"],
             "palette": s["pal"], "frames": len(s["frames"])}
        if s.get("origin"):
            e["origin"] = s["origin"]
        if s.get("collision"):
            e["collision"] = s["collision"]
        doc["sprites"].append(e)
    for name, s in SPRITES.items():
        for (aname, frames, ms) in (s.get("anims") or []):
            doc["animations"].append({"name": aname, "sprite": name,
                                     "frames": frames, "ms": ms})
    doc["maps"].append({"name": "level1", "file": "level1.txt",
                        "tileset": "tiles", "tile_w": 16, "tile_h": 16,
                        "legend": LEGEND})
    return doc


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dir", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--sprite", metavar="NAME")
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--ascii", metavar="NAME")
    a = ap.parse_args()

    register_sprites()

    if a.ascii:
        print(ascii_art(SPRITES[a.ascii]["frames"][0]))
        return 0
    if a.sprite:
        s = SPRITES[a.sprite]
        write_sheet(os.path.join(a.dir, "preview_%s.png" % a.sprite),
                    s["frames"], s["pal"], scale=a.scale)
        print("preview: preview_%s.png (%d frames %dx%d)" %
              (a.sprite, len(s["frames"]), s["frames"][0].w, s["frames"][0].h))
        return 0

    art = os.path.join(a.dir, "art")
    os.makedirs(art, exist_ok=True)
    for name, s in SPRITES.items():
        fr = s["frames"]
        write_strip(os.path.join(art, s["file"] + ".png"), fr, s["pal"],
                    fr[0].w, fr[0].h)
    with open(os.path.join(a.dir, "assets.json"), "w") as f:
        json.dump(make_assets(), f, indent=1)
    rows = build_level()
    with open(os.path.join(a.dir, "level1.txt"), "w") as f:
        f.write("\n".join(rows) + "\n")
    solid = set("#dDsbwBrmp")
    vals = []
    for row in rows:
        for ch in row:
            vals.append(1 if ch in solid else 0)
    with open(os.path.join(a.dir, "level.h"), "w") as f:
        f.write("/* generated by gen_art.py - level collision grid (1 = solid) */\n")
        f.write("#define LEVEL_COLS %d\n#define LEVEL_ROWS %d\n\n"
                % (len(rows[0]), len(rows)))
        f.write("static const unsigned char level_solid[LEVEL_COLS * LEVEL_ROWS] = {\n")
        for k in range(0, len(vals), 26):
            f.write("    " + ",".join(str(v) for v in vals[k:k + 26]) + ",\n")
        f.write("};\n")
    print("wrote art/*.png, assets.json, level1.txt, level.h")
    return 0


if __name__ == "__main__":
    sys.exit(main())
