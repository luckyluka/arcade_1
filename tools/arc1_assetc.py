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

Palette semantics (card 1.2): a palette fills one device bank — colors[i]
lands at PALETTE[bank*16 + i], at most 16 colors, bank 0..15. The device
never DRAWS nibble 0 (transparent), so colors[0] is the declared key /
transparent slot and colors[1..15] are the drawable colours; art
conventions keep at most 15 colours plus transparent per bank.
RGB565 = (r>>3)<<11 | (g>>2)<<5 | (b>>3) — device upload semantics.
"""
import argparse
import json
import sys

FORMAT = 1

# Errors are collected per asset (exactly one line per failing asset) and
# printed together, so one run shows every broken entry, not just the first.
errors = []
_errored_assets = set()


def fail_asset(where, msg):
    """Record one error per asset: the first failure of an asset wins."""
    if where in _errored_assets:
        return
    _errored_assets.add(where)
    errors.append("%s: %s" % (where, msg))


class AutoPaletteError(Exception):
    """An auto palette exceeded the 15 drawable slots (ROADMAP_V3 4.2)."""


def rgb565(r, g, b):
    """Device upload semantics (CHEATSHEET palette section)."""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def parse_hex_color(text):
    """Strict #RRGGBB -> (r, g, b); None when the shape is wrong."""
    if not isinstance(text, str) or len(text) != 7 or text[0] != "#":
        return None
    try:
        v = int(text[1:], 16)
    except ValueError:
        return None
    return (v >> 16 & 0xFF, v >> 8 & 0xFF, v & 0xFF)


def build_palette(name, spec):
    """Validate one named palette; return {"bank", "entries"} or None.

    entries is the full 16-slot bank in device order (index 0 = transparent
    slot value 0). Teaching errors are pinned by cards 1.2 / ROADMAP_V3 4.2.
    """
    where = "palette %s" % name
    if not isinstance(spec, dict):
        fail_asset(where, 'must be an object with "bank" and "colors"')
        return None

    bank = spec.get("bank")
    if not isinstance(bank, int) or isinstance(bank, bool):
        fail_asset(where, 'missing or non-integer "bank" — set "bank" to 0..15')
        return None
    if not 0 <= bank <= 15:
        fail_asset(where, "bank %d out of range 0..15" % bank)
        return None

    colors = spec.get("colors")
    if not isinstance(colors, list):
        fail_asset(where, 'missing or non-array "colors" — list "#RRGGBB" strings')
        return None
    if len(colors) > 16:
        fail_asset(where,
                   "%d colors (max 16) — merge colors or add a second bank"
                   % len(colors))
        return None

    entries = []
    seen_at = {}
    for i, text in enumerate(colors):
        rgb = parse_hex_color(text)
        if rgb is None:
            fail_asset(where, 'bad color %s — use #RRGGBB' % json.dumps(text))
            return None
        val = rgb565(*rgb)
        if val in seen_at:
            fail_asset(where,
                       "duplicate color %s at indices %d and %d — list each "
                       "color once" % (text, seen_at[val], i))
            return None
        seen_at[val] = i
        entries.append(val)

    entries += [0] * (16 - len(entries))
    return {"bank": bank, "entries": entries}


def collect_auto(sprite_name, pixels):
    """Build a "palette": "auto" bank from pixel values in first-appearance
    order (row-major over all frames).

    `pixels` yields the RGB565 value of every pixel in order, or None for a
    transparent one (skipped). Assigns indices 1..15. All distinct colours
    are counted first so the teaching error reports the art's true total
    (ROADMAP_V3 4.2 shows "19 colours", not "16").
    """
    colors = []
    for p in pixels:
        if p is None:
            continue
        if p not in colors:
            colors.append(p)
    if len(colors) > 15:
        raise AutoPaletteError(
            'sprite %s: %d colours (max 15 + transparent) — reduce '
            'colours, use "quantize": "nearest" with an explicit '
            'palette, or split the sheet' % (sprite_name, len(colors)))
    return {"entries": [0] + colors + [0] * (15 - len(colors))}


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


def parse_palettes(doc):
    """Validate every named palette; returns {name: palette} (valid ones)."""
    out = {}
    for name in sorted(doc.get("palettes", {})):
        pal = build_palette(name, doc["palettes"][name])
        if pal is not None:
            out[name] = pal
    return out


def sprite_label(index, sprite):
    """How a sprite is addressed in errors: by name, else by position."""
    if isinstance(sprite, dict) and isinstance(sprite.get("name"), str) \
            and sprite["name"]:
        return sprite["name"]
    return "#%d" % (index + 1)


def validate_sprites(doc, palettes):
    """Palette-side sprite checks: the palette field, auto/bank pairing,
    and the reference to a defined palette (card 1.2 + ROADMAP_V3 4.2)."""
    for i, sprite in enumerate(doc.get("sprites", [])):
        where = sprite_label(i, sprite)
        if not isinstance(sprite, dict):
            fail_asset(where, "each sprite must be an object")
            continue
        pal_ref = sprite.get("palette")
        if not isinstance(pal_ref, str) or not pal_ref:
            fail_asset(where,
                       'missing "palette" — name a palette or use "palette": "auto"')
            continue
        if pal_ref == "auto":
            bank = sprite.get("bank")
            if not isinstance(bank, int) or isinstance(bank, bool):
                fail_asset(where,
                           '"palette": "auto" requires "bank" (0..15)')
                continue
            if not 0 <= bank <= 15:
                fail_asset(where, "bank %d out of range 0..15" % bank)
                continue
        else:
            if "bank" in sprite:
                fail_asset(where,
                           '"bank" is only valid with "palette": "auto" — '
                           'remove it, or set "palette": "auto"')
                continue
            if pal_ref not in palettes:
                fail_asset(where,
                           "palette %s not defined — add it under "
                           '"palettes" or use "palette": "auto"'
                           % json.dumps(pal_ref))


def compile_assets(doc, out_dir, report):
    """Validate (and later compile) every asset. Cards 1.3-1.4 add the
    image loader, quantization and the header emitter."""
    validate_document(doc)
    if errors:
        return {}                     # document-level errors: stop here
    palettes = parse_palettes(doc)
    validate_sprites(doc, palettes)
    return {}                        # emitted bytes per asset (emitter, 1.4)


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
    compile_assets(doc, a.out_dir, a.report)

    if errors:
        for e in errors:
            sys.stderr.write("arc1-assetc: %s\n" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
