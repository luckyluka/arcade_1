#!/usr/bin/env python3
"""TEMPEST — art generator for the ARCADE-1 vertical shoot-'em-up.

Every sprite is painted procedurally (no PIL, stdlib only, fully
deterministic) as palette-index PNG strips, then the generator writes:

    art/*.png        horizontal sprite strips (assetc's contract)
    shmup_assets.json  the arc1-assetc manifest (palettes, sprites)

and the build compiles the manifest into the game's header:

    python3 tools/arc1_assetc.py --assets programs/shmup_art/shmup_assets.json \
        --out-dir programs --report

Options:
    --dir PATH       output directory (default: this script's directory)
    --preview P      write a contact sheet PNG of every sprite
    --manifest NAME  the manifest file name (default: shmup_assets.json)

The art follows the library's rules (art/README.md): index 0 is the
transparent key and is never painted, index 1 is the outline (the darkest
colour in the bank), 2..15 are the ramp, and every outline is painted
OUTSIDE the silhouette so the machine's outline check passes.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)))), "tools"))
from arc1_art.canvas import (Canvas as G, Rng, to_strip, contact_sheet)  # noqa: E402


# --------------------------------------------------------------------------
# RGB565 palette components. Authored as (r5, g6, b5) — the device's own
# 5/6/5 — and expanded to #RRGGBB for the PNG, which assetc maps back to the
# same components exactly (the expansion is injective).
# --------------------------------------------------------------------------

def h2c(s):
    s = s.lstrip("#")
    r, g, b = int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)
    return (r >> 3, g >> 2, b >> 3)


def H(s):
    return h2c(s)


PALETTES = {
    # the player: a cyan-steel interceptor
    "hero": (0, [None,
                 H("#0A1620"), H("#16344A"), H("#245C7E"), H("#3E9AC4"),
                 H("#7FE0F0"), H("#E8F8FF"), H("#FFC24A"), H("#FF7A2A"),
                 H("#0E2436"), H("#A8E8FF"), H("#5A7A8C"), H("#2A4A5E"),
                 H("#FFFFFF"), H("#1A2C3A"), H("#7A3A18")]),
    # the enemy air units: crimson and hot orange
    "foe": (1, [None,
                H("#1A0C10"), H("#5C1620"), H("#96262A"), H("#C93A32"),
                H("#F06A38"), H("#FFC46A"), H("#E8E4D8"), H("#6A6A72"),
                H("#A8AEB8"), H("#2A2A34"), H("#3E4A56"), H("#FFD98A"),
                H("#7A1E18"), H("#B0B8C4"), H("#501018")]),
    # the ground units: olive and steel
    "ground": (2, [None,
                   H("#10140E"), H("#2E3A20"), H("#4A5A2E"), H("#6E8040"),
                   H("#8A9A5A"), H("#3A4048"), H("#5A646E"), H("#8A949E"),
                   H("#B0B8C0"), H("#C8B070"), H("#8A7040"), H("#E0D8B0"),
                   H("#1E2418"), H("#6E3A20"), H("#A8B090")]),
    # the shots: the player's cyan bolts, the foes' orange ones
    "shot": (3, [None,
                 H("#081018"), H("#104050"), H("#1A7090"), H("#30B0D8"),
                 H("#70E8F8"), H("#E8FFFF"), H("#E0A020"), H("#F06020"),
                 H("#C03018"), H("#FFD060"), H("#A01810"), H("#2A90B0"),
                 H("#0A2838"), H("#F8F0C0"), H("#903010")]),
    # the explosions and the pickups
    "fx": (4, [None,
               H("#1A1014"), H("#7A2410"), H("#C04A18"), H("#F08020"),
               H("#FFC040"), H("#FFF0C0"), H("#E8E8F0"), H("#9098A8"),
               H("#40C060"), H("#80E890"), H("#4060D0"), H("#90C0FF"),
               H("#3A1A20"), H("#C0C8D0"), H("#E04060")]),
    # the debris: grey stone with brown earth
    "rock": (5, [None,
                 H("#14141A"), H("#3A3A44"), H("#5A5A66"), H("#7E7E8A"),
                 H("#A0A0AA"), H("#6A5A4A"), H("#8A7258"), H("#2A2A32"),
                 H("#C0B49A"), H("#46464F"), H("#8E8E96"), H("#E0E0E8"),
                 H("#22222A"), H("#564C40"), H("#A89880")]),
    # the boss: a violet carrier with crimson trim and a gold spine
    "boss": (6, [None,
                 H("#120E1C"), H("#2A2440"), H("#443C68"), H("#6258A0"),
                 H("#8C82D8"), H("#C8C0FF"), H("#B03040"), H("#E05060"),
                 H("#3A3A48"), H("#6A6A80"), H("#A0A0B8"), H("#F0D060"),
                 H("#FF9060"), H("#60E0E0"), H("#8A1020")]),
    # the star field
    "sky": (7, [None,
                H("#06080E"), H("#1A2438"), H("#2A3A54"), H("#405A78"),
                H("#6A86A8"), H("#9AB4D0"), H("#D8E4F0"), H("#FFFFFF"),
                H("#102030"), H("#24344C"), H("#35496A"), H("#5878A0"),
                H("#8FA8C8"), H("#C0D0E0"), H("#6A7A94")]),
}


def C(name, i):
    """Palette colour i of palette `name` as (r5, g6, b5)."""
    return PALETTES[name][1][i]


# Shorthand for the palette indices each drawing routine uses. Index 0 is
# the transparent key and is never painted.
(O, D1, D2, D3, D4, D5, L1, L2, L3, M1, M2, M3, H1, H2, H3) = range(1, 16)

# The palettes' roles, so the drawing code reads like a sketch.
HERO = "hero"
FOE = "foe"
GND = "ground"
SHT = "shot"
FX = "fx"
RCK = "rock"
BOS = "boss"
SKY = "sky"


def idx(name, i):
    """The palette INDEX of colour i in palette `name`.

    A canvas holds palette *indices*, not colours: the strip writer turns
    an index into the bank's colour when it writes the PNG. Every palette
    here uses the same numbering (1 = the outline, 2..15 = the ramp), so
    one set of index constants serves them all.
    """
    assert PALETTES[name][1][i] is not None, (name, i)
    return i


# --------------------------------------------------------------------------
# The player: a delta-wing interceptor, seen from above, nose up.
# --------------------------------------------------------------------------

def draw_player(g, thrust=0):
    """The player's ship, 24x24, nose up.

    `thrust` brightens the engine flame — the two frames are the same
    shape on purpose: the machine's baseline rule wants every frame of a
    state to stand on the same row, and a flame that grew would break it.
    """
    # --- wings (behind the fuselage) -------------------------------------
    g.polygon([(9, 9), (1, 18), (4, 20), (11, 13)], idx(HERO, 2))
    g.polygon([(14, 9), (22, 18), (19, 20), (12, 13)], idx(HERO, 2))
    g.line(3, 18, 8, 12, idx(HERO, 3), 1)
    g.line(20, 18, 15, 12, idx(HERO, 3), 1)
    # wingtip lights
    g.px(2, 18, idx(HERO, 5))
    g.px(21, 18, idx(HERO, 5))

    # --- fuselage ---------------------------------------------------------
    g.polygon([(12, 2), (15, 9), (16, 18), (8, 18), (9, 9)], idx(HERO, 3))
    g.polygon([(12, 3), (14, 9), (12, 18), (10, 9)], idx(HERO, 4))
    g.vline(11, 6, 16, idx(HERO, 2))
    g.vline(12, 6, 16, idx(HERO, 5))
    # nose
    g.px(12, 2, idx(HERO, 6))
    g.px(11, 3, idx(HERO, 5))
    g.px(13, 3, idx(HERO, 5))

    # --- canopy -----------------------------------------------------------
    g.ellipse(12, 9, 2, 3, idx(HERO, 9))
    g.ellipse(12, 9, 1, 2, idx(HERO, 10))
    g.px(12, 8, idx(HERO, 13))

    # --- the engine and its flame ----------------------------------------
    g.box(10, 18, 13, 19, idx(HERO, 2))
    g.box(11, 20, 12, 22, idx(HERO, 8))          # the flame
    g.vline(11, 20, 21, idx(HERO, 7))
    if thrust:
        g.px(11, 22, idx(HERO, 6))
        g.px(12, 22, idx(HERO, 6))
    g.outline(idx(HERO, 1))


# --------------------------------------------------------------------------
# The enemy air units: seen head-on, coming down at the player.
# --------------------------------------------------------------------------

def draw_fighter(g):
    """A small enemy fighter, 16x16, nose down."""
    # wings
    g.polygon([(6, 7), (1, 2), (3, 1), (8, 4)], idx(FOE, D2))
    g.polygon([(9, 7), (14, 2), (12, 1), (7, 4)], idx(FOE, D2))
    g.line(2, 2, 6, 5, idx(FOE, D3), 1)
    g.line(13, 2, 9, 5, idx(FOE, D3), 1)
    # fuselage
    g.polygon([(8, 14), (11, 8), (10, 3), (5, 3), (6, 8)], idx(FOE, D3))
    g.polygon([(8, 13), (10, 8), (8, 4), (6, 8)], idx(FOE, D4))
    g.px(8, 12, idx(FOE, H1))
    # canopy
    g.ellipse(8, 5, 1, 2, idx(FOE, L3))
    # engine flare
    g.box(7, 1, 8, 2, idx(FOE, H1))
    g.px(7, 0, idx(FOE, H2))
    g.outline(idx(FOE, O))


def draw_drone(g):
    """A small diving drone, 12x12."""
    g.ellipse(5, 6, 3, 3, idx(FOE, D3))
    g.ellipse(5, 6, 2, 2, idx(FOE, D4))
    g.polygon([(3, 8), (0, 11), (2, 11)], idx(FOE, D2))
    g.polygon([(7, 8), (10, 11), (8, 11)], idx(FOE, D2))
    g.ellipse(5, 5, 1, 1, idx(FOE, H2))          # the red eye
    g.px(5, 9, idx(FOE, H1))
    g.outline(idx(FOE, O))


def draw_copter(g):
    """An attack helicopter, 32x24, seen from the side."""
    # rotor
    g.box(2, 3, 29, 4, idx(FOE, L3))
    g.box(4, 2, 26, 2, idx(FOE, L2))
    g.vline(16, 4, 8, idx(FOE, M1))
    # tail boom and rotor
    g.polygon([(20, 11), (29, 6), (29, 9), (21, 14)], idx(FOE, D3))
    g.vline(28, 1, 8, idx(FOE, M2))
    g.px(28, 0, idx(FOE, L3))
    # cabin
    g.ellipse(13, 12, 7, 5, idx(FOE, D3))
    g.ellipse(12, 11, 4, 3, idx(FOE, D4))
    g.ellipse(10, 11, 2, 2, idx(FOE, L3))         # the canopy glass
    g.px(9, 10, idx(FOE, H2))
    # stub wing and the gun
    g.box(6, 15, 16, 16, idx(FOE, D2))
    g.box(8, 16, 9, 19, idx(FOE, M2))
    g.box(9, 18, 10, 19, idx(FOE, M1))
    # skids
    g.hline(20, 6, 13, idx(FOE, M2))
    g.hline(20, 16, 23, idx(FOE, M2))
    g.vline(9, 16, 19, idx(FOE, M1))
    g.vline(19, 16, 19, idx(FOE, M1))
    g.outline(idx(FOE, O))


def draw_bomber(g):
    """A heavy bomber, 48x32, head-on."""
    # the wings: swept out and slightly up, with a lit leading edge
    g.polygon([(17, 12), (3, 8), (1, 13), (17, 17)], idx(FOE, D2))
    g.polygon([(30, 12), (44, 8), (46, 13), (30, 17)], idx(FOE, D2))
    g.line(3, 9, 16, 12, idx(FOE, D4), 1)
    g.line(44, 9, 31, 12, idx(FOE, D4), 1)
    g.px(2, 12, idx(FOE, H2))
    g.px(45, 12, idx(FOE, H2))
    # the engine nacelles on the wings
    for ex in (11, 36):
        g.ellipse(ex, 13, 3, 4, idx(FOE, M1))
        g.ellipse(ex, 13, 1, 2, idx(FOE, D1))
    # the fuselage
    g.ellipse(23, 18, 7, 12, idx(FOE, D3))
    g.ellipse(23, 18, 4, 9, idx(FOE, D4))
    g.polygon([(23, 30), (26, 25), (20, 25)], idx(FOE, D4))
    g.px(23, 29, idx(FOE, H1))
    # the cockpit
    g.ellipse(23, 9, 3, 2, idx(FOE, L3))
    g.px(21, 8, idx(FOE, H2))
    # the bomb bay
    g.box(20, 21, 26, 26, idx(FOE, D1))
    g.hline(21, 20, 26, idx(FOE, M1))
    g.hline(25, 20, 26, idx(FOE, M1))
    g.outline(idx(FOE, O))


def draw_cruiser(g):
    """A large enemy cruiser, 64x40, head-on."""
    # the hull: a wide wedge, narrower at the bottom
    g.polygon([(32, 3), (50, 14), (60, 30), (56, 38), (8, 38), (4, 30),
               (14, 14)], idx(FOE, D2))
    g.polygon([(32, 8), (46, 16), (54, 28), (10, 28), (18, 16)],
              idx(FOE, D3))
    # the stepped superstructure
    g.box(22, 16, 42, 24, idx(FOE, D3))
    g.box(26, 10, 38, 17, idx(FOE, D4))
    g.box(28, 6, 36, 11, idx(FOE, D5))
    g.box(29, 7, 35, 10, idx(FOE, L3))          # the bridge windows
    g.hline(12, 26, 37, idx(FOE, H2))
    # the side pods
    for px in (11, 52):
        g.ellipse(px, 27, 8, 9, idx(FOE, D2))
        g.ellipse(px, 26, 5, 6, idx(FOE, D3))
        g.ellipse(px, 24, 2, 2, idx(FOE, M1))
        g.box(px - 2, 31, px + 2, 34, idx(FOE, M1))
    # the gun turrets
    for tx in (21, 43):
        g.ellipse(tx, 20, 3, 3, idx(FOE, M1))
        g.box(tx - 1, 13, tx, 18, idx(FOE, D1))
        g.px(tx, 12, idx(FOE, H2))
    # the engine blocks
    for ex in (16, 26, 36, 46):
        g.box(ex, 33, ex + 4, 38, idx(FOE, M1))
        g.box(ex + 1, 34, ex + 3, 38, idx(FOE, H2))
    g.outline(idx(FOE, O))


# --------------------------------------------------------------------------
# The ground units: they sit on the terrain band and shoot up.
# --------------------------------------------------------------------------

def draw_turret(g):
    """An anti-aircraft turret, 16x16, barrel up."""
    g.box(3, 10, 12, 14, idx(GND, D2))              # the base
    g.box(4, 11, 11, 14, idx(GND, M2))
    g.ellipse(7, 8, 5, 4, idx(GND, D3))             # the dome
    g.ellipse(7, 8, 3, 2, idx(GND, D4))
    g.box(6, 1, 8, 6, idx(GND, M2))                 # the barrel
    g.box(6, 1, 7, 5, idx(GND, L2))
    g.px(6, 0, idx(GND, H1))
    g.outline(idx(GND, O))


def draw_tank(g):
    """A ground vehicle, 24x12, barrel up."""
    g.box(1, 4, 22, 11, idx(GND, D1))               # the treads
    g.box(2, 5, 21, 10, idx(GND, M2))
    for wx in range(3, 21, 4):
        g.px(wx, 7, idx(GND, L2))
    g.box(3, 2, 15, 6, idx(GND, D3))                # the hull
    g.box(4, 3, 14, 5, idx(GND, D4))
    g.ellipse(9, 4, 3, 2, idx(GND, D2))             # the turret
    g.box(8, 0, 9, 3, idx(GND, M2))                 # the barrel
    g.outline(idx(GND, O))


def draw_launcher(g):
    """A missile launcher, 24x12: three tubes angled up."""
    g.box(1, 6, 22, 11, idx(GND, D1))             # the hull
    g.box(2, 6, 21, 10, idx(GND, D2))
    g.hline(6, 2, 21, idx(GND, D3))
    for i, x in enumerate((3, 9, 15)):
        g.box(x, 2 + i, x + 3, 7, idx(GND, D1))   # the tube
        g.vline(x, 2 + i, 6, idx(GND, D3))
        g.vline(x + 3, 3 + i, 6, idx(GND, M1))
        g.px(x + 1, 1 + i, idx(GND, H2))           # the ready missile
        g.px(x + 2, 1 + i, idx(GND, H1))
    g.outline(idx(GND, O))


# --------------------------------------------------------------------------
# The debris, the boss and the small pieces.
# --------------------------------------------------------------------------

def draw_debris(g):
    """A tumbling chunk of rock, 16x16."""
    pts = [(3, 4), (7, 1), (12, 3), (14, 8), (11, 13), (5, 14), (1, 9)]
    g.polygon(pts, idx(RCK, D2))
    g.polygon([(5, 5), (8, 3), (11, 5), (10, 9), (6, 9)], idx(RCK, D3))
    g.polygon([(6, 6), (9, 5), (9, 8), (6, 9)], idx(RCK, D4))
    g.px(4, 11, idx(RCK, D1))
    g.px(12, 6, idx(RCK, H1))
    g.outline(idx(RCK, O))


def draw_boss(g, damaged=False):
    """The boss: a violet carrier, 120x88, head-on.

    The silhouette does the work: a pointed prow, a stepped tower, two
    long side pods and a row of engines along the bottom.
    """
    # the hull: a wide wedge with a pointed prow
    g.polygon([(60, 2), (84, 16), (96, 38), (98, 66), (86, 84), (34, 84),
               (22, 66), (24, 38), (36, 16)], idx(BOS, D2))
    g.polygon([(60, 8), (79, 20), (89, 40), (90, 64), (80, 78), (40, 78),
               (30, 64), (31, 40), (41, 20)], idx(BOS, D3))
    # the side pods: long blades running down the hull's flanks
    g.polygon([(26, 30), (16, 44), (14, 74), (26, 78), (32, 50)],
              idx(BOS, D2))
    g.polygon([(94, 30), (104, 44), (106, 74), (94, 78), (88, 50)],
              idx(BOS, D2))
    g.polygon([(27, 36), (21, 48), (20, 68), (25, 70), (29, 50)],
              idx(BOS, D3))
    g.polygon([(93, 36), (99, 48), (100, 68), (95, 70), (91, 50)],
              idx(BOS, D3))
    # the tower: a stepped pyramid up the middle
    g.polygon([(50, 12), (70, 12), (74, 44), (46, 44)], idx(BOS, D3))
    g.polygon([(52, 16), (68, 16), (71, 40), (49, 40)], idx(BOS, D4))
    g.polygon([(55, 18), (65, 18), (67, 30), (53, 30)], idx(BOS, M1))
    # the bridge
    g.box(52, 6, 68, 15, idx(BOS, L2))
    g.box(54, 8, 66, 13, idx(BOS, D4))
    for x in range(55, 66, 3):
        g.vline(x, 9, 12, idx(BOS, L1))
    g.hline(6, 52, 67, idx(BOS, D4))
    # the crimson trim
    g.hline(46, 40, 79, idx(BOS, D1))
    g.hline(47, 40, 79, idx(BOS, D1))
    g.hline(76, 36, 83, idx(BOS, D1))
    g.hline(77, 36, 83, idx(BOS, D1))
    # the launch bays
    for bx in (36, 74):
        g.box(bx, 58, bx + 10, 74, idx(BOS, D1))
        g.box(bx + 1, 60, bx + 9, 72, idx(BOS, M2))
        g.hline(61, bx + 1, bx + 8, idx(BOS, M1))
    # the turrets
    for tx in (30, 60, 90):
        g.ellipse(tx, 52, 4, 4, idx(BOS, M1))
        g.box(tx - 1, 40, tx, 49, idx(BOS, D1))
        g.px(tx, 39, idx(BOS, H2))
    # the engines
    for ex in (34, 48, 62, 76):
        g.box(ex, 78, ex + 10, 86, idx(BOS, M1))
        g.box(ex + 2, 79, ex + 7, 86, idx(BOS, H2))
    if damaged:
        # the battle damage: smoke stains, kept clear of the edges so the
        # silhouette stays one piece
        g.ellipse(46, 36, 5, 4, idx(BOS, D1))
        g.ellipse(66, 52, 4, 4, idx(BOS, D1))
        g.line(64, 34, 70, 40, idx(BOS, D1), 1)
    g.outline(idx(BOS, O))


def draw_boss_core(g, hot=False):
    """The boss's core — the weak point, 16x16.

    The core is a solid disc: the pulsing frame paints a wider glow ring
    around it, but the silhouette must stay ONE piece (the machine's
    outline check counts the pieces).
    """
    # both frames are the same size: the core pulses in COLOUR, not in
    # size (the baseline rule wants the frames to stand on one row)
    g.ellipse(7, 7, 6, 6, idx(BOS, 13 if hot else 2))
    g.ellipse(7, 7, 5, 5, idx(BOS, 14))
    g.ellipse(7, 7, 3, 3, idx(BOS, 5 if hot else 15))
    g.outline(idx(BOS, 1))


def draw_boom(g, frame):
    """An explosion, 24x24, four frames."""
    r = (2, 5, 8, 10)[frame]
    if frame == 0:
        g.ellipse(11, 11, 2, 2, idx(FX, L2))
    elif frame == 1:
        g.ellipse(11, 11, 4, 4, idx(FX, H1))
        g.ellipse(11, 11, 2, 2, idx(FX, L1))
    elif frame == 2:
        g.ellipse(11, 11, 7, 7, idx(FX, H1))
        g.ellipse(11, 11, 4, 4, idx(FX, H1))
        g.ellipse(11, 11, 2, 2, idx(FX, L1))
        for a in range(8):
            import math
            ang = a * math.pi / 4.0
            g.px(int(11 + 9 * math.cos(ang)), int(11 + 9 * math.sin(ang)),
                 idx(FX, H2))
    else:
        g.ellipse(11, 11, 10, 10, idx(FX, M2))
        g.ellipse(11, 11, 7, 7, idx(FX, M2))
        g.ellipse(11, 11, 4, 4, idx(FX, D2))
        for a in range(10):
            import math
            ang = a * math.pi / 3.0
            g.px(int(11 + 10 * math.cos(ang)), int(11 + 10 * math.sin(ang)),
                 idx(FX, D2))
    g.outline(idx(FX, O))


def draw_pickup_power(g):
    """A weapon power-up, 12x12: a green pod with a chevron.

    The fx bank's colours: 9 = green, 10 = light green, 6 = hot white.
    """
    g.ellipse(5, 5, 5, 5, idx(FX, 9))
    g.ellipse(5, 5, 4, 4, idx(FX, 10))
    g.ellipse(5, 5, 3, 3, idx(FX, 9))
    g.line(3, 6, 5, 3, idx(FX, 6), 1)
    g.line(5, 3, 7, 6, idx(FX, 6), 1)
    g.outline(idx(FX, O))


def draw_pickup_shield(g):
    """A shield power-up, 12x12: a blue ring (fx bank: 11 blue, 12 pale)."""
    g.ellipse(5, 5, 5, 5, idx(FX, 11))
    g.ellipse(5, 5, 4, 4, idx(FX, 12))
    g.ellipse(5, 5, 2, 2, idx(FX, O))
    g.px(5, 1, idx(FX, 12))
    g.px(5, 9, idx(FX, 12))
    g.outline(idx(FX, O))


# --------------------------------------------------------------------------
# The shots and the small pieces.
# --------------------------------------------------------------------------

def draw_pshot(g):
    """The player's plasma bolt, 6x12."""
    g.box(2, 0, 3, 11, idx(SHT, D3))
    g.box(2, 1, 3, 8, idx(SHT, D4))
    g.vline(2, 3, 9, idx(SHT, L1))
    g.box(1, 9, 4, 11, idx(SHT, L2))
    g.outline(idx(SHT, O))


