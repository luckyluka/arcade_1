#!/usr/bin/env python3
"""gen_round7.py — round 7's art: a lighthouse keeper on a cliff at dusk.

Everything the game draws is painted HERE, deterministically, with the
repository's own drawing library (tools/arc1_art/canvas.py).  Nothing is
pasted in and nothing is hand-edited: this script is the master.

    python3 round7/art/gen_round7.py

It writes

    round7/art/strips/<name>.png   one horizontal strip per sprite
    round7/art/<layer>_map.txt      the world layers (ASCII grids)
    round7/assets.json              the manifest (palettes, sprites,
                                    animations, maps, provenance)

The world is 30x64 tiles of 16 px (480x1024) — taller than the 480x270
screen, so the camera scrolls up the tower while the sea rises.

Deterministic: no clock, no unseeded randomness (the one RNG is C.Rng with
a fixed seed), no dict-order dependence.
"""

import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from arc1_art import canvas as C          # noqa: E402

ROUND = HERE
# the pack's output directory: --out-dir, or the pack itself
_OUT = HERE
if "--out-dir" in sys.argv:
    _OUT = os.path.abspath(sys.argv[sys.argv.index("--out-dir") + 1])
STRIP_DIR = os.path.join(_OUT, "strips")
MAP_DIR = os.path.join(_OUT, "maps")

TILE = 16
COLS = 30
ROWS = 64
WORLD_W = COLS * TILE          # 480
WORLD_H = ROWS * TILE          # 1024

SEED = 20261021


# --------------------------------------------------------------------------
# The palettes — the repository's presets, one bank per asset group
# --------------------------------------------------------------------------

def preset(name):
    """A palette preset's 16 colours (index 0 is the transparent key)."""
    path = os.path.join(ROOT, "art", "palettes", name + ".json")
    with open(path, encoding="utf-8") as f:
        return json.load(f)["colors"]


SKY_P = preset("dusk16")
TOWER_P = preset("neutral16")
ROCK_P = preset("props16")
SEA_P = preset("cool16")
KEEPER_P = preset("warm16")
LAMP_P = preset("dusk16")
UI_P = preset("ui16")

# The keeper's palette indices (warm16).
K_OUT = 1        # #211408  the outline
K_DARK = 2       # #392410  boots, hat
K_DARK2 = 3      # #523418  hat brim, belt
K_COAT_D = 4     # #6B4521  the coat's shadow side
K_COAT_M = 5     # #845931  the coat's body
K_COAT_L = 8     # #C6A263  the coat's lit side
K_SKIN = 10      # #EFD394
K_SKIN_L = 11    # #F7E7B5
K_SCARF = 13     # #C66931  the one accent
K_BRASS = 15     # #F7C373  the lantern


# --------------------------------------------------------------------------
# Small painting helpers
# --------------------------------------------------------------------------

def canvas(w, h):
    return C.Canvas(w, h)


def speckle(c, rng, colour, n, w=None, h=None):
    """A few deterministic specks — texture, never noise on its own."""
    w = c.w if w is None else w
    h = c.h if h is None else h
    for _ in range(n):
        c.px(rng.rand(w), rng.rand(h), colour)


