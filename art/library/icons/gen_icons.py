#!/usr/bin/env python3
"""gen_icons.py — the library's `icons` pack.

Five small gameplay icons, authored as `.px` text pixmaps (the format from
ROADMAP_V4 card 7.3) and compiled into strips by the repository's own
pixmap reader. They are the library's *interface* layer: the coin, the
heart, the key, the potion and the arrow — the things a HUD shows and a
player picks up.

Deterministic: the pixmaps are the source (they are text), the strips are
the build truth, and a fresh run gives the same bytes.

    python3 art/library/icons/gen_icons.py --out-dir art/library/icons

It writes:

    strips/coin.png       8x8, 1 frame
    strips/heart.png      8x8, 1 frame
    strips/key.png        8x8, 1 frame
    strips/potion.png     8x8, 1 frame
    strips/arrow.png      8x8, 1 frame
    icons.json            the manifest fragment
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from arc1_art import pixmap                                # noqa: E402

# The five pixmaps, in the order the manifest lists them.
ICONS = ["coin", "heart", "key", "potion", "arrow"]

# The bank the icons live in. The props16 preset is the scenery palette
# (a vermilion, a tan, a stone grey, the greens) — the icons borrow its
# ramp; index 1 is the outline, as every palette in the library.
PALETTE = "props16"
BANK = 11


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
        "description": "Five small gameplay icons (coin, heart, key, potion, "
                       "arrow) — the HUD's vocabulary.",
        "palettes": {
            PALETTE: {"bank": BANK, "colors": list(preset["colors"])},
        },
        "sprites": [],
        "animations": [],
        "maps": [],
    }

    for name in ICONS:
        path = os.path.join(HERE, "pixmaps", "%s.px" % name)
        pm = pixmap.parse_px(path)
        out = os.path.join(strips, "%s.png" % name)
        pixmap.write_strip(out, pm, preset)
        fragment["sprites"].append({
            "name": name,
            "file": "strips/%s.png" % name,
            "palette": PALETTE,
            "frames": len(pm["frames"]),
            "origin": [pm["w"] // 2, pm["h"]],
            "provenance": {
                "tool": "agent-authored",
                "model": "",
                "prompt": "hand-authored by agent as a .px pixmap "
                          "(art/library/icons/pixmaps/%s.px)" % name,
                "seed": 0,
                "licence": "this repository",
            },
        })
        print("icons: %s (%dx%d, %d frame(s))"
              % (name, pm["w"], pm["h"], len(pm["frames"])))

    fragment_path = os.path.join(out_dir, "icons.json")
    with open(fragment_path, "w", encoding="utf-8") as f:
        json.dump(fragment, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("icons: %s" % fragment_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
