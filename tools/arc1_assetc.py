#!/usr/bin/env python3
"""arc1-assetc — compile sprite art into a generated C header (Wave 1, v0).

Contract (docs/ROADMAP_CREATION_FLOW.md card 1.1):

    python3 tools/arc1_assetc.py --assets <assets.json> --out-dir <dir> [--report]

    exit 0 = compiled; 1 = validation error (stderr, one per asset);
    2 = usage/IO error

assets.json is self-describing ("format": 1):

    { "format": 1,
      "palettes": { "world": { "bank": 3, "colors": ["#000000", "#F8D8A8"] } },
      "sprites":  [ { "name": "player", "file": "art/player.png",
                      "palette": "world", "frames": 6 } ] }

Sprite images are horizontal strips: image width = frames x frame width.
The output header is byte-deterministic (no timestamps, fixed
16-bytes-per-line hex, names emitted in sorted order). The committed header
is the build truth and this tool is the master: regenerate, never hand-edit.
"""
import argparse
import json
import sys

FORMAT = 1

# Errors are collected per asset (exactly one line per failing asset) and
# printed together, so one run shows every broken entry, not just the first.
errors = []


def fail_asset(where, msg):
    errors.append("%s: %s" % (where, msg))


def load_document(path):
    """Read assets.json. IO problems exit 2; content problems exit 1."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        sys.stderr.write("arc1-assetc: cannot read %s: %s\n" % (path, e.strerror))
        sys.exit(2)
    try:
        doc = json.loads(text)
    except ValueError as e:
        sys.stderr.write("arc1-assetc: %s is not valid JSON: %s\n" % (path, e))
        sys.exit(2)
    return doc


def validate_document(doc):
    """Structural checks shared by every later stage."""
    if not isinstance(doc, dict):
        fail_asset("assets.json", 'top level must be an object with "format"')
        return
    fmt = doc.get("format")
    if fmt != FORMAT:
        fail_asset("assets.json",
                   'format %d expected (got %s) — set "format": %d'
                   % (FORMAT, json.dumps(fmt), FORMAT))
    if not isinstance(doc.get("palettes", {}), dict):
        fail_asset("assets.json", '"palettes" must be an object keyed by palette name')
    if not isinstance(doc.get("sprites", []), list):
        fail_asset("assets.json", '"sprites" must be an array')


def compile_assets(doc, out_dir, report):
    """Compile every asset. Card 1.1 skeleton: structure only, no output yet
    (the palette module, image loader and emitter arrive as cards 1.2-1.4)."""
    validate_document(doc)
    return {}   # emitted bytes per asset, filled by the emitter card


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="arc1-assetc",
        description="compile assets.json (format 1) into a generated C header")
    ap.add_argument("--assets", required=True, metavar="FILE",
                    help="asset manifest, JSON, format 1")
    ap.add_argument("--out-dir", required=True, metavar="DIR",
                    help="directory for the generated header(s)")
    ap.add_argument("--report", action="store_true",
                    help="print greppable KEY lines (SPRITE/PALETTE/ROM/QUANT)")
    a = ap.parse_args(argv)          # usage errors exit 2

    doc = load_document(a.assets)    # IO errors exit 2
    emitted = compile_assets(doc, a.out_dir, a.report)

    if errors:
        for e in errors:
            sys.stderr.write("arc1-assetc: %s\n" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