def draw_fshot(g):
    """An enemy shot, 6x6: a hot orange bead."""
    g.ellipse(2, 2, 2, 2, idx(SHT, D2))
    g.ellipse(2, 2, 1, 1, idx(SHT, L1))
    g.outline(idx(SHT, O))


def draw_missile(g):
    """A homing missile, 8x8, nose down."""
    g.polygon([(4, 7), (6, 4), (5, 0), (2, 0), (3, 4)], idx(FOE, L3))
    g.polygon([(4, 6), (5, 4), (3, 4)], idx(FOE, D1))
    g.px(1, 3, idx(FOE, H2))
    g.px(6, 3, idx(FOE, H2))
    g.outline(idx(FOE, O))


def draw_ground(g):
    """The terrain band, 480x56 — the ground the fight happens over.

    The band is static: the world's motion comes from the star field and
    the air enemies, and the ground units work their own ground line. So
    this is a landscape, not a scrolling texture — a lit ridge edge, some
    towers and pylons standing on it, and dark ground below.
    """
    g.box(0, 4, 479, 55, idx(GND, D1))
    # the lit ridge edge
    g.box(0, 0, 479, 3, idx(GND, D2))
    g.hline(0, 0, 479, idx(GND, D3))
    g.hline(3, 0, 479, idx(GND, D1))
    # the towers along the ridge
    rng = Rng(0x7A11)
    x = 0
    while x < 480:
        w = 6 + rng.rand(10)
        h = 6 + rng.rand(16)
        g.box(x, 4 - h, x + w - 1, 4, idx(GND, D2))
        g.vline(x, 4 - h, 4, idx(GND, D3))
        g.vline(x + w - 1, 4 - h, 4, idx(GND, M1))
        if h > 12:
            g.box(x + 1, 4 - h + 2, x + w - 2, 4 - h + 3, idx(GND, D1))
            g.px(x + w / 2, 4 - h - 1, idx(GND, H2))     # the beacon
        x += w + 4 + rng.rand(20)
    # the ground: dust, rocks and the pylon lines
    for _ in range(90):
        px = rng.rand(480)
        py = 5 + rng.rand(50)
        g.px(px, py, idx(GND, D2))
    for px in range(6, 480, 74):
        g.box(px, 6, px + 3, 7, idx(GND, M1))
        g.vline(px + 1, 8, 20, idx(GND, D2))
        g.box(px, 20, px + 3, 22, idx(GND, M1))
    for _ in range(12):
        px = rng.rand(478)
        py = 30 + rng.rand(24)
        g.box(px, py, px + 2 + rng.rand(4), py + 1, idx(GND, D2))
        g.hline(py, px, px + 2, idx(GND, D1))


