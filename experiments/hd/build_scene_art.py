#!/usr/bin/env python3
"""experiments/hd/build_scene_art.py — art for H3's two scenes.

One builder, two outputs, so the scenes differ ONLY in pixel density:

    --scale 1 -> experiments/hd/scene480_art/assets.h   (480x270 scene)
    --scale 2 -> experiments/hd/scene960_art/assets.h   (960x540 scene)

Both manifests carry the same four sprites (the rig hero's idle state, the
terrain tileset, the parallax ridge, the coin), the same level map and the
same palettes at the SAME explicit banks — so both guests make literally the
same gfx_pal_upload() calls and draw the same elements. What differs:

  hero    scale 1: art/rigs/samurai.json        -> 28x40
          scale 2: experiments/hd/rig/samurai_2x.json -> 56x80, from
                  mk_rig2x.py (the rig animator at twice the height)
  ridge   scale 1: programs/petalblade_art/art/bg_ridge.png (the committed
                  art, referenced not regenerated)
          scale 2: drawn here at 480x120 by calling the GAME'S OWN
                  generator — programs/petalblade_art/gen_art.py — for its
                  canvas, its _profile() and its PNG writer, with the
                  geometry doubled. Importing the module is safe: its
                  argparse lives under `if __name__ == "__main__"`.
  tiles, coin, level map: identical files at both scales.

EXCLUDED, per HD_EXPERIMENT.md card H3's escape hatch: the tiles cannot be
regenerated at 32x32 in under an hour — gen_art.py's tile drawing is
absolute per-feature coordinates (`g.hline(oy + 15, ox, ox + 15, ...)`,
cracks from ox+4 to ox+7), so doubling means re-authoring the detail of
fourteen tile functions, not changing a parameter. The tiles therefore appear
at 16x16 in BOTH scenes, and docs/experiments/hd-art.md says so plainly
rather than pretending the tiles were compared.

Deterministic: sorted iteration, no clock, no randomness.

    python3 experiments/hd/build_scene_art.py --scale 1
    python3 experiments/hd/build_scene_art.py --scale 2
"""

import argparse
import importlib.util
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ART_TOOL = os.path.join(ROOT, "tools", "arc1-art")
ASSETC = os.path.join(ROOT, "tools", "arc1_assetc.py")
WORLD_MANIFEST = os.path.join(ROOT, "programs", "petalblade_rig", "assets.json")
WORLD_ART = os.path.join(ROOT, "programs", "petalblade_art", "art")
WORLD_LEVEL = os.path.join(ROOT, "programs", "petalblade_art", "level1.txt")
RIG1 = os.path.join(ROOT, "art", "rigs", "samurai.json")
RIG2 = os.path.join(ROOT, "experiments", "hd", "rig", "samurai_2x.json")

# The palettes the scene draws with, at the banks petalblade_rig's committed
# manifest uses, so both scenes upload to identical slots. hero16 is pinned
# to 0 (NOT the rig fragment's 1): bank 1 is oni's in the world manifest, and
# petalblade_rig's own header documents `palette hero16 (bank 0)`.
BANKS = {"hero16": 0, "terrain": 3, "ridge": 4, "fx": 8}


def run(*args):
    subprocess.check_call([str(a) for a in args], cwd=ROOT)


def load(path):
    with open(path) as f:
        return json.load(f)


def rel(path, start):
    """A manifest-relative path to PATH (manifests resolve files against
    their own directory)."""
    return os.path.relpath(os.path.abspath(path), os.path.abspath(start))


def load_gen_art():
    """Import the game's art generator as a module (stdlib only, its main()
    is guarded). Returns the module."""
    src = os.path.join(ROOT, "programs", "petalblade_art", "gen_art.py")
    spec = importlib.util.spec_from_file_location("petalblade_gen_art", src)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def ridge_scale(mod, scale):
    """The far range at `scale` x. bg_ridge()'s own maths, with every length
    doubled: _profile() divides by W, so passing the doubled width stretches
    the SAME silhouette horizontally (identical harmonic count over twice the
    width) while the doubled base and amplitudes stretch it vertically. The
    colour bands read off a normalised t, so they scale for free, and the
    strip stays seamless (the profile matches at x = 0 and x = W for any W)."""
    W, Hh = 240 * scale, 60 * scale
    base, terms = 18 * scale, [(1, 13 * scale, 0.4),
                               (2, 8 * scale, 1.3),
                               (5, 4 * scale, 2.1),
                               (9, 2 * scale, 0.7)]
    clamp_lo, clamp_hi = 3 * scale, (60 * scale) - 4 * scale
    g = mod.G(W, Hh)
    for x in range(W):
        top = int(mod._profile(x, W, base, terms))
        top = max(clamp_lo, min(clamp_hi, top))
        for y in range(top, Hh):
            t = (y - top) / max(1, (Hh - top))
            g.p[y][x] = (mod.R11 if t < 0.2 else
                         (mod.R10 if t < 0.4 else
                          (mod.R9 if t < 0.65 else mod.R8)))
        if top > 1 and g.p[top - 1][x] == 0:
            g.p[top - 1][x] = mod.R13
    return [g], W, Hh


