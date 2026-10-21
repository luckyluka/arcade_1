#!/usr/bin/env python3
"""tools/build_petalblade_rig.py — build the PETAL BLADE (rig) variant.

One command builds every asset of the art pass (ROADMAP_V4 card 7.7):

  * the hero, from the rig animator (art/rigs/samurai.json);
  * the five props, from the .px pixmaps (art/px/props/*.px);
  * the world's own art, reused from the procedural generator
    (programs/petalblade_art/);
  * the manifest that ties them together, and the compiled header.

Everything is deterministic: the same inputs produce the same bytes.

    python3 tools/build_petalblade_rig.py
"""

import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "tools", "arc1-art")
OUT = os.path.join(ROOT, "programs", "petalblade_rig")
RIG = os.path.join(ROOT, "art", "rigs", "samurai.json")
PROPS = os.path.join(ROOT, "art", "px", "props")
WORLD = os.path.join(ROOT, "programs", "petalblade_art",
                     "petalblade_assets.json")


def run(*args):
    subprocess.check_call(list(args), cwd=ROOT)


def _copy(src, dst):
    with open(src, "rb") as f:
        data = f.read()
    with open(dst, "wb") as f:
        f.write(data)


def load(path):
    with open(path) as f:
        return json.load(f)


def main():
    os.makedirs(OUT, exist_ok=True)

    # --- 1. the hero, from the rig ---------------------------------------
    rig_dir = os.path.join(OUT, "_rig")
    run(ART, "rig", RIG, "--out-dir", rig_dir)
    rig = load(os.path.join(rig_dir, "assets.json"))

    # --- 2. the props, from the pixmaps -----------------------------------
    props_dir = os.path.join(OUT, "_props")
    px_files = sorted(os.path.join(PROPS, n)
                      for n in os.listdir(PROPS) if n.endswith(".px"))
    run(ART, "px2assets", *px_files, "--out-dir", props_dir)
    props = load(os.path.join(props_dir, "assets.json"))

    # --- 3. the world, from the procedural generator ----------------------
    world = load(WORLD)

    # --- 4. the variant's manifest ----------------------------------------
    manifest = {"format": 1, "palettes": {}, "sprites": [], "animations": [],
                "maps": []}

    # the palettes: the world's, with the rig's hero bank taking the place of
    # the old hero palette (the rig paints with the same colours), plus the
    # props' bank.
    manifest["palettes"] = dict(world["palettes"])
    manifest["palettes"].pop("hero", None)
    for name, pal in rig["palettes"].items():
        manifest["palettes"][name] = pal
    for name, pal in props["palettes"].items():
        manifest["palettes"][name] = pal

    # the sprites: the world's (without the procedural hero), the rig's hero
    # states and the props.
    for sprite in world["sprites"]:
        if sprite["name"] == "hero":
            continue
        sprite = dict(sprite)
        # the world's art stays where it is: the path is now relative to
        # this manifest's directory
        sprite["file"] = "../petalblade_art/" + sprite["file"]
        manifest["sprites"].append(sprite)
    for sprite in rig["sprites"]:
        sprite = dict(sprite)
        state = sprite["name"].split("_", 1)[1]
        sprite["file"] = "hero_%s.png" % state
        manifest["sprites"].append(sprite)
    for sprite in props["sprites"]:
        sprite = dict(sprite)
        sprite["file"] = "prop_%s.png" % sprite["name"]
        manifest["sprites"].append(sprite)

    # the maps: the world's (the level is unchanged by the art pass)
    for level in world.get("maps", []):
        level = dict(level)
        level["file"] = "../petalblade_art/" + level["file"]
        manifest["maps"].append(level)

    # the animations: the world's (without the hero's) and the rig's.
    for anim in world["animations"]:
        if anim["sprite"] == "hero":
            continue
        manifest["animations"].append(anim)
    for anim in rig["animations"]:
        manifest["animations"].append(anim)

    # the files live beside the manifest
    for sprite in rig["sprites"]:
        state = sprite["name"].split("_", 1)[1]
        _copy(os.path.join(rig_dir, sprite["name"] + ".png"),
              os.path.join(OUT, "hero_%s.png" % state))
    _copy(os.path.join(rig_dir, "samurai_sheet.png"),
          os.path.join(OUT, "samurai_sheet.png"))
    for sprite in props["sprites"]:
        _copy(os.path.join(props_dir, sprite["file"]),
              os.path.join(OUT, "prop_%s.png" % sprite["name"]))

    manifest_path = os.path.join(OUT, "assets.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
        f.write("\n")

    # --- 5. the compiled header -------------------------------------------
    run(sys.executable, os.path.join(ROOT, "tools", "arc1_assetc.py"),
        "--assets", manifest_path, "--out-dir", OUT, "--report")

    # the scratch directories are not part of the build truth
    for scratch in (rig_dir, props_dir):
        for name in os.listdir(scratch):
            os.unlink(os.path.join(scratch, name))
        os.rmdir(scratch)

    print("petalblade-rig: %s (%d sprites, %d animations)"
          % (manifest_path, len(manifest["sprites"]),
             len(manifest["animations"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
