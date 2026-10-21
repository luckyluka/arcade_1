#!/usr/bin/env python3
"""gen_faces.py — the library's `faces` pack.

Two faces, authored as `.px` text pixmaps: a samurai's and a skeleton's. They are the rig's missing piece — the rig's shapes are
ellipses, capsules and polygons, so it **cannot draw a face**. A game
composites one of these on the rig's head and the character looks at you.

The face is drawn INSIDE the rig's head ellipse (8x8 in an 11x12 head), so
the head's own outline ring still shows around it.

Deterministic: the pixmaps are the source (they are text), the strips are
the build truth, and a fresh run gives the same bytes.

    python3 art/library/faces/gen_faces.py --out-dir art/library/faces

It writes:

    strips/face_samurai.png   8x8, 1 frame
    strips/face_skeleton.png  8x8, 1 frame
    faces.json               the manifest fragment
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from arc1_art import pixmap                                # noqa: E402

# The three faces, in the order the manifest lists them.
FACES = ["face_samurai", "face_skeleton"]


def main():
    out_dir = HERE
    args = sys.argv[1:]
    if "--out-dir" in args:
        out_dir = os.path.abspath(args[args.index("--out-dir") + 1])
    strips = os.path.join(out_dir, "strips")
    os.makedirs(strips, exist_ok=True)

    fragment = {
        "format": 1,
        "description": "Two faces for the rigged characters: the rig cannot "
                       "draw a face, so a game composites one of these on the "
                       "head.",
        # A face is an OVERLAY: it is drawn on top of the rig's head, so
        # it has no outline ring of its own (the head's own ring shows
        # around it) and it is not a standalone sprite.
        "roles": {"face_samurai": "overlay", "face_skeleton": "overlay"},
        "palettes": {},
        "sprites": [],
        "animations": [],
        "maps": [],
    }
    banks = {}

    for name in FACES:
        path = os.path.join(HERE, "pixmaps", "%s.px" % name)
        pm = pixmap.parse_px(path)
        palette_name = pm["palette"]
        with open(os.path.join(ROOT, "art", "palettes",
                               "%s.json" % palette_name)) as f:
            preset = json.load(f)
        if palette_name not in banks:
            bank = pm.get("bank", 0)
            banks[palette_name] = bank
            fragment["palettes"][palette_name] = {
                "bank": bank, "colors": list(preset["colors"])}
        out = os.path.join(strips, "%s.png" % name)
        pixmap.write_strip(out, pm, preset)
        fragment["sprites"].append({
            "name": name,
            "file": "strips/%s.png" % name,
            "palette": palette_name,
            "frames": len(pm["frames"]),
            "origin": [pm["w"] // 2, pm["h"]],
            "provenance": {
                "tool": "agent-authored",
                "model": "",
                "prompt": "hand-authored by agent as a .px pixmap "
                          "(art/library/faces/pixmaps/%s.px)" % name,
                "seed": 0,
                "licence": "this repository",
            },
        })
        print("faces: %-14s %dx%d (%s)"
              % (name, pm["w"], pm["h"], palette_name))

    fragment_path = os.path.join(out_dir, "faces.json")
    with open(fragment_path, "w", encoding="utf-8") as f:
        json.dump(fragment, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("faces: %s" % fragment_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
