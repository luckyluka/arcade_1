#!/usr/bin/env python3
"""BRICKSTORM — art generator for the ARCADE-1 breakout remake.

Every sprite is painted procedurally (no PIL, stdlib only, fully
deterministic) as palette-index PNG strips, then the generator writes:

    art/*.png          horizontal sprite strips (assetc's contract)
    shmup-style json   the arc1-assetc manifest (palettes, sprites)

Options:
    --dir PATH       output directory (default: this script's directory)
    --preview P      write a contact sheet PNG of every sprite
    --manifest NAME  the manifest file name (default: brickstorm_assets.json)

The rules (art/README.md): index 0 is the transparent key and is never
painted, index 1 is the outline (the darkest colour in the bank), 2..15
are the ramp, and every outline is painted OUTSIDE the silhouette so the
machine's outline check passes.
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
# RGB565 palette components, authored as (r5, g6, b5) — the device's own
# 5/6/5 — and expanded to #RRGGBB for the PNG.
# --------------------------------------------------------------------------

def h2c(s):
    s = s.lstrip("#")
    r, g, b = int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)
    return (r >> 3, g >> 2, b >> 3)


def H(s):
    return h2c(s)


PALETTES = {
    # the bricks: one bank per row colour, so a row is a colour and the
    # score follows the row
    "row0": (0, [None,
                 H("#180A10"), H("#7A1A2A"), H("#B82838"), H("#E8485C"),
                 H("#FF8A94"), H("#FFD0D4"), H("#FFFFFF"), H("#2A1018"),
                 H("#501828"), H("#902030"), H("#C03040"), H("#F06070"),
                 H("#F8A0A8"), H("#902838"), H("#50101C")]),
    "row1": (1, [None,
                 H("#1A1008"), H("#7A3A10"), H("#B86018"), H("#E88830"),
                 H("#FFB254"), H("#FFE0B4"), H("#FFFFFF"), H("#2A1A0C"),
                 H("#502C14"), H("#90501C"), H("#C07828"), H("#F0A048"),
                 H("#F8C888"), H("#905828"), H("#503014")]),
    "row2": (2, [None,
                 
H("#181808")
, 
H("#6E7010")
, 
H("#A8AC1C")
, 
H("#D8DC30")
,
                 
H("#F4F860")
, 
H("#FCF8C0")
, 
H("#FFFFFF")
, 
H("#282808")
,
                 
H("#4C4E10")
, 
H("#8A8C1C")
, 
H("#B8BA28")
, 
H("#E0E440")
,
                 
H("#F4F880")
, 
H("#5A5C18")
, 
H("#4C4E0C")
]),
    "row3": (3, [None,
                 
H("#081810")
, 
H("#126838")
, 
H("#1C9C48")
, 
H("#2ED060")
,
                 
H("#78E888")
, 
H("#C8F8D0")
, 
H("#FFFFFF")
, 
H("#102810")
,
                 
H("#184020")
, 
H("#207838")
, 
H("#28B050")
, 
H("#50D068")
,
                 
H("#98E898")
, 
H("#184820")
, 
H("#104020")
]),
    "row4": (4, [None,
                 
H("#081020")
, 
H("#183C78")
, 
H("#2058B0")
, 
H("#3080E0")
,
                 
H("#70A8F0")
, 
H("#C0DCF8")
, 
H("#FFFFFF")
, 
H("#101828")
,
                 
H("#182848")
, 
H("#204070")
, 
H("#2860A0")
, 
H("#4088D0")
,
                 
H("#88B4E8")
, 
H("#183058")
, 
H("#102040")
]),
    # the paddle and the ball
    "hero": (5, [None,
                 H("#081018"), H("#1A3A50"), H("#2A6A8C"), H("#48A8C8"),
                 H("#8CE0F8"), H("#E8FCFF"), H("#FFFFFF"), H("#102430"),
                 H("#1E4860"), H("#2E7498"), H("#50B0D8"), H("#90DCF0"),
                 H("#D0F4FC"), H("#1C3C50"), H("#0C1820")]),
    # the power-ups: two sprites per bank
    "pow_a": (6, [None,
                  H("#101408"), H("#3C6A18"), H("#66A828"), H("#9CE040"),
                  H("#D8FFA0"), H("#F4FFE0"), H("#FFFFFF"), H("#183008"),
                  H("#2A5010"), H("#4C8A20"), H("#78C030"), H("#B0E860"),
                  H("#E8FFC0"), H("#3C7018"), H("#142408")]),
    "pow_b": (7, [None,
                  H("#100818"), H("#3C1A68"), H("#6828A8"), H("#9C40E0"),
                  H("#D084F8"), H("#F0D4FF"), H("#FFFFFF"), H("#181028"),
                  H("#281048"), H("#481C80"), H("#7030B8"), H("#A860E8"),
                  H("#E0B0FF"), H("#3C1870"), H("#140820")]),
    "pow_c": (8, [None,
                  
H("#180810")
, 
H("#681828")
, 
H("#A82840")
, 
H("#E04060")
,
                  
H("#FF8098")
, 
H("#FFD0DC")
, 
H("#FFFFFF")
, 
H("#201018")
,
                  
H("#401828")
, 
H("#702038")
, 
H("#A83050")
, 
H("#DC5070")
,
                  
H("#F898AC")
, 
H("#501028")
, 
H("#381018")
]),
    # the boss
    "boss": (9, [None,
                 H("#0E0A18"), H("#241E38"), H("#3C3460"), H("#5A4E8C"),
                 H("#8678BC"), H("#C0B4E8"), H("#C83850"), H("#E86070"),
                 H("#38384C"), H("#64647C"), H("#9C9CB4"), H("#E8CC64"),
                 H("#FF9060"), H("#60E0E0"), H("#8A1020")]),
    # the effects: the sparks and the explosion
    "fx": (10, [None,
                H("#1A1014"), H("#7A2410"), H("#C04A18"), H("#F08020"),
                H("#FFC040"), H("#FFF0C0"), H("#E8E8F0"), H("#9098A8"),
                H("#40C060"), H("#80E890"), H("#4060D0"), H("#90C0FF"),
                H("#3A1A20"), H("#C0C8D0"), H("#E04060")]),
}

# the sprite table: name -> (frames, palette, width, height)
SPRITES = {}


def idx(name, i):
    """The palette INDEX of colour i in palette `name`."""
    assert PALETTES[name][1][i] is not None, (name, i)
    return i


def add(name, frames, pal_name, w, h, origin=None, collision=None,
        role="sprite"):
    SPRITES[name] = {
        "frames": frames,
        "palette": pal_name,
        "w": w, "h": h,
        "origin": origin if origin else [w // 2, h],
        "collision": collision,
        "role": role,
    }


# --------------------------------------------------------------------------
# The ball and the paddle
# --------------------------------------------------------------------------

def draw_ball(g, hot=0):
    """The ball, 8x8: a bright sphere with a glow."""
    g.disc(3, 3, 3, idx("hero", 3))
    g.disc(3, 3, 2, idx("hero", 4))
    g.disc(3, 3, 1, idx("hero", 5))
    g.px(2, 2, idx("hero", 6))
    if hot:
        g.px(5, 5, idx("hero", 2))


def draw_paddle(g, w):
    """The paddle, w x 10: a bevelled bar with a lit top edge."""
    h = 10
    g.box(0, 0, w - 1, h - 1, idx("hero", 2))
    g.box(1, 1, w - 2, h - 2, idx("hero", 3))
    g.box(2, 2, w - 3, h - 3, idx("hero", 4))
    g.hline(1, 1, w - 2, idx("hero", 5))
    g.hline(2, 2, w - 3, idx("hero", 5))
    g.hline(h - 1, 0, w - 1, idx("hero", 1))
    # the end caps
    g.vline(0, 2, h - 2, idx("hero", 5))
    g.vline(w - 1, 2, h - 2, idx("hero", 5))


# --------------------------------------------------------------------------
# The bricks
# --------------------------------------------------------------------------

BRICK_W = 44          # 40 px of brick + a 4 px gap
BRICK_H = 14          # 11 px of brick + a 3 px gap


def draw_brick(g, pal_name, cracked=False):
    """A brick, 44x14: a bevelled block in the row's colour."""
    g.box(0, 0, 39, 10, idx(pal_name, 2))
    g.box(1, 1, 38, 9, idx(pal_name, 3))
    g.box(2, 2, 37, 7, idx(pal_name, 4))
    g.hline(1, 1, 38, idx(pal_name, 5))
    g.hline(0, 0, 39, idx(pal_name, 1))
    g.vline(0, 0, 10, idx(pal_name, 1))
    g.vline(39, 0, 10, idx(pal_name, 1))
    g.vline(1, 1, 9, idx(pal_name, 5))
    if cracked:
        g.line(12, 2, 20, 8, idx(pal_name, 1), 1)
        g.line(20, 2, 14, 9, idx(pal_name, 1), 1)
        g.line(24, 3, 31, 8, idx(pal_name, 1), 1)


