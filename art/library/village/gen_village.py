#!/usr/bin/env python3
"""gen_round6.py — the round 6 village art.

The brief: *a fantasy village with a farmer, a slime enemy, 4 pickups and
a 3-tile ground*. This script paints that art with the repository's own
drawing library (tools/arc1_art/canvas.py) and writes the manifest the
asset compiler consumes.

Deterministic: no clock, no unseeded randomness, sorted iteration, fixed
seeds. The same inputs always give the same bytes. Stdlib only.

    python3 round6/art/gen_round6.py

It writes (and only writes — nothing here is ever hand-edited):

    round6/art/strips/farmer.png    the farmer, 2 walk frames, 16x24
    round6/art/strips/slime.png     the slime, 2 hop frames, 16x16
    round6/art/strips/coin.png      the 4 pickups, 1 frame each, 12x12
    round6/art/strips/apple.png
    round6/art/strips/carrot.png
    round6/art/strips/cabbage.png
    round6/art/strips/ground.png    the tileset: 3 tiles of 16x16, ONE frame
    round6/art/village.txt          the map: a grid of legend characters
    round6/art/village_solid.txt    the map's collision layer
    round6/assets.json              the manifest arc1-assetc compiles
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))          # round6/art
ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(os.path.dirname(HERE)))                                   # the repo root
sys.path.insert(0, os.path.join(ROOT, "tools"))

from arc1_art import canvas as C                            # noqa: E402

ROUND = HERE                               # round6
# the pack's output directory: --out-dir, or the pack itself
_OUT = HERE
if "--out-dir" in sys.argv:
    _OUT = os.path.abspath(sys.argv[sys.argv.index("--out-dir") + 1])
STRIP_DIR = os.path.join(_OUT, "strips")
MANIFEST = os.path.join(_OUT, "village.json")
MAP_FILE = os.path.join(HERE, "maps", "village.txt")
SOLID_FILE = os.path.join(HERE, "maps", "village_solid.txt")

SEED = 20261021                    # the one seed the tile texture uses
LICENCE = "this repository"
TOOL = "procedural"
PROMPT = "round6/art/gen_round6.py — the round 6 village generator"


# --------------------------------------------------------------------------
# The palettes — one bank per asset group (art/README.md rule 4).
#
# Index 0 is the transparent key, index 1 is the outline colour, 2..15 are
# the ramp. Every entry is distinct as RGB565, and index 1 is the darkest
# colour of its bank (the visual check needs that: the outline has to be
# the darkest thing in the frame).
# --------------------------------------------------------------------------

PALETTES = {
    # The farmer: skin, straw, indigo work clothes. (hero16, verbatim.)
    "farmer": [
        "#000000", "#101018", "#212442", "#313C73", "#4A59A5", "#B53839",
        "#732429", "#E7B28C", "#B57D63", "#101429", "#E7C35A", "#A57D31",
        "#21515A", "#31797B", "#EFF7FF", "#8CAAC6",
    ],
    # The slime: greens only, the forest preset. (forest16, verbatim.)
    "slime": [
        "#000000", "#101808", "#182810", "#293C18", "#315129", "#426531",
        "#4A7942", "#5A8E4A", "#6BA25A", "#7BB66B", "#8CCB7B", "#A5DF94",
        "#C6EFB5", "#6B4929", "#8C6539", "#AD8652",
    ],
    # The pickups: gold, red, orange, green and cream (custom — the presets
    # hold no red and no green together, and four pickups need both).
    "props": [
        "#000000", "#1A1218", "#7A5410", "#B8860F", "#E8B93A", "#F7E07A",
        "#8E1B22", "#C8302F", "#F0604F", "#A8500F", "#E07A1E", "#F2A94A",
        "#2C5A1E", "#4E8C2A", "#8FC65A", "#F5EEDD",
    ],
    # The ground: grass, path, water. (custom — the presets hold no water
    # and no earth together either.)
    "ground": [
        "#000000", "#1B1B22", "#2E5A22", "#3F7A2C", "#58A03A", "#7CC154",
        "#4A2F16", "#6B4520", "#8C6030", "#B08A4E", "#16324F", "#215080",
        "#3573AC", "#6BA3D6", "#A9D4E8", "#24461A",
    ],
}

BANKS = {"farmer": 1, "slime": 2, "props": 3, "ground": 4}

# The palette indices the drawing code names.
OUT = 1               # the outline colour, in every bank
F_SKIN, F_SKIN_SH, F_SHIRT, F_SHIRT_LT, F_SHIRT_DK = 7, 8, 3, 4, 2
F_HAT, F_HAT_SH, F_APRON, F_APRON_SH, F_BOOT, F_LEG = 10, 11, 14, 15, 9, 2
S_BODY, S_MID, S_DARK, S_LIGHT, S_HI, S_EYE = 6, 7, 4, 9, 12, 11
P_GOLD_D, P_GOLD, P_GOLD_LT, P_GOLD_HI = 2, 3, 4, 5
P_RED_D, P_RED, P_RED_LT = 6, 7, 8
P_BROWN, P_ORANGE, P_ORANGE_LT = 9, 10, 11
P_GREEN_D, P_GREEN, P_GREEN_LT, P_CREAM = 12, 13, 14, 15
G_OUT = 1
G_GRASS_D, G_GRASS, G_GRASS_LT, G_GRASS_HI = 2, 3, 4, 5
G_DIRT_D, G_DIRT, G_DIRT_LT, G_DIRT_HI = 6, 7, 8, 9
G_WATER_D, G_WATER, G_WATER_LT, G_WATER_HI = 10, 11, 12, 13


def provenance(seed=0):
    """The provenance block every asset carries (docs/art/PROVENANCE.md)."""
    return {"tool": TOOL, "model": "", "prompt": PROMPT, "seed": seed,
            "licence": LICENCE}


# --------------------------------------------------------------------------
# The farmer — 16x24, two walk frames.
#
# The light comes from the top left: the left column of the shirt is the
# light step, the right column the shadow step. The outline is 1 px and
# outside the silhouette; the feet stand on the same row in both frames.
# --------------------------------------------------------------------------

F_W, F_H = 16, 24


def farmer_frame(step):
    """One walk frame. step 0: the left leg forward; step 1: the right."""
    c = C.Canvas(F_W, F_H)

    # the hat: a straw crown on a wide brim
    c.rect(5, 2, 6, 2, F_HAT)
    c.rect(5, 3, 6, 1, F_HAT_SH)
    c.rect(2, 4, 12, 2, F_HAT_SH)

    # the head
    c.rect(5, 6, 6, 5, F_SKIN)
    c.rect(5, 6, 6, 1, F_SKIN_SH)       # the brim's shadow on the forehead
    c.rect(10, 7, 1, 4, F_SKIN_SH)      # the shadow side of the face
    c.px(6, 8, OUT)                     # the eyes
    c.px(9, 8, OUT)
    c.rect(7, 11, 2, 1, F_SKIN_SH)      # the neck

    # the torso: a work shirt with an apron
    c.rect(3, 12, 10, 6, F_SHIRT)
    c.rect(3, 12, 1, 6, F_SHIRT_LT)     # the lit side
    c.rect(12, 12, 1, 6, F_SHIRT_DK)    # the shadow side
    c.rect(3, 14, 2, 3, F_SKIN)         # the hands
    c.rect(11, 14, 2, 3, F_SKIN)
    c.rect(6, 13, 4, 5, F_APRON)        # the apron
    c.rect(9, 13, 1, 5, F_APRON_SH)     # its shadow side

    # the legs: the front one low, the back one lifted — but the lowest
    # opaque pixel stays on the same row in both frames
    if step == 0:
        c.rect(4, 18, 3, 3, F_LEG)
        c.rect(10, 17, 3, 3, F_LEG)
        c.rect(3, 21, 4, 2, F_BOOT)
        c.rect(10, 20, 4, 2, F_BOOT)
    else:
        c.rect(4, 17, 3, 3, F_LEG)
        c.rect(10, 18, 3, 3, F_LEG)
        c.rect(3, 20, 4, 2, F_BOOT)
        c.rect(10, 21, 4, 2, F_BOOT)

    c.outline(OUT)
    return c


# --------------------------------------------------------------------------
# The slime — 16x16, two hop frames (squash and stretch).
# --------------------------------------------------------------------------

S_W, S_H = 16, 16

# the rows of the blob: (y, x0, x1, colour index)
SLIME_SQUASH = [
    (7, 6, 9, S_LIGHT), (8, 4, 11, S_MID), (9, 3, 12, S_BODY),
    (10, 2, 13, S_BODY), (11, 2, 13, S_MID), (12, 2, 13, S_DARK),
    (13, 3, 12, S_DARK),
]
SLIME_STRETCH = [
    (4, 7, 8, S_LIGHT), (5, 5, 10, S_MID), (6, 4, 11, S_MID),
    (7, 3, 12, S_BODY), (8, 2, 13, S_BODY), (9, 2, 13, S_MID),
    (10, 2, 13, S_MID), (11, 2, 13, S_DARK), (12, 2, 13, S_DARK),
    (13, 3, 12, S_DARK),
]


def slime_frame(stretch):
    """One hop frame: the blob, shaded top to bottom, eyes and a mouth."""
    c = C.Canvas(S_W, S_H)
    rows = SLIME_STRETCH if stretch else SLIME_SQUASH
    for (y, x0, x1, col) in rows:
        c.rect(x0, y, x1 - x0 + 1, 1, col)
    # the shine, top left
    top = rows[2][0]
    c.rect(5, top, 2, 1, S_HI)
    c.rect(5, top + 1, 1, 1, S_HI)
    # the eyes: a pale sclera with a dark pupil, so they read at 1x
    c.rect(5, 8, 2, 3, S_EYE)
    c.rect(9, 8, 2, 3, S_EYE)
    c.px(5, 10, OUT)
    c.px(9, 10, OUT)
    # the mouth: a smile
    c.px(6, 11, OUT)
    c.px(9, 11, OUT)
    c.px(7, 12, OUT)
    c.px(8, 12, OUT)
    c.outline(OUT)
    return c


# --------------------------------------------------------------------------
# The pickups — 12x12, one frame each.
# --------------------------------------------------------------------------

P_W, P_H = 12, 12


def pickup_coin():
    c = C.Canvas(P_W, P_H)
    c.ellipse(6, 6, 4, 4, P_GOLD_D)     # the rim
    c.ellipse(6, 6, 3, 3, P_GOLD)       # the face
    c.ellipse(6, 6, 2, 2, P_GOLD_LT)
    c.rect(6, 4, 1, 4, P_GOLD_D)        # the mark on the face
    c.px(4, 4, P_GOLD_HI)               # the shine
    c.outline(OUT)
    return c


def pickup_apple():
    c = C.Canvas(P_W, P_H)
    c.rect(6, 2, 1, 2, P_BROWN)         # the stem
    c.rect(7, 2, 2, 1, P_GREEN)         # the leaf
    c.ellipse(6, 7, 3, 3, P_RED)        # the body
    c.px(4, 6, P_RED_LT)                # the shine
    c.px(5, 5, P_RED_LT)
    c.outline(OUT)
    return c


def pickup_carrot():
    c = C.Canvas(P_W, P_H)
    c.rect(5, 1, 1, 2, P_GREEN)         # the leaves
    c.rect(7, 1, 1, 2, P_GREEN)
    c.rect(6, 1, 1, 2, P_GREEN_LT)
    c.polygon([(4, 3), (9, 3), (6, 10)], P_ORANGE)   # the root
    c.px(5, 5, P_BROWN)                 # the ridges
    c.px(7, 6, P_BROWN)
    c.px(6, 8, P_BROWN)
    c.outline(OUT)
    return c


def pickup_cabbage():
    c = C.Canvas(P_W, P_H)
    c.rect(4, 2, 1, 2, P_GREEN_D)       # the outer leaves
    c.rect(7, 2, 1, 2, P_GREEN_D)
    c.ellipse(6, 7, 4, 3, P_GREEN_D)    # the head
    c.ellipse(6, 7, 3, 2, P_GREEN)
    c.px(4, 5, P_GREEN_LT)              # the shine
    c.outline(OUT)
    return c


# --------------------------------------------------------------------------
# The ground — the tileset: 3 tiles of 16x16 side by side, ONE frame.
#
# The map importer takes the tiles from the sprite's FIRST frame, so the
# tileset is one 48x16 frame (art/README.md: "a tileset is ONE frame").
#
# The dark border: the top and the bottom row of the strip are the outline
# colour, and so is the right-hand column (the pond's stone rim). The left
# column stays grass, so the field has no vertical seams.
# --------------------------------------------------------------------------

TILE = 16
TILES_W = TILE * 3


def draw_tileset():
    """The 48x16 tileset: grass, path, pond.

    The tiles are SEAMLESS where they must be: the grass flows into the
    grass, the path is a pavement of flagstones with a dark joint, and the
    pond is water with a wave in it. Nothing is drawn around the strip's
    border — a border would repeat in every cell of the map and turn the
    field into a quilt.
    """
    c = C.Canvas(TILES_W, TILE)
    rng = C.Rng(SEED)

    # --- tile 0: grass ---------------------------------------------------
    for y in range(TILE):
        c.rect(0, y, TILE, 1, G_GRASS)
    for i in range(9):                  # the blades
        x = rng.rand(TILE)
        y = rng.rand(TILE)
        c.px(x, y, G_GRASS_D)
        c.px(x, (y + 1) % TILE, G_GRASS_D)
    for i in range(5):                  # the light on the blades
        c.px(rng.rand(TILE), rng.rand(TILE), G_GRASS_LT)
    for i in range(2):                  # the dry tufts
        c.px(rng.rand(TILE), rng.rand(TILE), G_GRASS_HI)

    # --- tile 1: the path (flagstones with a dark joint) -----------------
    ox = TILE
    for y in range(TILE):
        c.rect(ox, y, TILE, 1, G_DIRT)
    c.rect(ox, 0, TILE, 1, G_DIRT_D)    # the joint
    c.rect(ox, 0, 1, TILE, G_DIRT_D)
    c.rect(ox + TILE - 1, 0, 1, TILE, G_DIRT_D)
    c.rect(ox, TILE - 1, TILE, 1, G_DIRT_D)
    for i in range(12):                 # the grit
        x = ox + 1 + rng.rand(TILE - 2)
        y = 1 + rng.rand(TILE - 2)
        c.px(x, y, G_DIRT_LT if i % 2 else G_DIRT_D)
    for i in range(3):
        c.px(ox + 1 + rng.rand(TILE - 2), 1 + rng.rand(TILE - 2), G_DIRT_HI)

    # --- tile 2: the pond ------------------------------------------------
    ox = TILE * 2
    for y in range(TILE):
        c.rect(ox, y, TILE, 1, G_WATER)
    for i in range(7):                  # the ripples
        x = ox + 1 + rng.rand(TILE - 4)
        y = rng.rand(TILE)
        c.rect(x, y, 2 + rng.rand(2), 1, G_WATER_LT)
    for i in range(3):                  # the shine on the water
        c.px(ox + rng.rand(TILE), rng.rand(TILE), G_WATER_HI)
    return c


# --------------------------------------------------------------------------
# The map — 30x16 cells of 16x16 (480x256, one screen of the 480x270
# display). The legend: g = grass, p = path, w = water.
# --------------------------------------------------------------------------

MAP_COLS, MAP_ROWS = 30, 16

MAP_ROWS_TEXT = [
    "gggggggggggggggggggggggggggggg",     # 0
    "ggwwwwwwgggggggggggggggggggggg",     # 1  the pond
    "ggwwwwwwgggggggggggggggggggggg",     # 2
    "gggggggggggggggggggggggggggggg",     # 3
    "gggggggggggggggggggggggggggggg",     # 4
    "gggggggggggggggggggggggggggggg",     # 5
    "gggggggggggggggggggggggggggggg",     # 6
    "ggggggggggggppppppppppppgggggg",     # 7  the village square
    "ggggggggggggppppppppppppgggggg",     # 8
    "ggggggggggggppppppppppppgggggg",     # 9
    "ggggggggggggppppppppppppgggggg",     # 10
    "ggggggggggggppppppppppppgggggg",     # 11
    "pppppppppppppppppppppppppppppp",       # 12 the road
    "gggggggggggggggggggggggggggggg",     # 13
    "gggggggggggggggggggggggggggggg",     # 14
    "gggggggggggggggggggggggggggggg",     # 15
]

# The collision layer: '#' solid, '.' empty, '^' hazard. The pond is the
# only solid thing — the farmer cannot walk into the water.
SOLID_TEXT = [
    "..............................",
    "..######......................",
    "..######......................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
]


# --------------------------------------------------------------------------
# Writing
# --------------------------------------------------------------------------

def write_strip(name, frames, palette_name):
    """Write one strip PNG and return its manifest entry."""
    path = os.path.join(STRIP_DIR, name + ".png")
    C.to_strip(frames, PALETTES[palette_name], path)
    print("  strip %s (%d frame(s) of %dx%d)"
          % (path, len(frames), frames[0].w, frames[0].h))
    return {
        "name": name,
        "file": "strips/%s.png" % name,
        "palette": palette_name,
        "frames": len(frames),
        "provenance": provenance(),
    }


def write_text(path, lines):
    with open(path, "w", encoding="utf-8") as f:
        for line in lines:
            f.write(line + "\n")
    print("  file %s (%d rows)" % (path, len(lines)))


def main():
    for row in MAP_ROWS_TEXT + SOLID_TEXT:
        assert len(row) == MAP_COLS, "a map row is %d wide, want %d" % (
            len(row), MAP_COLS)
    assert len(MAP_ROWS_TEXT) == MAP_ROWS
    assert len(SOLID_TEXT) == MAP_ROWS

    os.makedirs(STRIP_DIR, exist_ok=True)

    sprites = []

    farmer = write_strip("farmer", [farmer_frame(0), farmer_frame(1)],
                          "farmer")
    farmer["origin"] = [8, 24]              # the feet
    farmer["collision"] = [3, 12, 10, 11]   # the body, not the hat
    sprites.append(farmer)

    slime = write_strip("slime", [slime_frame(False), slime_frame(True)],
                         "slime")
    slime["origin"] = [8, 16]
    slime["collision"] = [3, 8, 10, 6]
    sprites.append(slime)

    for (name, maker, box) in (
            ("coin", pickup_coin, [2, 2, 8, 9]),
            ("apple", pickup_apple, [3, 4, 6, 7]),
            ("carrot", pickup_carrot, [4, 3, 5, 8]),
            ("cabbage", pickup_cabbage, [2, 4, 8, 7])):
        sprite = write_strip(name, [maker()], "props")
        sprite["origin"] = [6, 12]
        sprite["collision"] = box
        sprites.append(sprite)

    ground = write_strip("ground", [draw_tileset()], "ground")
    ground["origin"] = [0, 0]
    sprites.append(ground)

    animations = [
        {"name": "farmer_walk", "sprite": "farmer", "frames": [0, 1],
         "ms": 160, "provenance": provenance()},
        {"name": "slime_hop", "sprite": "slime", "frames": [0, 1],
         "ms": 240, "provenance": provenance()},
    ]

    maps = []


    doc = {
        "format": 1,
"roles": {
            "ground": "tileset", "sky": "tileset",
            "tower": "tileset", "rock": "tileset",
            "sea": "tileset",
        },
        "description": "A village's cast and ground: a farmer, a slime, four harvests and a three-tile ground (grass, a paved square, a pond).",
        "palettes": dict((name, {"bank": BANKS[name], "colors": colors})
                         for (name, colors) in PALETTES.items()),
        "sprites": sprites,
        "animations": animations,
        "maps": [],
    }
    with open(MANIFEST, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("  manifest %s (%d sprites, %d animations, %d map(s))"
          % (MANIFEST, len(sprites), len(animations), len(maps)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