def bricks(c, x0, y0, w, h, base, mortar, light, rng):
    """A brick wall: courses with staggered joints."""
    c.rect(x0, y0, w, h, base)
    for band in range((h + 7) // 8):
        y = y0 + band * 8
        c.hline(y, x0, x0 + w - 1, mortar)
        joint = x0 + (0 if band % 2 == 0 else 8)
        x = joint
        while x < x0 + w:
            c.vline(x, y, min(y + 7, y0 + h - 1), mortar)
            x += 16
    # the lit top edge of each course
    for band in range((h + 7) // 8):
        y = y0 + band * 8
        for x in range(x0, x0 + w):
            if (x + band) % 5 == 0:
                c.px(x, y + 1, light)
    speckle(c, rng, mortar, 5, w, h)


# --------------------------------------------------------------------------
# The tower tileset (6 x 3 tiles of 16 px) — palette neutral16
# --------------------------------------------------------------------------

def tile_wall(c, rng):
    bricks(c, 0, 0, 16, 16, 6, 3, 7, rng)


def tile_wall_dark(c, rng):
    bricks(c, 0, 0, 16, 16, 5, 3, 6, rng)


def tile_window(c, rng):
    bricks(c, 0, 0, 16, 16, 6, 3, 7, rng)
    # the opening: a tall slit with a round head
    c.rect(6, 5, 4, 8, 1)
    c.disc(7, 5, 2, 1)
    # the frame and the sill
    c.vline(5, 4, 13, 8)
    c.vline(10, 4, 13, 8)
    c.hline(13, 5, 10, 8)
    c.hline(14, 4, 11, 9)
    # a mullion
    c.hline(9, 6, 9, 8)


def tile_ladder(c, rng):
    """The ladder: bright iron in a dark recess, so it reads at 1x."""
    bricks(c, 0, 0, 16, 16, 6, 3, 7, rng)
    # the recess the ladder hangs in
    c.rect(2, 0, 12, 16, 3)
    c.vline(2, 0, 15, 2)
    c.vline(13, 0, 15, 2)
    # the rails
    c.vline(3, 0, 15, 9)
    c.vline(4, 0, 15, 8)
    c.vline(11, 0, 15, 9)
    c.vline(12, 0, 15, 8)
    # the rungs (8 px apart, so the ladder reads as continuous)
    for y in (2, 10):
        c.hline(y, 4, 11, 8)
        c.hline(y + 1, 4, 11, 7)


def tile_gallery(c, rng):
    """The balcony band: a lit lip, corbels, shadow under the deck."""
    c.rect(0, 0, 16, 16, 4)
    c.rect(0, 0, 16, 3, 9)
    c.hline(3, 0, 15, 7)
    for x in (2, 9):
        c.rect(x, 4, 3, 4, 7)
    c.rect(0, 11, 16, 5, 2)
    c.hline(11, 0, 15, 3)
    speckle(c, rng, 5, 4)


def tile_glass(c, rng):
    """The lantern room: a big pane with mullions and a faint reflection."""
    c.rect(0, 0, 16, 16, 2)
    c.rect(1, 1, 14, 13, 3)
    c.vline(7, 1, 13, 2)
    c.vline(8, 1, 13, 2)
    c.hline(7, 1, 14, 2)
    c.line(3, 11, 6, 3, 4)
    c.line(4, 12, 7, 4, 4)
    c.rect(0, 14, 16, 2, 7)


def tile_roof(c, rng):
    """Slate shingles, overlapping."""
    c.rect(0, 0, 16, 16, 3)
    for row in range(4):
        y = row * 4
        off = 0 if row % 2 == 0 else 2
        for k in range(-1, 4):
            x = k * 5 + off
            c.rect(x, y, 4, 3, 4)
            c.px(x, y, 5)
    c.hline(15, 0, 15, 2)


def tile_buttress(c, rng):
    bricks(c, 0, 0, 16, 16, 6, 3, 7, rng)
    c.rect(0, 0, 3, 16, 7)
    c.vline(3, 0, 15, 3)
    c.vline(4, 0, 15, 5)


def tile_base(c, rng):
    """The tower's foot: big blocks."""
    c.rect(0, 0, 16, 16, 5)
    for y in (0, 8):
        c.hline(y, 0, 15, 3)
    for x in (0, 8):
        c.vline(x, 0, 15, 3)
    for x in range(1, 8):
        if (x * 3) % 5 == 0:
            c.px(x, 3, 7)
    speckle(c, rng, 4, 6)


def tile_vent(c, rng):
    bricks(c, 0, 0, 16, 16, 6, 3, 7, rng)
    c.rect(4, 4, 8, 8, 1)
    c.rect(5, 5, 6, 6, 2)
    c.hline(3, 3, 12, 8)
    c.hline(12, 3, 12, 8)


TOWER_TILES = [
    tile_wall, tile_window, tile_ladder, tile_gallery, tile_glass, tile_roof,
    tile_wall_dark, tile_buttress, tile_vent, tile_base, tile_wall_dark,
    tile_wall,
]


# --------------------------------------------------------------------------
# The cliff tileset (4 x 2 tiles) — palette props16
# --------------------------------------------------------------------------

def facets(c, rng, base, mid, light, dark):
    """Rock as angular blocks, not bands.

    The blocks are drawn whole and their edges run diagonally, so a wall of
    these tiles does not read as a stack of horizontal steps.
    """
    c.rect(0, 0, 16, 16, base)
    c.polygon([(0, 0), (6, 2), (5, 9), (0, 10)], mid)
    c.polygon([(6, 2), (12, 0), (13, 7), (5, 9)], light)
    c.polygon([(12, 0), (16, 0), (16, 16), (13, 7)], dark)
    # the lit lower edges of the blocks
    c.line(0, 10, 5, 9, mid)
    c.line(5, 9, 13, 7, mid)
    # the cracks
    c.line(7, 16, 9, 10, 13)
    c.line(11, 16, 12, 12, 13)


def tile_rock(c, rng):
    facets(c, rng, 7, 8, 14, 13)


def tile_rock_light(c, rng):
    facets(c, rng, 6, 7, 14, 8)


def tile_rock_shadow(c, rng):
    facets(c, rng, 8, 13, 7, 13)


def tile_rock_moss(c, rng):
    tile_rock(c, rng)
    # the mossy crown on the top edge
    for x in range(16):
        depth = 2 + (1 if x % 5 < 2 else 0)
        for y in range(depth):
            c.px(x, y, 10)
        c.px(x, depth, 11)
    c.line(0, 4, 15, 3, 10)


def tile_rock_edge(c, rng):
    """The cliff's lit left edge and its shadowed right."""
    tile_rock(c, rng)
    for y in range(16):
        c.px(0, y, 14)
        c.px(15, y, 13)
        c.px(14, y, 8)


def tile_pebbles(c, rng):
    c.rect(0, 0, 16, 16, 7)
    for _ in range(4):
        x, y = rng.rand(13) + 1, rng.rand(11) + 3
        c.disc(x, y, 1, 14)
    for _ in range(3):
        x, y = rng.rand(13) + 1, rng.rand(11) + 3
        c.px(x, y, 8)


ROCK_TILES = [
    tile_rock, tile_rock_light, tile_rock_shadow, tile_rock_moss,
    tile_rock_edge, tile_pebbles, tile_rock, tile_rock_light,
]


# --------------------------------------------------------------------------
# The sea tileset (3 x 2 tiles) — palette cool16, fully opaque
# --------------------------------------------------------------------------

def tile_crest(c, rng, phase):
    """The surface: a foam band whose depth waves across the tile.

    The wave is one long swell over three tiles (48 px), so the three crest
    tiles in a row read as one wave, not as three mountains.
    """
    c.rect(0, 0, 16, 16, 6)
    c.rect(0, 8, 16, 8, 4)
    for x in range(16):
        t = (x + phase) / 48.0
        depth = 2 + int(round(1.6 * math.sin(t * 2.0 * math.pi)))
        for y in range(depth):
            c.px(x, y, 13)
        c.px(x, depth, 9)


def tile_crest_b(c, rng):
    tile_crest(c, rng, 16)


def tile_crest_c(c, rng):
    tile_crest(c, rng, 32)


def tile_flat(c, rng):
    c.rect(0, 0, 16, 16, 6)
    c.rect(0, 6, 16, 10, 4)
    for _ in range(8):
        x, y = rng.rand(16), 6 + rng.rand(6)
        c.px(x, y, 7)


def tile_deep(c, rng):
    c.rect(0, 0, 16, 16, 4)
    for _ in range(10):
        x, y = rng.rand(16), rng.rand(16)
        c.px(x, y, 3)


def tile_swirl(c, rng):
    c.rect(0, 0, 16, 16, 6)
    c.rect(0, 6, 16, 10, 4)
    for x in range(16):
        t = x / 16.0
        y = 8 + int(round(2.0 * math.sin(t * 2.0 * math.pi)))
        c.px(x, y, 13)
        c.px(x, y + 1, 9)


SEA_TILES = [
    lambda c, r: tile_crest(c, r, 0), tile_crest_b, tile_crest_c,
    tile_flat, tile_deep, tile_swirl,
]


# --------------------------------------------------------------------------
# The sky tileset (13 tiles of 16 px) — palette dusk16, fully opaque
# --------------------------------------------------------------------------

SKY_BANDS = [
    # The sky's bands: (first row, last row, palette index). The ramp runs
    # through the indigo family and then turns warm at the horizon. The
    # bands are flat on purpose — a dither between two far-apart hues
    # reads as static at 1x, and a dither inside one family reads as
    # stripes. The warm bands are thin: they are the glow at the horizon,
    # not the sky.
    (0, 19, 2),    # #181831  the deep night
    (20, 27, 3),   # #21244A
    (28, 33, 4),   # #31306B
    (34, 38, 5),   # #424184
    (39, 42, 6),   # #5255A5
    (43, 45, 7),   # #7371BD  violet
    (46, 47, 10),  # #7B2839  the first crimson
    (48, 49, 11),  # #A5384A
    (50, 51, 12),  # #C6595A
    (52, 53, 13),  # #EF8E73
    (54, 63, 15),  # #FFE3A5  the gold at the horizon
]
SKY_CHARS = "abcdefghijkSCM"   # the bands, then stars, cloud, moon


def sky_flat(colour):
    def paint(c, rng):
        c.rect(0, 0, 16, 16, colour)
    return paint


def sky_stars(c, rng):
    c.rect(0, 0, 16, 16, SKY_BANDS[0][2])
    for x, y, col in ((2, 3, 8), (9, 1, 8), (13, 7, 7), (5, 11, 8),
                      (11, 14, 8), (7, 6, 7)):
        c.px(x, y, col)
    c.px(2, 3, 9)


def sky_cloud(c, rng):
    c.rect(0, 0, 16, 16, SKY_BANDS[0][2])
    c.ellipse(8, 10, 6, 3, 3)
    c.ellipse(6, 8, 4, 2, 4)
    c.ellipse(11, 9, 3, 2, 4)
    c.ellipse(7, 7, 2, 1, 5)


def sky_moon(c, rng):
    c.rect(0, 0, 16, 16, SKY_BANDS[0][2])
    c.disc(8, 7, 4, 9)
    c.disc(10, 9, 3, 8)


def sky_tiles():
    """The sky tiles: the flat bands, and the things that live in the deep
    upper sky."""
    tiles = [sky_flat(colour) for _, _, colour in SKY_BANDS]
    tiles += [sky_stars, sky_cloud, sky_moon]
    return tiles


def sky_legend():
    """The sky map's legend: one character per sky tile."""
    leg = {}
    for i, ch in enumerate(SKY_CHARS):
        leg[ch] = i
    return leg


# --------------------------------------------------------------------------
# The keeper — 24 x 32, feet on the last row
# --------------------------------------------------------------------------

def keeper_body(c, rng, bob=0):
    """The keeper's body — the same in every state (arms aside).

    `bob` lifts the head, the hat and the scarf by a pixel (the breath in
    the idle animation); the scarf is three rows deep so it still touches
    the coat when it moves.
    """
    # the legs and the boots
    c.rect(8, 26, 3, 4, K_COAT_D)
    c.rect(12, 26, 3, 4, K_COAT_D)
    c.rect(8, 29, 3, 2, K_DARK)
    c.rect(12, 29, 3, 2, K_DARK)

    # the coat
    mask = canvas(24, 32)
    mask.polygon([(8, 13), (15, 13), (17, 27), (6, 27)], 1)
    for y in range(32):
        for x in range(24):
            if not mask.get(x, y):
                continue
            if x <= 8:
                c.px(x, y, K_COAT_L)
            elif x == 9:
                c.px(x, y, K_COAT_L if y % 2 == 0 else K_COAT_M)
            elif x >= 15:
                c.px(x, y, K_COAT_D)
            else:
                c.px(x, y, K_COAT_M)
    # the hem's shadow
    for x in range(6, 18):
        if mask.get(x, 26):
            c.px(x, 26, K_COAT_D)
    # the belt
    for x in range(7, 17):
        if mask.get(x, 20):
            c.px(x, 20, K_DARK2)
    c.px(11, 20, K_BRASS)
    c.px(12, 20, K_BRASS)

    # the head
    c.ellipse(11, 7 + bob, 3, 3, K_SKIN)
    c.px(8, 7 + bob, K_SKIN_L)
    c.px(9, 6 + bob, K_SKIN_L)
    c.px(9, 7 + bob, K_DARK)          # the eye
    c.px(12, 7 + bob, K_DARK)         # the eye

    # the hat: the crown, then the brim over the brow
    c.rect(8, 1 + bob, 7, 4, K_DARK)
    c.rect(9, 2 + bob, 5, 3, K_DARK2)
    c.rect(7, 4 + bob, 9, 2, K_DARK2)
    c.hline(5 + bob, 7, 15, K_DARK)

    # the scarf (the one accent) and its tail
    c.rect(9, 11 + bob, 6, 3, K_SCARF)
    c.rect(8, 13 + bob, 2, 4, K_SCARF)


def keeper_arm_down(c, x):
    """An arm hanging at the side, with a hand at the end."""
    c.rect(x, 14, 3, 10, K_COAT_D)
    c.rect(x, 24, 3, 2, K_SKIN)


def keeper_arm_up(c, x, top):
    """An arm reaching up, with a hand at the top. The shoulder end stays
    inside the coat, so the arm never reads as a floating stick."""
    c.line(x + 2, 18, x, top + 1, K_COAT_D, 3)
    c.rect(x - 1, top - 1, 3, 3, K_SKIN)


def keeper_lantern(c, x, y):
    """A lit lantern hanging from the keeper's hand."""
    c.vline(x + 2, y - 2, y - 1, K_DARK2)
    c.rect(x, y, 5, 5, K_BRASS)
    c.rect(x + 1, y + 1, 3, 3, 12)
    c.px(x + 2, y + 2, K_COAT_L)
    c.rect(x - 1, y + 5, 7, 1, K_DARK2)


def keeper_frame(state, phase):
    """One keeper frame: 24 x 32, the boots on row 30."""
    c = canvas(24, 32)
    rng = C.Rng(SEED + phase)
    bob = -1 if (state == "idle" and phase == 1) else 0
    keeper_body(c, rng, bob)

    if state == "idle":
        keeper_arm_down(c, 6)
        keeper_arm_down(c, 15)
    elif state == "climb":
        if phase == 0:
            keeper_arm_up(c, 5, 6)
            keeper_arm_down(c, 15)
        elif phase == 1:
            keeper_arm_up(c, 5, 5)
            keeper_arm_up(c, 16, 5)
        elif phase == 2:
            keeper_arm_down(c, 6)
            keeper_arm_up(c, 16, 5)
        else:
            keeper_arm_up(c, 5, 6)
            keeper_arm_up(c, 16, 6)
        # the legs tuck as he pulls himself up
        if phase % 2:
            c.rect(12, 25, 3, 3, K_COAT_D)
            c.rect(12, 28, 3, 2, K_DARK)
    elif state == "carry":
        keeper_arm_down(c, 6)
        # the right arm is bent forward, holding the lantern
        c.line(16, 16, 18, 20, K_COAT_D, 3)
        c.rect(17, 20, 3, 2, K_SKIN)
        keeper_lantern(c, 16, 22)
    else:
        raise ValueError("unknown keeper state %r" % state)

    # the outline (outside the silhouette, one pixel, one colour)
    c.outline(K_OUT)
    return c


def keeper_idle_frames():
    return [keeper_frame("idle", 0), keeper_frame("idle", 1)]


def keeper_climb_frames():
    return [keeper_frame("climb", i) for i in range(4)]


def keeper_carry_frames():
    return [keeper_frame("carry", 0), keeper_frame("carry", 1)]


# --------------------------------------------------------------------------
# The wall lamp — 16 x 16, dusk16
# --------------------------------------------------------------------------

def lamp_frame(lit, phase):
    """A lantern on a bracket: lit or dark, with a flickering flame."""
    c = canvas(16, 16)
    # the bracket (it reaches the tower wall on the right)
    c.rect(0, 1, 15, 1, 8)
    c.rect(0, 2, 15, 1, 5)
    c.rect(0, 0, 2, 3, 5)
    # the chain
    c.vline(5, 3, 5, 4)
    # the lantern's cap
    c.rect(2, 5, 8, 2, 5)
    c.hline(6, 2, 9, 3)
    # the glass
    c.rect(3, 7, 6, 6, 2)
    if lit:
        # a flame, not a lit window: a taper with a bright core
        flame = ((8, (5,)), (9, (5, 6)), (10, (4, 5, 6, 7)), (11, (4, 5, 6, 7)),
                 (12, (5, 6)))
        for y, xs in flame:
            for x in xs:
                c.px(x, y, 14)
        for x, y in ((5, 10), (5, 11), (6, 10), (6, 11)):
            c.px(x, y, 15)
        if phase == 1:
            c.px(5, 9, 13)
            c.px(6, 12, 13)
    else:
        c.px(4, 9, 3)
    # the frame's uprights and the base
    c.vline(2, 6, 12, 4)
    c.vline(9, 6, 12, 4)
    c.rect(1, 13, 10, 2, 4)
    c.hline(14, 1, 10, 3)
    c.outline(1)
    return c


# --------------------------------------------------------------------------
# The world layers
# --------------------------------------------------------------------------

def build_tower_map():
    rows = [['.'] * COLS for _ in range(ROWS)]
    # the shaft
    for r in range(9, 48):
        for c in range(13, 19):
            rows[r][c] = 'w'
    # the ladder: the tower's left face, so the lamps can hang beside it
    for r in range(9, 46):
        rows[r][13] = 'l'
    # the windows
    for r in (13, 21, 29, 37, 43):
        rows[r][14] = 'n'
        rows[r][16] = 'n'
    # the buttress at the foot
    for r in range(44, 48):
        rows[r][18] = 'p'
    # the lantern room
    for r in range(2, 8):
        for c in range(13, 19):
            rows[r][c] = 'b'
    # the gallery
    for c in range(11, 21):
        rows[8][c] = 'g'
    # the roof
    for r in range(0, 2):
        for c in range(12, 20):
            rows[r][c] = 'r'
    # the shaft's foot on the cliff
    for r in range(46, 50):
        for c in range(13, 19):
            rows[r][c] = 'w'
    return rows


def build_rock_map():
    """The cliff: a broken edge, not a pyramid."""
    rows = [['.'] * COLS for _ in range(ROWS)]
    profile = {46: (9, 21), 47: (8, 22), 48: (6, 23), 49: (7, 24),
               50: (5, 24), 51: (4, 25), 52: (6, 25), 53: (3, 26),
               54: (5, 26), 55: (4, 26), 56: (3, 27), 57: (2, 27)}
    for r in range(46, ROWS):
        lo, hi = profile.get(r, (2, 27))
        for c in range(lo, hi):
            rows[r][c] = 'c'
    # the mossy crown: the topmost rock of every column
    for c in range(COLS):
        for r in range(ROWS):
            if rows[r][c] == 'c':
                rows[r][c] = 'm'
                break
    # the shadowed left edge of the cliff
    for r in range(46, ROWS):
        for c in range(COLS):
            if rows[r][c] != '.':
                rows[r][c] = 's'
                break
    return rows


def build_sky_map():
    """The dusk sky: flat bands from the deep night at the top of the world
    to the amber at the horizon, with the stars in the deep part."""
    rows = [['.'] * COLS for _ in range(ROWS)]
    for i, (lo, hi, colour) in enumerate(SKY_BANDS):
        ch = SKY_CHARS[i]
        for r in range(lo, hi + 1):
            for c in range(COLS):
                rows[r][c] = ch
    # the stars, the clouds and the moon (the deep upper sky only)
    stars = [(2, 4), (6, 2), (11, 6), (15, 3), (19, 8), (24, 2), (27, 7),
             (3, 7), (9, 5), (14, 1), (21, 3), (26, 9), (8, 8), (17, 6),
             (22, 9), (5, 9), (12, 8), (28, 4), (20, 7)]
    for c, r in stars:
        rows[r][c] = 'S'
    for c, r in ((4, 5), (23, 6), (12, 3), (27, 8)):
        rows[r][c] = 'C'
    rows[3][22] = 'M'
    rows[6][9] = 'M'
    return rows


def build_sea_map():
    """The sea band: the surface on top, the deep water below."""
    rows = [['.'] * COLS for _ in range(8)]
    crest = "abc"
    for c in range(COLS):
        rows[0][c] = crest[c % 3]
        rows[1][c] = 'd'
        for r in range(2, 8):
            rows[r][c] = 'e'
    # a swirl of foam
    rows[2][7] = 'f'
    rows[2][8] = 'f'
    rows[3][22] = 'f'
    return rows


def build_sea_deep_map():
    """The deep water under the surface: one column of the deep tile, tall
    enough to reach the bottom of the world from the highest the sea gets."""
    rows = [['.'] * COLS for _ in range(56)]
    for r in range(56):
        for c in range(COLS):
            rows[r][c] = 'e'
    return rows


# --------------------------------------------------------------------------
# Writing it all out
# --------------------------------------------------------------------------

def build_tileset(painters, cols, palette, rng_seed):
    """A tileset image: ONE frame holding the whole grid of tiles."""
    rows = (len(painters) + cols - 1) // cols
    c = canvas(cols * TILE, rows * TILE)
    for i, paint in enumerate(painters):
        tile = canvas(TILE, TILE)
        paint(tile, C.Rng(rng_seed + i))
        c.stamp(tile, (i % cols) * TILE, (i // cols) * TILE)
    return c


def write_strip(name, frames, palette):
    path = os.path.join(STRIP_DIR, name + ".png")
    C.to_strip(frames, palette, path)
    return path


def write_map(name, rows):
    path = os.path.join(MAP_DIR, name + ".txt")
    with open(path, "w", encoding="utf-8") as f:
        for row in rows:
            f.write("".join(row) + "\n")
    return path


def provenance(prompt, seed=0):
    return {
        "tool": "procedural",
        "model": "",
        "prompt": prompt,
        "seed": seed,
        "licence": "this repository",
    }


def main():
    os.makedirs(STRIP_DIR, exist_ok=True)
    written = []

    # --- the sprites ------------------------------------------------------
    sky = build_tileset(sky_tiles(), 14, SKY_P, SEED + 1)
    tower = build_tileset(TOWER_TILES, 6, TOWER_P, SEED + 2)
    rock = build_tileset(ROCK_TILES, 4, ROCK_P, SEED + 3)
    sea = build_tileset(SEA_TILES, 3, SEA_P, SEED + 4)

    written.append(write_strip("sky", [sky], SKY_P))
    written.append(write_strip("tower", [tower], TOWER_P))
    written.append(write_strip("rock", [rock], ROCK_P))
    written.append(write_strip("sea", [sea], SEA_P))

    written.append(write_strip("keeper_idle", keeper_idle_frames(), KEEPER_P))
    written.append(write_strip("keeper_climb", keeper_climb_frames(), KEEPER_P))
    written.append(write_strip("keeper_carry", keeper_carry_frames(), KEEPER_P))

    written.append(write_strip("lamp_off", [lamp_frame(False, 0)], LAMP_P))
    written.append(write_strip("lamp_on",
                               [lamp_frame(True, 0), lamp_frame(True, 1)], LAMP_P))

    # --- the world layers -------------------------------------------------

    # --- the manifest -----------------------------------------------------
    doc = {
        "format": 1,
"roles": {
            "ground": "tileset", "sky": "tileset",
            "tower": "tileset", "rock": "tileset",
            "sea": "tileset",
        },
        "description": "A lighthouse's cast and tiles: the keeper (three states), the tower lamp, and four tilesets (a dusk sky, the tower, the cliff, the sea).",
        "palettes": {
            "sky16": {"bank": 0, "colors": list(SKY_P)},
            "tower16": {"bank": 1, "colors": list(TOWER_P)},
            "rock16": {"bank": 2, "colors": list(ROCK_P)},
            "sea16": {"bank": 3, "colors": list(SEA_P)},
            "keeper16": {"bank": 4, "colors": list(KEEPER_P)},
            "lamp16": {"bank": 5, "colors": list(LAMP_P)},
            "ui16": {"bank": 6, "colors": list(UI_P)},
        },
        "sprites": [
            {"name": "sky", "file": "strips/sky.png", "palette": "sky16",
             "frames": 1, "origin": [0, 0],
             "provenance": provenance("round 7: the dusk sky tiles "
                                      "(a gradient, stars, clouds, a moon)")},
            {"name": "tower", "file": "strips/tower.png",
             "palette": "tower16", "frames": 1, "origin": [0, 0],
             "provenance": provenance("round 7: the lighthouse tiles "
                                      "(wall, window, ladder, gallery, glass)")},
            {"name": "rock", "file": "strips/rock.png",
             "palette": "rock16", "frames": 1, "origin": [0, 0],
             "provenance": provenance("round 7: the cliff tiles")},
            {"name": "sea", "file": "strips/sea.png", "palette": "sea16",
             "frames": 1, "origin": [0, 0],
             "provenance": provenance("round 7: the sea tiles (crest, flat, "
                                      "deep, foam)")},
            {"name": "keeper_idle", "file": "strips/keeper_idle.png",
             "palette": "keeper16", "frames": 2, "origin": [12, 31],
             "collision": [6, 12, 12, 19],
             "provenance": provenance("round 7: the keeper, standing")},
            {"name": "keeper_climb", "file": "strips/keeper_climb.png",
             "palette": "keeper16", "frames": 4, "origin": [12, 31],
             "collision": [6, 12, 12, 19],
             "provenance": provenance("round 7: the keeper, climbing")},
            {"name": "keeper_carry", "file": "strips/keeper_carry.png",
             "palette": "keeper16", "frames": 2, "origin": [12, 31],
             "collision": [6, 12, 12, 19],
             "provenance": provenance("round 7: the keeper, carrying a "
                                      "lantern")},
            {"name": "lamp_off", "file": "strips/lamp_off.png",
             "palette": "lamp16", "frames": 1, "origin": [8, 8],
             "provenance": provenance("round 7: the tower lamp, dark")},
            {"name": "lamp_on", "file": "strips/lamp_on.png",
             "palette": "lamp16", "frames": 2, "origin": [8, 8],
             "provenance": provenance("round 7: the tower lamp, lit")},
        ],
        "animations": [
            {"name": "keeper_idle_anim", "sprite": "keeper_idle",
             "frames": [0, 1], "ms": 520,
             "provenance": provenance("round 7: the keeper's idle")},
            {"name": "keeper_climb_anim", "sprite": "keeper_climb",
             "frames": [0, 1, 2, 3], "ms": 130,
             "provenance": provenance("round 7: the keeper's climb")},
            {"name": "keeper_carry_anim", "sprite": "keeper_carry",
             "frames": [0, 1], "ms": 320,
             "provenance": provenance("round 7: the keeper's carry")},
            {"name": "lamp_on_anim", "sprite": "lamp_on", "frames": [0, 1],
             "ms": 140,
             "provenance": provenance("round 7: the lamp's flicker")},
        ],
        "maps": [],
    }

    manifest = os.path.join(_OUT, "lighthouse.json")
    with open(manifest, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
        f.write("\n")

    print("gen_round7: %d file(s) written" % (len(written) + 1))
    for path in written:
        print("  %s" % os.path.relpath(path, ROOT))
    print("  %s" % os.path.relpath(manifest, ROOT))


if __name__ == "__main__":
    main()
