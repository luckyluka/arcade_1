#!/usr/bin/env python3
"""gen_enemies.py — the library's `enemies` pack.

Five opponents a game needs: a slime, a bat, a rat, a spider and a
walking skeleton.

Four are drawn as *shapes* — a blob that squashes, a wing that beats, a
body that scurries — and the skeleton is a **rig** (art/rigs/skeleton.json),
because a walking enemy is exactly what the rig animator is for: four states
(idle, walk, hurt, dead) from a file of numbers.

Deterministic: fixed seeds, no clock, sorted iteration.

    python3 art/library/enemies/gen_enemies.py --out-dir art/library/enemies

It writes:

    strips/slime.png      2 frames of 16x16
    strips/bat.png        2 frames of 16x16
    strips/rat.png        2 frames of 16x12
    strips/spider.png     2 frames of 12x12
    strips/skeleton_idle.png    2 frames of 24x32
    strips/skeleton_walk.png    4 frames
    strips/skeleton_hurt.png    1 frame
    strips/skeleton_dead.png    1 frame
    enemies.json          the manifest fragment
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from arc1_art import canvas as C                              # noqa: E402
from arc1_art import rig as rigmod                            # noqa: E402

PALETTE = "enemy16"
BANK = 7
SEED = 20261021

# The palette's indices.
DARK = 1        # the outline
SLIME_D = 2
SLIME = 3
SLIME_L = 4
BROWN_D = 5
BROWN = 6
BROWN_L = 7
BLACK = 8
GREY_D = 9
GREY = 10
BONE = 11
WHITE = 12
RED_D = 13
RED = 14
GOLD = 15

SKELETON_RIG = "skeleton"
SKELETON_STATES = ["idle", "walk", "hurt", "dead"]


def _blob(c, cx, cy, rx, ry, colour):
    """A filled ellipse."""
    for y in range(c.h):
        for x in range(c.w):
            dx = (x + 0.5 - cx) / float(rx)
            dy = (y + 0.5 - cy) / float(ry)
            if dx * dx + dy * dy <= 1.0:
                c.px(x, y, colour)


def slime(frame):
    """A slime: a blob that squashes and stretches."""
    c = C.Canvas(16, 16)
    squash = (6, 5) if frame == 0 else (5, 7)
    _blob(c, 8, 12, squash[0], squash[1], SLIME)
    _blob(c, 8, 12, squash[0] - 1, squash[1] - 1, SLIME_L)
    # the highlight and the eyes
    c.px(6, 10, WHITE)
    c.px(9, 11, BLACK)
    c.px(10, 11, BLACK)
    c.px(5, 13, SLIME_D)
    return c


def bat(frame):
    """A bat: the wings beat."""
    c = C.Canvas(16, 16)
    up = (frame == 0)
    for y in range(16):
        for x in range(16):
            dx = abs(x - 7.5)
            dy = 6 - y
            if dy < 0:
                dy = 0
            if up:
                span = 2 + (5 - dy) // 2
            else:
                span = 2 + dy // 2
            if dy <= 5 and dx <= span:
                c.px(x, y, BROWN if dy > 1 else BROWN_D)
    # the body and the eyes
    for y in range(6, 11):
        c.px(7, y, BLACK)
        c.px(8, y, BLACK)
    c.px(7, 7, RED)
    c.px(8, 7, RED)
    return c


def rat(frame):
    """A rat: a body that scurries (the legs swap)."""
    c = C.Canvas(16, 12)
    for y in range(2, 11):
        for x in range(3, 13):
            d = ((x - 7.5) / 5.0) ** 2 + ((y - 6.0) / 3.6) ** 2
            if d <= 1.0:
                c.px(x, y, BROWN if d < 0.6 else BROWN_D)
    # the head and the ears
    c.rect(11, 4, 3, 4, BROWN)
    c.px(12, 3, BROWN_D)
    c.px(14, 3, BROWN_D)
    c.px(13, 5, RED)
    # the legs, swapped between the frames
    legs = (2, 9) if frame == 0 else (3, 8)
    for lx in legs:
        c.px(lx, 10, BLACK)
        c.px(lx + 1, 10, BLACK)
    # the tail
    c.px(1, 7, BROWN_D)
    c.px(0, 8, BROWN_D)
    return c


def spider(frame):
    """A spider: eight legs, two of them lifted on the second frame."""
    c = C.Canvas(12, 12)
    for y in range(2, 11):
        for x in range(4, 8):
            d = ((x - 5.5) / 2.2) ** 2 + ((y - 6.0) / 3.6) ** 2
            if d <= 1.0:
                c.px(x, y, BLACK if d < 0.4 else GREY_D)
    for i in range(4):
        lift = 1 if (frame == 1 and i in (1, 2)) else 0
        y = 5 - lift
        c.px(4 - i // 2, y, GREY)      # the left legs start ON the body
        c.px(7 + i // 2, y, GREY)      # the right legs too
    c.px(5, 5, RED)
    c.px(6, 5, RED)
    return c


ENEMIES = [
    ("slime", slime, 2, 160),
    ("bat", bat, 2, 110),
    ("rat", rat, 2, 150),
    ("spider", spider, 2, 130),
]


def main():
    out_dir = HERE
    args = sys.argv[1:]
    if "--out-dir" in args:
        out_dir = os.path.abspath(args[args.index("--out-dir") + 1])
    strips = os.path.join(out_dir, "strips")
    os.makedirs(strips, exist_ok=True)

    with open(os.path.join(ROOT, "art", "palettes", "%s.json" % PALETTE)) as f:
        preset = json.load(f)

    fragment = {
        "format": 1,
        "description": "Five opponents: a slime, a bat, a rat, a spider and a "
                       "walking skeleton (a rig).",
        "roles": {"skeleton_idle": "sprite", "skeleton_walk": "sprite",
                   "skeleton_hurt": "sprite", "skeleton_dead": "sprite"},
        "palettes": {
            PALETTE: {"bank": BANK, "colors": list(preset["colors"])},
        },
        "sprites": [],
        "animations": [],
        "maps": [],
    }

    def add_sprite(name, frames, palette, note):
        out = os.path.join(strips, "%s.png" % name)
        C.to_strip(frames, palette["colors"], out)
        fragment["sprites"].append({
            "name": name,
            "file": "strips/%s.png" % name,
            "palette": PALETTE,
            "frames": len(frames),
            "origin": [frames[0].w // 2, frames[0].h],
            "provenance": {
                "tool": "procedural", "model": "",
                "prompt": note, "seed": SEED,
                "licence": "this repository",
            },
        })
        print("enemies: %-14s %d frame(s) of %dx%d"
              % (name, len(frames), frames[0].w, frames[0].h))

    for name, make, count, ms in ENEMIES:
        frames = []
        for f in range(count):
            frame = make(f)
            frame.grow_outline(DARK)     # the 1 px dark ring
            frames.append(frame)
        add_sprite(name, frames, preset,
                   "the enemies pack: art/library/enemies/gen_enemies.py")
        fragment["animations"].append({
            "name": "%s_anim" % name,
            "sprite": name,
            "frames": list(range(count)),
            "ms": ms,
        })

    # --- the skeleton: a rig ---------------------------------------------
    rig_path = os.path.join(ROOT, "art", "rigs", "%s.json" % SKELETON_RIG)
    with open(rig_path) as f:
        rig_preset = json.load(f)
    built, states = rigmod.build_rig(rig_preset, states=SKELETON_STATES)
    for state in SKELETON_STATES:
        frames = states.get(state) or []
        if not frames:
            continue
        name = "skeleton_%s" % state
        add_sprite(name, frames, preset,
                   "the rig animator: art/rigs/%s.json" % SKELETON_RIG)
        fragment["animations"].append({
            "name": "skeleton_%s_anim" % state,
            "sprite": name,
            "frames": list(range(len(frames))),
            "ms": rig_preset.get("animations", {}).get(state, {}).get("ms", 150),
        })

    fragment_path = os.path.join(out_dir, "enemies.json")
    with open(fragment_path, "w", encoding="utf-8") as f:
        json.dump(fragment, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("enemies: %s" % fragment_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