def draw_brick_tough(g, cracked=False):
    """A tough brick, 44x14: it takes two hits and cracks on the first."""
    draw_brick(g, "row2", cracked)
    # the steel bands that mark it out
    g.vline(6, 1, 9, idx("row2", 1))
    g.vline(7, 1, 9, idx("row2", 5))
    g.vline(32, 1, 9, idx("row2", 1))
    g.vline(33, 1, 9, idx("row2", 5))


def draw_brick_steel(g):
    """A steel brick, 44x14: the ball bounces off it."""
    g.box(0, 0, 39, 10, idx("hero", 1))
    g.box(1, 1, 38, 9, idx("hero", 2))
    g.box(2, 2, 37, 7, idx("hero", 3))
    g.hline(1, 1, 38, idx("hero", 5))
    g.hline(2, 2, 37, idx("hero", 5))
    for x in range(4, 38, 8):
        g.vline(x, 3, 8, idx("hero", 2))
        g.px(x + 1, 4, idx("hero", 5))


def draw_brick_bomb(g):
    """A bomb brick, 44x14: it takes one hit and blows its neighbours up."""
    g.box(0, 0, 39, 10, idx("fx", 1))
    g.box(1, 1, 38, 9, idx("fx", 2))
    g.box(2, 2, 37, 7, idx("fx", 3))
    g.hline(1, 1, 38, idx("fx", 4))
    g.disc(20, 5, 3, idx("fx", 1))
    g.disc(20, 5, 2, idx("fx", 4))
    g.px(19, 4, idx("fx", 6))
    g.px(21, 6, idx("fx", 5))


