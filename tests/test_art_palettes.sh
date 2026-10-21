#!/bin/sh
# ROADMAP_V4 card 7.1: the art pipeline's workspace — palette presets, the
# style template, the tool package, the provenance schema.
#
# Acceptance (pinned):
#   1. every palette preset has exactly 16 entries: index 0 is the
#      transparent key, the 15 colours are distinct as RGB565, and the
#      outline index holds a dark colour;
#   2. art/STYLE_TEMPLATE.md declares every required field (the lint);
#   3. tools/arc1_art/ holds one module per remaining card (7.2-7.6);
#   4. docs/art/PROVENANCE.md states the schema, and art/ART_LICENSES.md is
#      the generated summary of it (and is up to date);
#   5. art/README.md states the pipeline's rules.
#
# Host-side only: it reads JSON and markdown, and runs the licence
# generator into a temp dir to compare.
#
# Usage: test_art_palettes.sh [repo-root]
set -eu

ROOT=${1:-$(cd "$(dirname "$0")/.." && pwd)}
case "$ROOT" in /*) ;; *) ROOT="$(pwd)/$ROOT" ;; esac

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-art-palettes: FAILED — $*" >&2; exit 1; }
CHECKS=0

# --- 1) the palette presets ------------------------------------------------
PRESETS="neutral16 warm16 cool16 dusk16 forest16 ui16"
for p in $PRESETS; do
    f="$ROOT/art/palettes/$p.json"
    [ -f "$f" ] || fail "missing preset $p"
    python3 - "$f" "$p" <<'PY' || fail "preset $p is invalid"
import json, sys

path, name = sys.argv[1], sys.argv[2]
doc = json.load(open(path))
colors = doc.get("colors")
assert isinstance(colors, list), "%s: no colors array" % name
assert len(colors) == 16, "%s: %d colours, want 16" % (name, len(colors))
assert doc.get("outline") == 1, "%s: the outline index must be 1" % name

def rgb565(h):
    v = int(h[1:], 16)
    r, g, b = (v >> 16) & 255, (v >> 8) & 255, v & 255
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def lum(h):
    v = int(h[1:], 16)
    r, g, b = (v >> 16) & 255, (v >> 8) & 255, v & 255
    return 0.299 * r + 0.587 * g + 0.114 * b

for c in colors:
    assert isinstance(c, str) and len(c) == 7 and c[0] == "#", \
        "%s: %r is not #RRGGBB" % (name, c)

key = colors[0]
seen = {}
for i, c in enumerate(colors):
    v = rgb565(c)
    assert v not in seen, "%s: %s and %s collapse to the same RGB565" % (
        name, c, seen[v])
    seen[v] = c
# index 0 is the transparent key: it must NOT be one of the painted colours
assert rgb565(key) not in [rgb565(c) for c in colors[1:]], \
    "%s: the key colour collides with a painted colour" % name
# the outline (index 1) is dark: it must read as an edge, not as a fill
assert lum(colors[1]) < 64, "%s: the outline colour %s is not dark" % (
    name, colors[1])
# ...and it must be the DARKEST colour in the bank: the checker infers the
# outline as the darkest colour among the frame's edge pixels, so a bank
# whose index 1 is only just the darkest fails the check by accident
# (ROADMAP_V4 card 7.8, the round's doc gap 4).
others = [lum(c) for i, c in enumerate(colors) if i not in (0, 1)]
assert lum(colors[1]) < min(others) - 4, (
    "%s: the outline %s (luminance %.1f) is not clearly the darkest "
    "colour in the bank (the next is %.1f)"
    % (name, colors[1], lum(colors[1]), min(others)))
PY
    CHECKS=$((CHECKS + 1))
done
echo "  ok: all 6 palette presets — 16 entries, distinct as RGB565, dark outline"

# --- 2) the style template lint -------------------------------------------
T="$ROOT/art/STYLE_TEMPLATE.md"
[ -f "$T" ] || fail "missing art/STYLE_TEMPLATE.md"
for field in "Sprite grid and sizes" "Outline rule" "Light direction" \
             "Shading steps" "Palette" "Silhouette rules" "Animation" \
             "Provenance"; do
    grep -q "$field" "$T" || fail "STYLE_TEMPLATE.md has no '$field' section"
done
for field in "Frame size" "Frames per state" "Anchor" "Outline colour" \
             "Light comes from" "Steps per material" "Preset" "Readable at 1x" \
             "Frame counts" "Tool" "Licence"; do
    grep -q "$field" "$T" || fail "STYLE_TEMPLATE.md has no '$field' field"
done
CHECKS=$((CHECKS + 1))
echo "  ok: STYLE_TEMPLATE.md declares every required field"

# --- 3) the tool package ----------------------------------------------------
[ -f "$ROOT/tools/arc1_art/__init__.py" ] || fail "missing tools/arc1_art/"
for m in canvas pixmap rig ingest qa; do
    [ -f "$ROOT/tools/arc1_art/$m.py" ] || fail "missing tools/arc1_art/$m.py"
    grep -q "$m" "$ROOT/tools/arc1_art/__init__.py" || \
        fail "tools/arc1_art/__init__.py does not mention $m"
done
CHECKS=$((CHECKS + 1))
echo "  ok: tools/arc1_art/ holds one module per remaining card (7.2-7.6)"

# --- 4) the provenance schema and the generated summary ---------------------
P="$ROOT/docs/art/PROVENANCE.md"
[ -f "$P" ] || fail "missing docs/art/PROVENANCE.md"
for key in tool model prompt seed licence; do
    grep -q "\`$key\`" "$P" || fail "the provenance schema does not mention '$key'"
done
L="$ROOT/art/ART_LICENSES.md"
[ -f "$L" ] || fail "missing art/ART_LICENSES.md (run: make art-licenses)"
grep -q "DO NOT EDIT" "$L" || fail "art/ART_LICENSES.md is not marked generated"
grep -q "| asset | source | tool |" "$L" || fail "ART_LICENSES.md has no table"
# it must be up to date: regenerate it and compare
python3 "$ROOT/tools/gen_art_licenses.py" > /dev/null || fail "the generator failed"
cp "$L" "$OUT/ART_LICENSES.md"
python3 "$ROOT/tools/gen_art_licenses.py" > /dev/null
cmp -s "$OUT/ART_LICENSES.md" "$L" || fail "art/ART_LICENSES.md is stale"
CHECKS=$((CHECKS + 1))
echo "  ok: the provenance schema and the generated ART_LICENSES.md (up to date)"

# --- 5) the pipeline's rules ------------------------------------------------
R="$ROOT/art/README.md"
[ -f "$R" ] || fail "missing art/README.md"
for rule in "Palette index 0 is transparent" "outline" "distinct as RGB565" \
            "one palette bank per asset group" "share their size" \
            "regenerated by one command" "Provenance is recorded"; do
    grep -qi "$rule" "$R" || fail "art/README.md does not state: $rule"
done
CHECKS=$((CHECKS + 1))
echo "  ok: art/README.md states the pipeline's rules"

echo "test-art-palettes: ok ($CHECKS checks, card 7.1)"
