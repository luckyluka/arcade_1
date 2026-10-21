#!/usr/bin/env python3
"""tests/fixtures/manifest_fixture.py — a tiny manifest for the QA tests.

A tileset of two tiles, and a map that uses only the first: the spare tile
is what the manifest check must REPORT (a warning — a spare tile is
legitimate, but a tile nobody can draw is invisible in the game).

    python3 tests/fixtures/manifest_fixture.py <out-dir>
"""
import json
import os
import sys

out = sys.argv[1]
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "..", "tools"))
from arc1_art import canvas

COLOURS = ["#000000", "#101018", "#202028", "#303040"]

tiles = canvas.Canvas(32, 16)
tiles.rect(0, 0, 16, 16, 2)
tiles.rect(16, 0, 16, 16, 3)
canvas.to_strip([tiles], COLOURS, os.path.join(out, "tiles.png"))

with open(os.path.join(out, "map.txt"), "w") as f:
    f.write("g\n")

manifest = {
    "format": 1,
    "palettes": {"p": {"bank": 0, "colors": COLOURS}},
    "sprites": [{"name": "tiles", "file": "tiles.png", "palette": "p",
                 "frames": 1}],
    "maps": [{"name": "lvl", "file": "map.txt", "tileset": "tiles",
              "tile_w": 16, "tile_h": 16, "legend": {"g": 0}}],
}
with open(os.path.join(out, "assets.json"), "w") as f:
    json.dump(manifest, f)
print("fixture: a two-tile tileset, a map that uses one of them")