# --------------------------------------------------------------------------
# The power-ups: 14x14 capsules, each with its own mark
# --------------------------------------------------------------------------

def _capsule(g, pal_name):
    g.ellipse(6, 6, 6, 6, idx(pal_name, 1))
    g.ellipse(6, 6, 5, 5, idx(pal_name, 2))
    g.ellipse(6, 6, 4, 4, idx(pal_name, 3))
    g.px(4, 3, idx(pal_name, 5))


def draw_pow_expand(g):
    """EXPAND: the paddle grows."""
    _capsule(g, "pow_a")
    g.line(3, 7, 6, 4, idx("pow_a", 5), 1)
    g.line(6, 4, 9, 7, idx("pow_a", 5), 1)
    g.px(6, 3, idx("pow_a", 6))


def draw_pow_multi(g):
    """MULTI: three balls."""
    _capsule(g, "pow_b")
    g.disc(4, 5, 1, idx("pow_b", 5))
    g.disc(8, 5, 1, idx("pow_b", 5))
    g.disc(6, 9, 1, idx("pow_b", 5))


def draw_pow_slow(g):
    """SLOW: the ball slows down."""
    _capsule(g, "pow_a")
    g.disc(6, 6, 3, idx("pow_a", 5), )
    g.px(6, 6, idx("pow_a", 1))
    g.line(3, 9, 9, 3, idx("pow_a", 6), 1)


