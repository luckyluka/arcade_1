#!/bin/sh
# ROADMAP_V4 card 7.6: the visual QA tool.
#
# The fixtures are SYNTHESIZED with the canvas library — one good sprite
# and three deliberately bad ones (too many colours, a jittery baseline,
# orphan pixels). No image is committed, and none is needed.
#
# Acceptance (pinned):
#   1. the good fixture passes, and the report carries its keys;
#   2. each bad fixture fails with ITS message (the colour count, the
#      baseline, the orphan pixels);
#   3. the five .px samples pass;
#   4. the humanoid rig's strips pass.
#
# Host-side only.
#
# Usage: test_art_qa.sh [repo-root]
set -eu

ROOT=${1:-$(cd "$(dirname "$0")/.." && pwd)}
case "$ROOT" in /*) ;; *) ROOT="$(pwd)/$ROOT" ;; esac

ART=$ROOT/tools/arc1-art
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-art-qa: FAILED — $*" >&2; exit 1; }
CHECKS=0

[ -x "$ART" ] || fail "no arc1-art at $ART"

# --- the fixtures ----------------------------------------------------------
python3 - "$ROOT" "$OUT" <<'PY' || exit 1
import os, sys
sys.path.insert(0, os.path.join(sys.argv[1], "tools"))
from arc1_art import canvas

out = sys.argv[2]
PAL = [(0, 0, 0), (10, 10, 20), (200, 200, 210), (120, 60, 40)]


def write(name, frames, w, h):
    """A strip PNG: the frames side by side."""
    exp = [(0, 0, 0)] * len(PAL)
    for i, (r, g, b) in enumerate(PAL):
        exp[i] = (r, g, b)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for frame in frames:
            for x in range(w):
                raw += bytes(exp[frame[y][x]])
    canvas.write_indexed_png(os.path.join(out, name), raw, w * len(frames), h)


# a good sprite: a filled square with an outline
good = [[1, 1, 1, 1, 1, 1, 1, 1],
        [1, 2, 2, 2, 2, 2, 2, 1],
        [1, 2, 2, 2, 2, 2, 2, 1],
        [1, 2, 2, 2, 2, 2, 2, 1],
        [1, 2, 2, 2, 2, 2, 2, 1],
        [1, 2, 2, 2, 2, 2, 2, 1],
        [1, 2, 2, 2, 2, 2, 2, 1],
        [1, 1, 1, 1, 1, 1, 1, 1]]
write("good.png", [good], 8, 8)

# too many colours: sixteen distinct ones (the bank holds fifteen)
PAL.extend([(i * 11, 255 - i * 11, 128) for i in range(20)])
many = [[1 + (x + y * 8) % 16 for x in range(8)] for y in range(8)]
write("many.png", [many], 8, 8)

# a jittery baseline: the second frame's feet stand two pixels higher
high = ([[0] * 8 for _ in range(3)] + [row[:] for row in good[2:6]] +
        [[0] * 8])
write("jitter.png", [good, high], 8, 8)

# orphan pixels: a block with a speck floating far away from it
orphan = [[0] * 8 for _ in range(8)]
for y in range(2, 6):
    for x in range(2, 6):
        orphan[y][x] = 2
orphan[0][0] = 2                 # the speck: no opaque neighbour
write("orphan.png", [orphan], 8, 8)
print("fixtures written")
PY

# --- 1) the good fixture passes -------------------------------------------
rc=0
"$ART" check "$OUT/good.png" --frame 8x8 > "$OUT/good.json" 2>"$OUT/good.err" || rc=$?
[ "$rc" -eq 0 ] || fail "the good fixture should pass: $(cat "$OUT/good.err")"
python3 - "$OUT/good.json" <<'PY' || exit 1
import json, sys
r = json.load(open(sys.argv[1]))
for key in ("file", "frames", "size", "checks", "per_frame", "failures", "ok"):
    if key not in r:
        sys.stderr.write("test-art-qa: FAILED — the report has no %r\n" % key)
        sys.exit(1)
if not r["ok"]:
    sys.stderr.write("test-art-qa: FAILED — the good fixture reports "
                     "failures\n")
    sys.exit(1)
PY
CHECKS=$((CHECKS + 1))
echo "  ok: the good fixture passes and the report carries its keys"

# --- 2) the bad fixtures fail with their own message ----------------------
expect_fail() {
    file=$1; want=$2
    rc=0
    "$ART" check "$OUT/$file" --frame 8x8 > "$OUT/$file.json" 2>"$OUT/$file.err" || rc=$?
    [ "$rc" -eq 1 ] || fail "$file should exit 1, got $rc"
    grep -q "$want" "$OUT/$file.err" || \
        fail "$file: the message is not '$want': $(cat "$OUT/$file.err")"
}

expect_fail many.png "colours"
expect_fail jitter.png "baseline"
expect_fail orphan.png "orphan"
CHECKS=$((CHECKS + 1))
echo "  ok: the three bad fixtures fail with their own message"

# --- 3) the .px samples pass ---------------------------------------------
for s in coin heart key potion arrow; do
    rc=0
    "$ART" check "$ROOT/programs/px/$s.png" --frame 8x8 > /dev/null 2>&1 || rc=$?
    [ "$rc" -eq 0 ] || fail "the sample $s should pass the checks (exit $rc)"
done
CHECKS=$((CHECKS + 1))
echo "  ok: the five .px samples pass the checks"

# --- 4) the humanoid rig's strips pass -----------------------------------
for s in idle walk run jump fall attack hurt dead; do
    rc=0
    "$ART" check "$ROOT/programs/rig/humanoid_$s.png" --frame 24x32 \
        > /dev/null 2>&1 || rc=$?
    [ "$rc" -eq 0 ] || fail "the rig strip $s should pass (exit $rc)"
done
CHECKS=$((CHECKS + 1))
echo "  ok: the humanoid rig's strips pass the checks"

# --- 5) the manifest's maps are checked ------------------------------------
# A manifest whose legend cannot draw one of the tileset's tiles: a warning
# (a spare tile is legitimate) — and the check reads the manifest at all.
python3 "$ROOT/tests/fixtures/manifest_fixture.py" "$OUT" || exit 1
rc=0
"$ART" check "$OUT/assets.json" --manifest > "$OUT/man.json" 2>"$OUT/man.err" || rc=$?
[ "$rc" -eq 0 ] || fail "the manifest with a spare tile should pass: $(cat "$OUT/man.err")"
grep -q "never uses 1" "$OUT/man.err" || \
    fail "the unused tile is not reported: $(cat "$OUT/man.err")"
CHECKS=$((CHECKS + 1))
echo "  ok: the manifest's maps are checked (a spare tile is a warning)"

# --- 6) the world preview --------------------------------------------------
rc=0
"$ART" preview "$OUT/assets.json" --out "$OUT/world.png" --maps > /dev/null 2>&1 || rc=$?
[ "$rc" -eq 0 ] || fail "the world preview failed"
[ -s "$OUT/world.png" ] || fail "the world preview is empty"
rc=0
"$ART" preview "$ROOT/round7/assets.json" --out "$OUT/world7.png" --maps > /dev/null 2>&1 || rc=$?
[ "$rc" -eq 0 ] || fail "the world preview of the round-7 manifest failed"
CHECKS=$((CHECKS + 1))
echo "  ok: the pipeline can compose the world"

echo "test-art-qa: ok ($CHECKS checks, card 7.6)"
