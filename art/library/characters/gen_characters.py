#!/usr/bin/env python3
"""gen_characters.py — the library's `characters` pack.

The two rig presets, built with the rig animator (ROADMAP_V4 card 7.4):

* `humanoid` — the reference rig: a plain person, 8 states
* `samurai`  — Petal Blade's hero: a kabuto with a gold maedate, a crimson
  sash, indigo armour, a hakama and a katana

A rig is the library's *character* format: the parts, the keyframes and
the poses are numbers in one file, so a game can change a walk cycle
without touching the art, and every frame of every state is the same
character by construction.

Deterministic: the presets are fixed and the animator is exact, so a
fresh run gives the same bytes.

    python3 art/library/characters/gen_characters.py --out-dir art/library/characters

It writes:

    strips/humanoid_<state>.png    the humanoid's states
    strips/samurai_<state>.png     the samurai's states
    characters.json                the manifest fragment
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from arc1_art import rig                                  # noqa: E402

RIGS = ["humanoid", "samurai"]


def main():
    out_dir = HERE
    args = sys.argv[1:]
    if "--out-dir" in args:
        out_dir = os.path.abspath(args[args.index("--out-dir") + 1])
    strips = os.path.join(out_dir, "strips")
    os.makedirs(strips, exist_ok=True)

    fragment = {
        "format": 1,
        "description": "Two rigged characters (the humanoid reference and the "
                       "samurai) — eight states each, built from parts and "
                       "keyframes rather than drawn frame by frame.",
        "palettes": {},
        "sprites": [],
        "animations": [],
        "maps": [],
    }
    seen_palettes = {}

    for rig_name in RIGS:
        preset_path = os.path.join(ROOT, "art", "rigs", "%s.json" % rig_name)
        with open(preset_path) as f:
            preset = json.load(f)
        palette_name = preset.get("palette", "neutral16")
        with open(os.path.join(ROOT, "art", "palettes",
                               "%s.json" % palette_name)) as f:
            palette = json.load(f)
        if palette_name not in seen_palettes:
            bank = preset.get("bank", len(seen_palettes))
            seen_palettes[palette_name] = bank
            fragment["palettes"][palette_name] = {
                "bank": bank, "colors": list(palette["colors"])}

        built, states = rig.build_rig(preset)
        for state, frames in sorted(states.items()):
            sprite = "%s_%s" % (rig_name, state)
            out = os.path.join(strips, "%s.png" % sprite)
            rig.write_strip(out, frames, palette["colors"])
            box = rig.collision_box(frames)
            fragment["sprites"].append({
                "name": sprite,
                "file": "strips/%s.png" % sprite,
                "palette": palette_name,
                "frames": len(frames),
                "origin": [built.width // 2, built.height],
                "collision": list(box),
                "provenance": {
                    "tool": "procedural",
                    "model": "",
                    "prompt": "the rig animator: art/rigs/%s.json" % rig_name,
                    "seed": 0,
                    "licence": "this repository",
                },
            })
            anim = preset.get("animations", {}).get(state, {})
            fragment["animations"].append({
                "name": "%s_%s" % (rig_name, state),
                "sprite": sprite,
                "frames": list(range(len(frames))),
                "ms": anim.get("ms", 100),
            })
        print("characters: %s (%d state(s))" % (rig_name, len(states)))

    fragment_path = os.path.join(out_dir, "characters.json")
    with open(fragment_path, "w", encoding="utf-8") as f:
        json.dump(fragment, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("characters: %s" % fragment_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
