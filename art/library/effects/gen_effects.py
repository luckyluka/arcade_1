#!/usr/bin/env python3
"""gen_effects.py — the library's `effects` pack.

The particles a game throws around: dust, smoke, a spark, a splash and a
flame. Five small animated strips, 16x16, in one palette (the round-5
effects palette, reused: the browns for the dust, the creams for the
smoke, the oranges for the flame).

The frames are drawn as *expanding* shapes rather than as drawn
animations, so they stay consistent: a puff is a puff at every size, and
the whole set is one function per effect.

Deterministic: fixed seeds, no clock, sorted iteration.

    python3 art/library/effects/gen_effects.py --out-dir art/library/effects

It writes:

    strips/dust.png     3 frames of 16x16
    strips/smoke.png    3 frames
    strips/spark.png    3 frames
    strips/splash.png   3 frames
    strips/fire.png     4 frames
    effects.json        the manifest fragment
"""

import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from arc1_art import canvas as C                              # noqa: E402

SIZE = 16
PALETTE = "fx16"
BANK = 13

# The palette's indices.
DARK = 1        # the outline
BROWN = 2
GOLD = 3
AMBER = 4
CREAM = 5
ROSE = 6
PINK = 7
BLUSH = 8
SNOW = 9
ICE = 10
BLUE = 11
RED = 12
EMBER = 13
WHITE = 14
SMOKE = 15

SEED = 20261021


def _ring(c, cx, cy, radius, colour, fill=False):
    """A circle of `radius` pixels (the fill is the disc)."""
    r = radius + 0.5
    for y in range(SIZE):
        for x in range(SIZE):
            d = (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2
            if d <= r * r:
                if fill or d > (r - 1) * (r - 1):
                    c.px(x, y, colour)


def dust(frame, total):
    """A puff of earth, spreading and thinning."""
    c = C.Canvas(SIZE, SIZE)
    t = frame / float(total - 1)
    r = C.Rng(SEED)
    for i in range(9):
        a = r.rand(256) / 256.0 * 2.0 * math.pi
        d = 1.0 + t * 4.0
        x = 8 + int(round(math.cos(a) * d))
        y = 8 + int(round(math.sin(a) * d)) - int(t)
        if 0 <= x < SIZE and 0 <= y < SIZE:
            c.px(x, y, BROWN if r.rand(2) else GOLD)
    if t < 0.5:
        c.px(8, 8 - int(t * 4), GOLD)
    return c


def smoke(frame, total):
    """A pale puff, rising."""
    c = C.Canvas(SIZE, SIZE)
    t = frame / float(total - 1)
    r = C.Rng(SEED + 1)
    for i in range(6):
        x = 5 + r.rand(6)
        y = 11 - int(t * 6) + r.rand(3)
        if 0 <= x < SIZE and 0 <= y < SIZE:
            c.px(x, y, SMOKE if r.rand(2) else CREAM)
    return c


def spark(frame, total):
    """A four-pointed flash, shrinking."""
    c = C.Canvas(SIZE, SIZE)
    t = frame / float(total - 1)
    arm = max(1, 6 - int(t * 5))
    for i in range(-arm, arm + 1):
        for (x, y) in ((8 + i, 8), (8, 8 + i)):
            if 0 <= x < SIZE and 0 <= y < SIZE:
                c.px(x, y, WHITE if abs(i) < arm else CREAM)
    if frame == 0:
        c.px(8, 8, SNOW)
    return c


def splash(frame, total):
    """Droplets thrown up, falling back."""
    c = C.Canvas(SIZE, SIZE)
    t = frame / float(total - 1)
    r = C.Rng(SEED + 2)
    for i in range(5):
        x0 = 3 + r.rand(10)
        rise = int(t * 6) - 1
        y = 12 - rise
        if 0 <= x0 < SIZE and 0 <= y < SIZE:
            c.px(x0, y, BLUE)
            c.px(x0, y + 1, ICE)
    return c


def fire(frame, total):
    """A flame: a bright core, licking upward, breathing over the frames."""
    c = C.Canvas(SIZE, SIZE)
    t = frame / float(total - 1)
    height = 6 + int(round(math.sin(t * math.pi) * 4))     # 6..10 px
    for y in range(SIZE):
        dy = SIZE - 1 - y
        if dy > height:
            continue
        half = 3.2 * (1.0 - dy / float(height)) + 0.6
        for x in range(SIZE):
            if abs(x + 0.5 - 7.5) < half:
                c.px(x, y, CREAM if abs(x + 0.5 - 7.5) < half * 0.45
                      else AMBER)
    return c


EFFECTS = [
    ("dust", dust, 3, 120),
    ("smoke", smoke, 3, 140),
    ("spark", spark, 3, 70),
    ("splash", splash, 3, 110),
    ("fire", fire, 4, 90),
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
        "description": "Five particle strips for a game's effects: dust, "
                       "smoke, a spark, a splash and a flame.",
        "palettes": {
            PALETTE: {"bank": BANK, "colors": list(preset["colors"])},
        },
        "roles": {name: "effect" for name, _m, _c, _ms in EFFECTS},
        "sprites": [],
        "animations": [],
        "maps": [],
    }

    for name, make, count, ms in EFFECTS:
        frames = [make(f, count) for f in range(count)]
        out = os.path.join(strips, "%s.png" % name)
        C.to_strip(frames, preset["colors"], out)
        fragment["sprites"].append({
            "name": name,
            "file": "strips/%s.png" % name,
            "palette": PALETTE,
            "frames": count,
            "origin": [SIZE // 2, SIZE],
            "provenance": {
                "tool": "procedural",
                "model": "",
                "prompt": "the effects pack: "
                          "art/library/effects/gen_effects.py",
                "seed": SEED,
                "licence": "this repository",
            },
        })
        fragment["animations"].append({
            "name": "%s_anim" % name,
            "sprite": name,
            "frames": list(range(count)),
            "ms": ms,
        })
        print("effects: %-7s %d frame(s)" % (name, count))

    fragment_path = os.path.join(out_dir, "effects.json")
    with open(fragment_path, "w", encoding="utf-8") as f:
        json.dump(fragment, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("effects: %s" % fragment_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
