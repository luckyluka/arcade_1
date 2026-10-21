#!/bin/sh
# ROADMAP_V4 card 7.4: the rig animator.
#
# Acceptance (pinned):
#   1. two builds are byte-identical (determinism);
#   2. within one state every frame has the same size, and the lowest
#      opaque pixel is on the same row (the feet stay on the ground);
#   3. across a walk cycle the opaque-pixel count of any frame stays
#      within 25 % of the cycle's mean (the animation is consistent);
#   4. every frame uses at most 15 colours plus transparent;
#   5. the contact sheet is written, and the review that the card asks
#      for exists (art/reports/humanoid.md).
#
# Host-side only: it builds the rig with the tool and reads the frames.
#
# Usage: test_art_rig.sh [repo-root]
set -eu

ROOT=${1:-$(cd "$(dirname "$0")/.." && pwd)}
case "$ROOT" in /*) ;; *) ROOT="$(pwd)/$ROOT" ;; esac

ART=$ROOT/tools/arc1-art
PRESET=$ROOT/art/rigs/humanoid.json
REPORT=$ROOT/art/reports/humanoid.md

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-art-rig: FAILED — $*" >&2; exit 1; }
CHECKS=0

[ -x "$ART" ] || fail "no arc1-art at $ART"
[ -f "$PRESET" ] || fail "missing $PRESET"

# --- 1) two builds, byte for byte -----------------------------------------
"$ART" rig "$PRESET" --out-dir "$OUT/a" > /dev/null || fail "the build failed"
"$ART" rig "$PRESET" --out-dir "$OUT/b" > /dev/null || fail "the build failed"
diff -r "$OUT/a" "$OUT/b" > /dev/null || fail "two builds differ"
CHECKS=$((CHECKS + 1))
echo "  ok: two builds are byte-identical"

# --- 2) to 4) the frames themselves ---------------------------------------
python3 - "$PRESET" "$OUT/a" <<'PY' || exit 1
import json, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(
    os.path.join(sys.argv[1], "..", ".."))), "tools"))
from arc1_art import rig as rigmod

preset = json.load(open(sys.argv[1]))
out_dir = sys.argv[2]
built, states = rigmod.build_rig(preset)

fails = []

# 2) one size per state, and the feet on one row
for name, frames in sorted(states.items()):
    sizes = set((f.w, f.h) for f in frames)
    if len(sizes) != 1:
        fails.append("%s: the frames have different sizes %s" % (name, sizes))
    lows = set(rigmod.lowest_opaque_row(f) for f in frames)
    if len(lows) != 1:
        fails.append("%s: the lowest opaque row is not the same in every "
                     "frame (%s)" % (name, sorted(lows)))

# 3) the walk cycle stays within 25 % of its mean
for name, frames in sorted(states.items()):
    counts = [sum(1 for row in f.p for v in row if v) for f in frames]
    mean = sum(counts) / float(len(counts))
    for i, c in enumerate(counts):
        if abs(c - mean) / mean > 0.25:
            fails.append("%s: frame %d has %d opaque pixels, the mean is "
                         "%.1f — more than 25 %% off" % (name, i + 1, c, mean))

# 4) at most 15 colours plus transparent per frame
for name, frames in sorted(states.items()):
    for i, f in enumerate(frames):
        colours = set(v for row in f.p for v in row if v)
        if len(colours) > 15:
            fails.append("%s: frame %d uses %d colours (max 15)"
                         % (name, i + 1, len(colours)))

# the fragment carries the collision box of the idle frame
fragment = json.load(open(os.path.join(out_dir, "assets.json")))
idle = [s for s in fragment["sprites"] if s["name"].endswith("_idle")]
if not idle:
    fails.append("the fragment has no idle sprite")
elif len(idle[0].get("collision", [])) != 4:
    fails.append("the idle sprite has no collision box")

if fails:
    for f in fails:
        sys.stderr.write("test-art-rig: FAILED — %s\n" % f)
    sys.exit(1)
PY
CHECKS=$((CHECKS + 1))
echo "  ok: one size and one baseline per state, the walk within 25 %, <=15 colours"

# --- 5) the sheet and the review -----------------------------------------
[ -f "$OUT/a/humanoid_sheet.png" ] || fail "no contact sheet was written"
[ -s "$OUT/a/humanoid_sheet.png" ] || fail "the contact sheet is empty"
[ -f "$REPORT" ] || fail "missing the review $REPORT"
for section in "Verdict" "Observations" "What changed" "Re-check"; do
    grep -qi "$section" "$REPORT" || \
        fail "the review has no '$section' section"
done
CHECKS=$((CHECKS + 1))
echo "  ok: the contact sheet is written and the review exists"

echo "test-art-rig: ok ($CHECKS checks, card 7.4)"