def draw_pow_fast(g):
    """FAST: the ball speeds up."""
    _capsule(g, "pow_c")
    g.line(3, 4, 8, 8, idx("pow_c", 5), 1)
    g.line(3, 7, 8, 11, idx("pow_c", 5), 1)
    g.px(9, 5, idx("pow_c", 6))


def draw_pow_laser(g):
    """LASER: the paddle shoots."""
    _capsule(g, "pow_b")
    g.box(6, 2, 7, 10, idx("pow_b", 5))
    g.box(3, 6, 4, 7, idx("pow_b", 6))
    g.box(9, 6, 10, 7, idx("pow_b", 6))


def draw_pow_catch(g):
    """CATCH: the ball sticks to the paddle."""
    _capsule(g, "pow_c")
    g.box(3, 8, 9, 9, idx("pow_c", 5))
    g.vline(4, 4, 7, idx("pow_c", 5))
    g.vline(8, 4, 7, idx("pow_c", 5))
    g.px(4, 3, idx("pow_c", 6))
    g.px(8, 3, idx("pow_c", 6))


# --------------------------------------------------------------------------
# The boss: the last level's ship
# --------------------------------------------------------------------------

BOSS_W = 200
BOSS_H = 72


def draw_boss(g, hurt=False):
    """The boss, 200x72: a dreadnought with three glowing cores."""
    # the hull
    g.polygon([(100, 2), (150, 12), (186, 30), (196, 50), (180, 66),
               (20, 66), (4, 50), (14, 30), (50, 12)], idx("boss", 2))
    g.polygon([(100, 8), (146, 18), (178, 32), (188, 48), (172, 60),
               (28, 60), (12, 48), (22, 32), (54, 18)], idx("boss", 3))
    g.polygon([(100, 16), (138, 24), (164, 36), (172, 48), (158, 56),
               (42, 56), (28, 48), (36, 36), (62, 24)], idx("boss", 4))
    # the spine
    g.box(88, 12, 112, 58, idx("boss", 2))
    g.box(92, 16, 108, 54, idx("boss", 9))
    for y in range(20, 54, 7):
        g.hline(y, 93, 107, idx("boss", 10))
    # the gold trim
    g.hline(40, 30, 170, idx("boss", 11))
    g.hline(41, 30, 170, idx("boss", 1))
    # the three core housings, along the BOTTOM edge: the ball comes from
    # below, so a core on the hull's face would be unreachable
    for cx in (46, 100, 154):
        g.ellipse(cx, 56, 15, 11, idx("boss", 2))
        g.ellipse(cx, 56, 12, 9, idx("boss", 9))
        g.ellipse(cx, 56, 9, 6, idx("boss", 3))
    # the gun ports
    for gx in (24, 60, 140, 176):
        g.box(gx, 52, gx + 5, 60, idx("boss", 2))
        g.hline(53, gx, gx + 5, idx("boss", 10))
    # the engines
    for ex in (30, 58, 86, 114, 142, 170):
        g.box(ex, 60, ex + 10, 69, idx("boss", 9))
        g.box(ex + 2, 62, ex + 7, 69, idx("boss", 12))
    if hurt:
        g.ellipse(70, 22, 9, 7, idx("boss", 1))
        g.ellipse(130, 44, 8, 6, idx("boss", 1))
        g.line(120, 20, 140, 36, idx("boss", 1), 1)
    g.outline(idx("boss", 1))


def draw_boss_core(g, hot=False):
    """A boss core: the weak point, 20x20.

    It is big on purpose: a 14 px target on a 200 px hull would be a
    needle to thread, and the fight has to be a fight, not a lottery.
    """
    if hot:
        g.ellipse(9, 9, 9, 9, idx("boss", 13))
        g.ellipse(9, 9, 8, 8, idx("boss", 12))
        g.ellipse(9, 9, 5, 5, idx("boss", 6))
    else:
        g.ellipse(9, 9, 8, 8, idx("boss", 2))
        g.ellipse(9, 9, 7, 7, idx("boss", 3))
        g.ellipse(9, 9, 4, 4, idx("boss", 13))
    g.outline(idx("boss", 1))


# --------------------------------------------------------------------------
# Build every sprite
# --------------------------------------------------------------------------

