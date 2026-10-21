#!/bin/sh
# ROADMAP_V4 card 7.8: the four DOC GAPs the round's rehearsal found, and
# their fixes. Each one is pinned here, so a regression is a test failure
# rather than a fresh agent's afternoon.
#
#   1. the generator's path assumption is documented (and ARCADE1_ROOT works)
#   2. "a tileset is ONE frame" is in the error message
#   3. the SDK defines ARCADE1_SCREEN_W / ARCADE1_SCREEN_H
#   4. `arcade1 build` carries the game's own include path
#
# Host-side only (the build check compiles a throwaway guest).
#
# Usage: test_doc_gaps.sh [repo-root]
set -eu

ROOT=${1:-$(cd "$(dirname "$0")/.." && pwd)}
case "$ROOT" in /*) ;; *) ROOT="$(pwd)/$ROOT" ;; esac

CLI=$ROOT/build/arcade1
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-doc-gaps: FAILED — $*" >&2; exit 1; }
CHECKS=0

[ -x "$CLI" ] || fail "no CLI at $CLI (make build/arcade1)"

# --- 1) the generator's path assumption is documented -----------------------
grep -q "ARCADE1_ROOT" "$ROOT/art/README.md" || \
    fail "art/README.md does not document how a generator finds the library"
grep -q "ModuleNotFoundError" "$ROOT/art/README.md" || \
    fail "art/README.md does not name the symptom"
grep -q "ONE frame" "$ROOT/art/README.md" || \
    fail "art/README.md does not state the one-frame tileset rule"
grep -q "ulimit" "$ROOT/art/README.md" || \
    fail "art/README.md does not warn about unbounded allocations"
CHECKS=$((CHECKS + 1))
echo "  ok: the generator's rules are documented"

# --- 2) the tileset error teaches ------------------------------------------
python3 - "$ROOT" "$OUT" <<'PY' || exit 1
import json, os, sys
root, out = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.join(root, "tools"))
from arc1_art import canvas

# a tileset written as THREE FRAMES instead of one frame of three tiles:
# the mistake the rehearsal made.
frames = [canvas.Canvas(24, 24) for _ in range(3)]
for i, f in enumerate(frames):
    f.rect(0, 0, 24, 24, i + 1)
canvas.to_strip(frames, ["#000000", "#112233", "#223344", "#334455"],
                os.path.join(out, "tiles.png"))
manifest = {
    "format": 1,
    "palettes": {"p": {"bank": 0, "colors": ["#000000", "#112233",
                                            "#223344", "#334455"]}},
    "sprites": [{"name": "tiles", "file": "tiles.png", "palette": "p",
                 "frames": 3}],
    "maps": [{"name": "lvl", "file": "lvl.txt", "tileset": "tiles",
              "tile_w": 24, "tile_h": 24, "legend": {"g": 0, "p": 1}}],
}
with open(os.path.join(out, "assets.json"), "w") as f:
    json.dump(manifest, f)
with open(os.path.join(out, "lvl.txt"), "w") as f:
    f.write("gp\n")
PY
rc=0
python3 "$ROOT/tools/arc1_assetc.py" --assets "$OUT/assets.json" \
    --out-dir "$OUT" > "$OUT/err.txt" 2>&1 || rc=$?
[ "$rc" -eq 1 ] || fail "the three-frame tileset should be a validation error"
grep -q "must be ONE frame" "$OUT/err.txt" || \
    fail "the error does not teach the one-frame rule: $(cat "$OUT/err.txt")"
CHECKS=$((CHECKS + 1))
echo "  ok: the tileset error teaches the one-frame rule"

# --- 3) the SDK defines the screen size -----------------------------------
grep -q "#define ARCADE1_SCREEN_W 480" "$ROOT/programs/arcade1.h" || \
    fail "arcade1.h does not define ARCADE1_SCREEN_W"
grep -q "#define ARCADE1_SCREEN_H 270" "$ROOT/programs/arcade1.h" || \
    fail "arcade1.h does not define ARCADE1_SCREEN_H"
CHECKS=$((CHECKS + 1))
echo "  ok: the SDK defines the screen size"

# --- 4) `arcade1 build` carries the game's own include path ---------------
# A game whose generated header sits BESIDE it, compiled from another
# directory: without the fix the include does not resolve.
mkdir -p "$OUT/game"
cat > "$OUT/game/assets.h" <<'EOF'
/* the generated header, beside the game */
static const int assets_marker = 1;
EOF
cat > "$OUT/game/game.c" <<'EOF'
#include "arcade1.h"
#include "assets.h"      /* the generated header, beside this file */
int main(void)
{
    for (;;) {
        arcade1_clear(0x0000u);
        arcade1_rect(0, 0, 4, 4, 0xF800u);
        arcade1_present();
    }
}
EOF
rc=0
(cd "$OUT" && "$CLI" build game/game.c game/game.arc1) > "$OUT/build.log" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || \
    fail "the build did not find the header beside the game: $(tail -3 "$OUT/build.log")"
[ -f "$OUT/game/game.arc1" ] || fail "no image was written"
CHECKS=$((CHECKS + 1))
echo "  ok: the build carries the game's own include path"

echo "test-doc-gaps: ok ($CHECKS checks, card 7.8)"