# --------------------------------------------------------------------------
# The sprite table: name -> (frames, palette, origin, collision)
# --------------------------------------------------------------------------

def build_sprites():
    """Every sprite of the game, as a list of palette-index frames."""
    out = {}

    def add(name, frames, pal_name, w, h, origin=None, collision=None,
            role="sprite"):
        out[name] = {
            "frames": frames,
            "palette": pal_name,
            "w": w, "h": h,
            "origin": origin if origin else [w // 2, h],
            "collision": collision,
            "role": role,
        }

    # the player: 24x24, nose up. The anchor is the ship's centre-ish: the
    # hitbox is a 4x4 box at the cockpit, so the anchor is the ship's middle.
    p1 = G(24, 24); draw_player(p1, 0)
    p2 = G(24, 24); draw_player(p2, 2)
    add("player", [p1, p2], HERO, 24, 24, [12, 12], [10, 8, 4, 4])

    f = G(16, 16); draw_fighter(f)
    add("fighter", [f], FOE, 16, 16, [8, 8], [3, 3, 10, 10])

    d = G(12, 12); draw_drone(d)
    add("drone", [d], FOE, 12, 12, [6, 6], [3, 3, 6, 6])

    c = G(32, 24); draw_copter(c)
    add("copter", [c], FOE, 32, 24, [16, 12], [4, 4, 24, 16])

    b = G(48, 32); draw_bomber(b)
    add("bomber", [b], FOE, 48, 32, [24, 16], [6, 4, 36, 24])

    cr = G(64, 40); draw_cruiser(cr)
    add("cruiser", [cr], FOE, 64, 40, [32, 20], [8, 6, 48, 28])

    t = G(16, 16); draw_turret(t)
    add("turret", [t], GND, 16, 16, [8, 14], [2, 6, 12, 10])

    tk = G(24, 12); draw_tank(tk)
    add("tank", [tk], GND, 24, 12, [12, 10], [2, 2, 20, 10])

    la = G(24, 12); draw_launcher(la)
    add("launcher", [la], GND, 24, 12, [12, 10], [2, 2, 20, 10])

    db = G(16, 16); draw_debris(db)
    add("debris", [db], RCK, 16, 16, [8, 8], [3, 3, 10, 10])

    bo = G(24, 24)
    frames = []
    for i in range(4):
        g = G(24, 24)
        draw_boom(g, i)
        frames.append(g)
    add("boom", frames, FX, 24, 24, [12, 12], None, "effect")

    pw = G(12, 12); draw_pickup_power(pw)
    add("pickup_power", [pw], FX, 12, 12, [6, 6], [1, 1, 10, 10])

    sh = G(12, 12); draw_pickup_shield(sh)
    add("pickup_shield", [sh], FX, 12, 12, [6, 6], [1, 1, 10, 10])

    bs = G(120, 88); draw_boss(bs, False)
    bs2 = G(120, 88); draw_boss(bs2, True)
    add("boss", [bs, bs2], BOS, 120, 88, [60, 44], [8, 8, 104, 72])

    bc = G(16, 16); draw_boss_core(bc, False)
    bc2 = G(16, 16); draw_boss_core(bc2, True)
    add("boss_core", [bc, bc2], BOS, 16, 16, [8, 8], [3, 3, 10, 10])

    ps = G(6, 12); draw_pshot(ps)
    add("pshot", [ps], SHT, 6, 12, [3, 12], [1, 2, 4, 10])

    fs = G(6, 6); draw_fshot(fs)
    add("fshot", [fs], SHT, 6, 6, [3, 3], [1, 1, 4, 4], "effect")

    ms = G(8, 8); draw_missile(ms)
    add("missile", [ms], FOE, 8, 8, [4, 4], [2, 2, 4, 4])

    gr = G(480, 56); draw_ground(gr)
    add("ground", [gr], GND, 480, 56, [0, 0], None, "layer")

    return out


# --------------------------------------------------------------------------
# Output
# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--preview", default=None)
    ap.add_argument("--manifest", default="shmup_assets.json")
    args = ap.parse_args()

    out_dir = os.path.abspath(args.dir)
    art_dir = os.path.join(out_dir, "art")
    os.makedirs(art_dir, exist_ok=True)

    sprites = build_sprites()

    manifest = {"format": 1, "palettes": {}, "roles": {}, "sprites": [],
                "animations": [], "maps": []}
    for name, (bank, colours) in sorted(PALETTES.items()):
        # index 0 is the transparent key; assetc wants it written out
        manifest["palettes"][name] = {
            "bank": bank,
            "colors": ["#000000"] + ["#%02X%02X%02X" % (
                (c[0] << 3) | (c[0] >> 2),
                (c[1] << 2) | (c[1] >> 4),
                (c[2] << 3) | (c[2] >> 2)) for c in colours[1:]],
        }

    for name in sorted(sprites):
        s = sprites[name]
        path = os.path.join(art_dir, "%s.png" % name)
        to_strip(s["frames"], PALETTES[s["palette"]][1], path)
        entry = {
            "name": name,
            "file": "art/%s.png" % name,
            "palette": s["palette"],
            "frames": len(s["frames"]),
            "origin": s["origin"],
            "provenance": {
                "tool": "procedural",
                "model": "",
                "prompt": "the shmup's art: programs/shmup_art/gen_art.py",
                "seed": 0,
                "licence": "this repository",
            },
        }
        if s["collision"]:
            entry["collision"] = s["collision"]
        manifest["sprites"].append(entry)
        if s["role"] != "sprite":
            manifest["roles"][name] = s["role"]
        print("shmup-art: %-14s %dx%d x%d (%s)"
              % (name, s["w"], s["h"], len(s["frames"]), s["palette"]))

    mpath = os.path.join(out_dir, args.manifest)
    with open(mpath, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("shmup-art: %s" % mpath)

    if args.preview:
        items = []
        for name in sorted(sprites):
            s = sprites[name]
            items.append((s["frames"][0], name))
        contact_sheet(items, scale=3, path=args.preview)
        print("shmup-art: preview %s" % args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
