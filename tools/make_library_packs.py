#!/usr/bin/env python3
"""tools/make_library_packs.py — bring the rounds' art into the library.

The two clean-room rounds painted good art and then buried it in their
own folders. The library is the same art, tidied: each round's generator
becomes a *pack* — a generator, a manifest fragment, a style sheet and the
strips it produces — with the game's own map dropped (a map belongs to a
game, not to a library).

This script does the mechanical part: it copies a round's generator into
its pack and patches the paths and the manifest. The painting code is
untouched, so the strips come out byte-identical to the round's.

    python3 tools/make_library_packs.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PACKS = [
    # (the round's generator, the pack name, the manifest fragment, a
    #  description, the sprite names to drop with the game)
    ("round6/art/gen_round6.py", "village", "village.json",
     "A village's cast and ground: a farmer, a slime, four harvests and a "
     "three-tile ground (grass, a paved square, a pond).", []),
    ("round7/art/gen_round7.py", "lighthouse", "lighthouse.json",
     "A lighthouse's cast and tiles: the keeper (three states), the tower "
     "lamp, and four tilesets (a dusk sky, the tower, the cliff, the sea).",
     []),
]


def patch(source, pack, fragment, description):
    s = open(source, encoding="utf-8").read()

    # --- the paths --------------------------------------------------------
    # the pack lives one level deeper than the round did
    s = s.replace('''ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(HERE))''',
                  '''ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(os.path.dirname(HERE)))''')
    s = s.replace('ROUND = os.path.dirname(HERE)', 'ROUND = HERE')
    s = s.replace('''ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(os.path.dirname(HERE))''',
                  '''ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(os.path.dirname(HERE)))''')
    # the pack's generator must be able to write anywhere: the library's
    # check regenerates it into a temporary directory to prove the bytes
    s = s.replace('''STRIP_DIR = os.path.join(HERE, "strips")''',
                  '''# the pack's output directory: --out-dir, or the pack itself
_OUT = HERE
if "--out-dir" in sys.argv:
    _OUT = os.path.abspath(sys.argv[sys.argv.index("--out-dir") + 1])
STRIP_DIR = os.path.join(_OUT, "strips")''')
    s = s.replace('''STRIP_DIR = os.path.join(ROUND, "art", "strips")''',
                  '''# the pack's output directory: --out-dir, or the pack itself
_OUT = HERE
if "--out-dir" in sys.argv:
    _OUT = os.path.abspath(sys.argv[sys.argv.index("--out-dir") + 1])
STRIP_DIR = os.path.join(_OUT, "strips")''')
    s = s.replace('''MAP_DIR = os.path.join(HERE, "maps")''',
                  '''MAP_DIR = os.path.join(_OUT, "maps")''')
    s = s.replace('''MAP_DIR = os.path.join(ROUND, "art")''',
                  '''MAP_DIR = os.path.join(_OUT, "maps")''')
    s = s.replace('''MANIFEST = os.path.join(HERE, "%s")''' % fragment,
                  '''MANIFEST = os.path.join(_OUT, "%s")''' % fragment)
    s = s.replace('''manifest = os.path.join(HERE, "%s")''' % fragment,
                  '''manifest = os.path.join(_OUT, "%s")''' % fragment)
    s = s.replace('''MAP_FILE = os.path.join(HERE, "maps", "village.txt")''',
                  '''MAP_FILE = os.path.join(MAP_DIR, "village.txt")''')
    s = s.replace('''SOLID_FILE = os.path.join(HERE, "maps", "
                  "village_solid.txt")''',
                  '''SOLID_FILE = os.path.join(MAP_DIR, "village_solid.txt")''')
    s = s.replace('STRIP_DIR = os.path.join(ROUND, "art", "strips")',
                  'STRIP_DIR = os.path.join(HERE, "strips")')
    s = s.replace('MAP_DIR = os.path.join(ROUND, "art")',
                  'MAP_DIR = os.path.join(HERE, "maps")')
    s = s.replace('MANIFEST = os.path.join(ROUND, "assets.json")',
                  'MANIFEST = os.path.join(_OUT, "%s")' % fragment)
    s = s.replace('manifest = os.path.join(ROUND, "assets.json")',
                  'manifest = os.path.join(_OUT, "%s")' % fragment)
    s = s.replace('MAP_FILE = os.path.join(HERE, "village.txt")',
                  'MAP_FILE = os.path.join(HERE, "maps", "village.txt")')
    s = s.replace('SOLID_FILE = os.path.join(HERE, "village_solid.txt")',
                  'SOLID_FILE = os.path.join(HERE, "maps", '
                  '"village_solid.txt")')
    s = s.replace('"file": "art/strips/', '"file": "strips/')
    s = s.replace('"file": "art/', '"file": "maps/')
    s = s.replace('"file": "strips/', '"file": "strips/')


    # --- the game's map belongs to the game, not to the library ----------
    # The pack offers the tilesets and the cast; a map (and its legend and
    # its collision layer) is the game's own.
    s = re.sub(r"    write_text\(MAP_FILE[^\n]*\n", "", s)
    s = re.sub(r"    write_text\(SOLID_FILE[^\n]*\n", "", s)
    s = re.sub(r"    written\.append\(write_map\([^\n]*\n", "", s)
    s = re.sub(r"    maps = \[.*?\n    \]\n", "    maps = []\n", s, flags=re.S)
    s = re.sub(r'        "maps": \[.*?\n        \],\n', '        "maps": [],\n',
               s, flags=re.S)
    s = s.replace('"maps": maps,', '"maps": [],')

    # --- the manifest: a fragment, with a description --------------------
    s = s.replace('"format": 1,\n        "palettes"',
                  '"format": 1,\n        "description": %r,\n        "palettes"'
                  % description)
    return s


def main():
    for source, pack, fragment, description, _drop in PACKS:
        src = os.path.join(ROOT, source)
        if not os.path.isfile(src):
            sys.stderr.write("make_library_packs: no such generator: %s\n" % src)
            return 1
        out_dir = os.path.join(ROOT, "art", "library", pack)
        os.makedirs(out_dir, exist_ok=True)
        out = os.path.join(out_dir, "gen_%s.py" % pack)
        text = patch(src, pack, fragment, description)
        with open(out, "w", encoding="utf-8") as f:
            f.write(text)
        print("pack %s: %s" % (pack, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
