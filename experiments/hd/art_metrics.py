#!/usr/bin/env python3
"""experiments/hd/art_metrics.py — H3's measurements, from the artifacts.

Prints the numbers docs/experiments/hd-art.md quotes, straight out of the
generated headers and the captures, so nothing in the report is typed by
hand:

  bytes   every sprite's COMPILED size, read out of scene480_art/assets.h and
          scene960_art/assets.h (the `*_sheet[N]` array lengths the asset
          compiler emitted — real ROM bytes, not a recomputation), against
          HD_EXPERIMENT.md H2's rule  bytes = W * H * 4 / 8, with the
          percentage deviation of each row and of each scene total.
          H2's own table used hypothetical dimensions ("computed, not
          drawn"), so this also prints H2's rows whose dimensions this scene
          happens to share, and says plainly which ones it does not share.
  colours the distinct opaque colours each sprite actually uses, at both
          densities, and the smallest gap between two of them — that gap is
          what "the colours the hero needed to avoid banding" means: merge
          the closest pair and the silhouette loses its outline.

Deterministic: sorted iteration, no clock, no randomness.

    python3 experiments/hd/art_metrics.py
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tools", "arc1_art"))

import arc1_assetc  # noqa: E402  (the same PNG decoder the pipeline uses)

SCENES = (("480", os.path.join(ROOT, "experiments", "hd", "scene480_art")),
          ("960", os.path.join(ROOT, "experiments", "hd", "scene960_art")))

# H2's table, verbatim from docs/experiments/hd-fpga-estimates.md — the rows
# this scene's art does and does not share a shape with.
H2_ROWS = (("one tile 64x64", 64, 64, 1, 2048),
           ("one tile 128x128", 128, 128, 1, 8192),
           ("hero frame 96x120", 96, 120, 1, 5760),
           ("hero frame 192x240", 192, 240, 1, 23040),
           ("full-width strip 480x270", 480, 270, 1, 64800),
           ("full-width strip 960x540", 960, 540, 1, 259200))


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    w, h, px = arc1_assetc.decode_png(data, os.path.basename(path))
    return w, h, px


def header_sheet_bytes(header_path):
    """Every `static const unsigned char NAME_sheet[N]` the compiler emitted:
    N bytes of real ROM. This is the measurement, not a recomputation."""
    with open(header_path) as f:
        text = f.read()
    return {m.group(1): int(m.group(2))
            for m in re.finditer(r"unsigned char (\w+)_sheet\[(\d+)\]", text)}


def sprite_geom(manifest):
    """name -> (w, h, frames) read from the sprite's own PNG (the asset
    compiler takes the geometry from the image, so this is the authority)."""
    base = os.path.dirname(manifest)
    with open(manifest) as f:
        doc = json.load(f)
    out = {}
    for s in doc["sprites"]:
        path = os.path.normpath(os.path.join(base, s["file"]))
        w, h, _ = read_png(path)
        n = int(s.get("frames", 1))
        out[s["name"]] = (w // n, h, n, s.get("palette"))
    return out, doc


def pct(measured, formula):
    return 0.0 if formula == 0 else (measured - formula) * 100.0 / formula


def main():
    print("H3 asset bytes: compiled (from the generated headers) vs H2's rule "
          "W*H*4/8")
    print()
    print("| scene | sprite | frame | frames | H2 rule | compiled | dev |")
    print("|---|---|---:|---:|---:|---:|---:|")

    totals = {}
    worst = 0.0
    geom_by_scene = {}
    for tag, d in SCENES:
        hdr = os.path.join(d, "assets.h")
        sheets = header_sheet_bytes(hdr)
        geom, _doc = sprite_geom(os.path.join(d, "assets.json"))
        geom_by_scene[tag] = geom
        tot_rule = tot_compiled = 0
        for name in sorted(geom):
            w, h, n, _pal = geom[name]
            rule = (w * h * 4 // 8) * n
            compiled = sheets.get(name, -1)
            dev = pct(compiled, rule)
            worst = max(worst, abs(dev))
            tot_rule += rule
            tot_compiled += compiled
            print("| %s | %s | %dx%d | %d | %s B | %s B | %+.1f%% |"
                  % (tag, name, w, h, n, f"{rule:,}", f"{compiled:,}", dev))
        totals[tag] = (tot_rule, tot_compiled)
        print("| **%s** | **sprites** | | | **%s B** | **%s B** | **%+.1f%%** |"
              % (tag, f"{tot_rule:,}", f"{tot_compiled:,}",
                 pct(tot_compiled, tot_rule)))
        print()

    print("worst row deviation: %+.1f%%  (acceptance: within 10%%)\n" % worst)

    r480, c480 = totals["480"]
    r960, c960 = totals["960"]
    # the tiles and the coin are 1x in both scenes by the card's escape hatch,
    # so only the hero and the ridge are expected to quadruple.
    fixed = 0
    for name, (w, h, n, _pal) in sorted(geom_by_scene["480"].items()):
        if name in ("tiles", "coin"):
            fixed += w * h * 4 // 8 * n
    scaled480 = r480 - fixed
    scaled960 = r960 - fixed
    print("2x rule (H2's table is a straight quadruple):")
    print("  regenerated assets  %s B -> %s B  = %.3fx  (H2 predicts 4.000x, "
          "%+.1f%% dev)" % (f"{scaled480:,}", f"{scaled960:,}",
                            scaled960 / scaled480,
                            (scaled960 / scaled480 - 4.0) * 100.0 / 4.0))
    print("  excluded 1x assets  %s B unchanged (tiles + coin, the card's "
          "escape hatch)" % f"{fixed:,}")
    print("  all sprites         %s B -> %s B  = %.3fx"
          % (f"{r480:,}", f"{r960:,}", r960 / r480))
    print()

    print("H2's own table (docs/experiments/hd-fpga-estimates.md) used")
    print("hypothetical dimensions, labelled 'computed, not drawn'. Which of")
    print("its rows this scene actually shares:")
    mine = set()
    for tag in sorted(geom_by_scene):
        for name, (w, h, n, _pal) in sorted(geom_by_scene[tag].items()):
            mine.add((tag, w, h, n))
    for label, w, h, n, val in H2_ROWS:
        tags = sorted({t for t, tw, th, tn in mine if (tw, th, tn) == (w, h, n)})
        print("  %-26s %8s B   %s"
              % (label, f"{val:,}",
                 "this scene draws exactly that shape in: %s" % ", ".join(tags)
                 if tags else "no such asset in this scene (dimensions differ)"))
    print()

    print("colours (art/CRITIQUE.md's palette criterion):")
    for tag, d in SCENES:
        manifest = os.path.join(d, "assets.json")
        with open(manifest) as f:
            doc = json.load(f)
        files = {s["name"]: os.path.normpath(os.path.join(d, s["file"]))
                 for s in doc["sprites"]}
        for name in sorted(geom_by_scene[tag]):
            pal = geom_by_scene[tag][name][3]
            key_hex = doc["palettes"][pal]["colors"][0]
            key = tuple(int(key_hex[i:i + 2], 16) for i in (1, 3, 5))
            w, h, px = read_png(files[name])
            vis = sorted({c for c in set(px) if c[:3] != key})
            gap = None
            for i in range(len(vis)):
                for j in range(i + 1, len(vis)):
                    a, b = vis[i], vis[j]
                    g = max(abs(a[k] - b[k]) for k in range(3))
                    gap = g if gap is None else min(gap, g)
            iso, tot = speckle(w, h, px)
            print("  %s  %-14s visible %2d of 15 bank colours, key %5d px, "
                  "tightest pair %s/255, speckle %.3f%%"
                  % (tag, name, len(vis),
                     sum(1 for c in px if c[:3] == key),
                     "n/a" if gap is None else str(gap),
                     100.0 * iso / tot))
    print()

    print("outline (art/CRITIQUE.md: outline consistency). The rig's outline")
    print("pass is grow_outline(palette slot 1) at thickness `outline: 1`,")
    print("verbatim at both scales — so 1 scene px of ring measures 4 screen")
    print("px in the 480 capture and 2 screen px in the 960 capture.")
    print()
    for tag, d in SCENES:
        manifest = os.path.join(d, "assets.json")
        with open(manifest) as f:
            doc = json.load(f)
        by_name = {s["name"]: s for s in doc["sprites"]}
        pal = by_name["samurai_idle"]["palette"]
        hexv = doc["palettes"][pal]["colors"][1]
        ring = tuple(int(hexv[i:i + 2], 16) for i in (1, 3, 5))
        path = os.path.normpath(os.path.join(d, by_name["samurai_idle"]["file"]))
        w, h, px = read_png(path)
        fw = w // 4
        ring0 = sum(1 for y in range(h) for x in range(fw)
                    if px[y * w + x][:3] == ring)
        solid0 = sum(1 for y in range(h) for x in range(fw)
                     if px[y * w + x][:3] != (0, 0, 0))
        print("  %s  hero frame %dx%d, ring colour %s: %d ring px, "
              "%d drawn px, ring = %.1f%% of the sprite"
              % (tag, fw, h, hexv, ring0, solid0, 100.0 * ring0 / solid0))
    return 0


def speckle(w, h, px):
    """Pixels differing from ALL four orthogonal neighbours: art/CRITIQUE.md's
    'noise' criterion, counted rather than eyeballed. A regenerated sprite
    that is a clean geometric scale keeps this RATE (the count grows ~4x with
    the area); one that dithers or speckles sees the rate climb."""
    iso = 0
    for y in range(h):
        for x in range(w):
            c = px[y * w + x]
            n = 0
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h and px[ny * w + nx] != c:
                    n += 1
            if n == (bool(x) + bool(x + 1 < w) + bool(y) + bool(y + 1 < h)):
                iso += 1
    return iso, w * h


if __name__ == "__main__":
    sys.exit(main())