def build_sprites():
    b = G(8, 8); draw_ball(b, 0)
    b2 = G(8, 8); draw_ball(b2, 1)
    add("ball", [b, b2], "hero", 8, 8, [4, 4], [1, 1, 6, 6])

    for name, w in (("paddle_s", 40), ("paddle_m", 56), ("paddle_l", 72)):
        g = G(w, 10); draw_paddle(g, w)
        add(name, [g], "hero", w, 10, [w // 2, 5], None)

    for i in range(5):
        g = G(BRICK_W, BRICK_H); draw_brick(g, "row%d" % i)
        add("brick%d" % i, [g], "row%d" % i, BRICK_W, BRICK_H,
            [BRICK_W // 2, BRICK_H // 2], None)

    t1 = G(BRICK_W, BRICK_H); draw_brick_tough(t1, False)
    t2 = G(BRICK_W, BRICK_H); draw_brick_tough(t2, True)
    add("brick_tough", [t1, t2], "row2", BRICK_W, BRICK_H,
        [BRICK_W // 2, BRICK_H // 2], None)

    st = G(BRICK_W, BRICK_H); draw_brick_steel(st)
    add("brick_steel", [st], "hero", BRICK_W, BRICK_H,
        [BRICK_W // 2, BRICK_H // 2], None)

    bm = G(BRICK_W, BRICK_H); draw_brick_bomb(bm)
    add("brick_bomb", [bm], "fx", BRICK_W, BRICK_H,
        [BRICK_W // 2, BRICK_H // 2], None)

    for name, fn, pal in (("pow_expand", draw_pow_expand, "pow_a"),
                          ("pow_multi", draw_pow_multi, "pow_b"),
                          ("pow_slow", draw_pow_slow, "pow_a"),
                          ("pow_fast", draw_pow_fast, "pow_c"),
                          ("pow_laser", draw_pow_laser, "pow_b"),
                          ("pow_catch", draw_pow_catch, "pow_c")):
        g = G(14, 14); fn(g)
        add(name, [g], pal, 14, 14, [7, 7], [1, 1, 12, 12])

    bs = G(BOSS_W, BOSS_H); draw_boss(bs, False)
    bs2 = G(BOSS_W, BOSS_H); draw_boss(bs2, True)
    add("boss", [bs, bs2], "boss", BOSS_W, BOSS_H, [BOSS_W // 2, BOSS_H // 2],
        None)

    bc = G(20, 20); draw_boss_core(bc, False)
    bc2 = G(20, 20); draw_boss_core(bc2, True)
    add("boss_core", [bc, bc2], "boss", 20, 20, [10, 10], [2, 2, 16, 16])

    return SPRITES


# --------------------------------------------------------------------------
# Output
# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--preview", default=None)
    ap.add_argument("--manifest", default="brickstorm_assets.json")
    args = ap.parse_args()

    out_dir = os.path.abspath(args.dir)
    art_dir = os.path.join(out_dir, "art")
    os.makedirs(art_dir, exist_ok=True)

    sprites = build_sprites()

    manifest = {"format": 1, "palettes": {}, "roles": {}, "sprites": [],
                "animations": [], "maps": []}
    for name, (bank, colours) in sorted(PALETTES.items()):
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
                "prompt": "the brickstorm art: "
                          "programs/brickstorm_art/gen_art.py",
                "seed": 0,
                "licence": "this repository",
            },
        }
        if s["collision"]:
            entry["collision"] = s["collision"]
        manifest["sprites"].append(entry)
        if s["role"] != "sprite":
            manifest["roles"][name] = s["role"]
        print("brickstorm-art: %-14s %dx%d x%d (%s)"
              % (name, s["w"], s["h"], len(s["frames"]), s["palette"]))

    mpath = os.path.join(out_dir, args.manifest)
    with open(mpath, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("brickstorm-art: %s" % mpath)

    if args.preview:
        items = [(sprites[n]["frames"][0], n) for n in sorted(sprites)]
        contact_sheet(items, scale=3, path=args.preview)
        print("brickstorm-art: preview %s" % args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