def build(scale):
    assert scale in (1, 2), "scale must be 1 or 2"
    out = os.path.join(ROOT, "experiments", "hd",
                       "scene%d_art" % (480 if scale == 1 else 960))
    scratch = os.path.join(out, "_rig")
    os.makedirs(out, exist_ok=True)

    # --- 1. the hero, from the rig animator at this scale -------------------
    rig_preset = RIG1 if scale == 1 else RIG2
    if scale == 2 and not os.path.exists(rig_preset):
        run(sys.executable, os.path.join(ROOT, "experiments", "hd",
                                         "mk_rig2x.py"))
    run(ART_TOOL, "rig", rig_preset, "--out-dir", scratch)
    fragment = load(os.path.join(scratch, "assets.json"))

    # --- 2. the ridge -------------------------------------------------------
    if scale == 1:
        shutil.copyfile(os.path.join(WORLD_ART, "bg_ridge.png"),
                        os.path.join(out, "bg_ridge.png"))
    else:
        mod = load_gen_art()
        frames, W, Hh = ridge_scale(mod, scale)
        mod.write_strip(os.path.join(out, "bg_ridge.png"), frames, "ridge",
                        W, Hh)

    # --- 3. the hero strip beside the manifest ------------------------------
    shutil.copyfile(os.path.join(scratch, "samurai_idle.png"),
                    os.path.join(out, "samurai_idle.png"))

    # --- 4. the manifest: the world's entries, trimmed to what the scene
    #        draws, with explicit banks and re-based file paths --------------
    world = load(WORLD_MANIFEST)
    by_name = {s["name"]: s for s in world["sprites"]}

    manifest = {"format": 1, "palettes": {}, "sprites": [], "animations": [],
                "maps": []}

    for name in sorted(BANKS):
        pal = dict(world["palettes"][name])
        pal["bank"] = BANKS[name]
        manifest["palettes"][name] = pal

    # the hero comes from THIS scale's rig fragment, not the world's entry:
    # the fragment measures the collision box at this scale (the world's is
    # the 1x one, wrong at 56x80). Its palette NAME (hero16) resolves against
    # the pinned bank-0 copy above, so the fragment's own bank 1 never
    # reaches the manifest — bank 1 is oni's in the world manifest.
    frag_hero = {s["name"]: s for s in fragment["sprites"]}["samurai_idle"]
    hero = dict(frag_hero)
    hero["palette"] = "hero16"
    hero["file"] = "samurai_idle.png"
    manifest["sprites"].append(hero)

    for name, local in (("ridge", "bg_ridge.png"),
                        ("tiles", None), ("coin", None)):
        spr = dict(by_name[name])
        spr["file"] = local if local else rel(
            os.path.join(WORLD_ART,
                         os.path.basename(by_name[name]["file"])), out)
        manifest["sprites"].append(spr)

    # the tileset: assetc emits level1_cells/level1_tiles from the map, which
    # is where the scene's four ground cells come from.
    for level in world.get("maps", []):
        level = dict(level)
        level["file"] = rel(WORLD_LEVEL, out)
        manifest["maps"].append(level)

    # the hero's idle animation, so both headers carry the same animation
    # tables (the scene is static and draws frame 0 either way).
    for anim in fragment.get("animations", []):
        if anim["sprite"] == "samurai_idle":
            manifest["animations"].append(anim)

    manifest_path = os.path.join(out, "assets.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
        f.write("\n")

    # --- 5. the compiled header -------------------------------------------
    run(sys.executable, ASSETC, "--assets", manifest_path, "--out-dir", out,
        "--report")

    # the rig scratch is not part of the build truth
    for name in os.listdir(scratch):
        os.unlink(os.path.join(scratch, name))
    os.rmdir(scratch)

    print("scene%d: %s (%d sprites, %d palettes)"
          % (scale, os.path.relpath(manifest_path, ROOT),
             len(manifest["sprites"]), len(manifest["palettes"])))
    return 0


def main(argv):
    ap = argparse.ArgumentParser(
        description="build one scene's art at scale 1 or 2")
    ap.add_argument("--scale", type=int, default=2, choices=(1, 2))
    a = ap.parse_args(argv[1:])
    return build(a.scale)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
