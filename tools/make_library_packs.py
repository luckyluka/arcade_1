#!/usr/bin/env python3
"""tools/make_library_packs.py — bring the rounds' art into the library.

The two clean-room rounds painted good art and then buried it in their
own folders. The library is the same art, tidied: each round's generator
becomes a *pack* — a generator, a manifest fragment, a style sheet and the
strips it produces — with the game's own map dropped (a map belongs to a
game, not to a library).

This script does the mechanical part: it copies a round's generator into
its pack and patches the paths, the manifest and the palette banks. The
painting code is untouched, so the strips come out byte-identical to the
round's.

    python3 tools/make_library_packs.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PACKS = [
    # (the round's generator, the pack name, the manifest fragment, a
    #  description)
    ("round6/art/gen_round6.py", "village", "village.json",
     "A village's cast and ground: a farmer, a slime, four harvests and a "
     "three-tile ground (grass, a paved square, a pond)."),
    ("round7/art/gen_round7.py", "lighthouse", "lighthouse.json",
     "A lighthouse's cast and tiles: the keeper (three states), the tower "
     "lamp, and four tilesets (a dusk sky, the tower, the cliff, the sea)."),
]

# The machine has SIXTEEN palette banks. A pack's banks must be inside
# 0..15, and — where possible — out of each other's way, so that a merge
# has room to resolve what is left.
VILLAGE_BANKS = {"farmer": 5, "slime": 6, "props": 7, "ground": 8}
LIGHTHOUSE_BANKS = {"sky16": 9, "tower16": 10, "rock16": 11, "sea16": 12,
                    "keeper16": 13, "lamp16": 14, "ui16": 15}


def patch(source, pack, fragment, description):
    s = open(source, encoding="utf-8").read()

    # --- the paths --------------------------------------------------------
    s = s.replace('ROUND = os.path.dirname(HERE)', 'ROUND = HERE')
    s = s.replace('''ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(HERE))''',
                  '''ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(os.path.dirname(HERE)))''')
    s = s.replace('''ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(os.path.dirname(HERE))''',
                  '''ROOT = os.environ.get("ARCADE1_ROOT") or os.path.dirname(
    os.path.dirname(os.path.dirname(HERE)))''')
    # the pack's output directory: --out-dir, or the pack itself
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
    s = s.replace('''MANIFEST = os.path.join(ROUND, "assets.json")''',
                  '''MANIFEST = os.path.join(_OUT, "%s")''' % fragment)
    s = s.replace('''MANIFEST = os.path.join(HERE, "%s")''' % fragment,
                  '''MANIFEST = os.path.join(_OUT, "%s")''' % fragment)
    s = s.replace('''manifest = os.path.join(ROUND, "assets.json")''',
                  '''manifest = os.path.join(_OUT, "%s")''' % fragment)
    s = s.replace('''manifest = os.path.join(HERE, "%s")''' % fragment,
                  '''manifest = os.path.join(_OUT, "%s")''' % fragment)
    s = s.replace('''MAP_FILE = os.path.join(HERE, "maps", "village.txt")''',
                  '''MAP_FILE = os.path.join(MAP_DIR, "village.txt")''')
    s = s.replace('''SOLID_FILE = os.path.join(HERE, "maps", "
                  "village_solid.txt")''',
                  '''SOLID_FILE = os.path.join(MAP_DIR, "village_solid.txt")''')
    s = s.replace('"file": "art/strips/', '"file": "strips/')
    s = s.replace('"file": "art/', '"file": "maps/')

    # --- the palette banks -------------------------------------------------
    for name, bank in VILLAGE_BANKS.items():
        s = s.replace('"%s": %d,' % (name, bank - 4), '"%s": %d,' % (name, bank))
    s = s.replace('BANKS = {"farmer": 1, "slime": 2, "props": 3, "ground": 4}',
                  'BANKS = {"farmer": 5, "slime": 6, "props": 7, "ground": 8}')
    for name, bank in LIGHTHOUSE_BANKS.items():
        s = re.sub(r'"%s": \{"bank": \d' % name,
                   '"%s": {"bank": %d' % (name, bank), s)

    # --- the game's map belongs to the game, not to the library ----------
    s = re.sub(r"    write_text\(MAP_FILE[^\n]*\n", "", s)
    s = re.sub(r"    write_text\(SOLID_FILE[^\n]*\n", "", s)
    s = re.sub(r"    written\.append\(write_map\([^\n]*\n", "", s)
    s = re.sub(r"    maps = \[.*?\n    \]\n", "    maps = []\n", s, flags=re.S)
    s = re.sub(r'        "maps": \[.*?\n        \],\n', '        "maps": [],\n',
               s, flags=re.S)
    s = s.replace('"maps": maps,', '"maps": [],')
    s = s.replace("len(animations), len(maps)))", "len(animations), 0))")

    # --- the roles: what kind of asset each one is ----------------------
    roles = ('"roles": {\n'
             '            "ground": "tileset", "sky": "tileset",\n'
             '            "tower": "tileset", "rock": "tileset",\n'
             '            "sea": "tileset",\n'
             '        },\n')
    s = s.replace('''        "format": 1,
        "palettes"''',
                  '''        "format": 1,
''' + roles + '''        "description": %r,
        "palettes"''' % description)
    return s


def main():
    for source, pack, fragment, description in PACKS:
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
