#!/bin/sh
# ROADMAP_V4 (the library): the device's horizontal flip, proved.
#
# The library's sprites are drawn facing one way, and the packs claim the
# device mirrors them for free (sprite4's flags bit0 = flip X). This test
# proves it: a guest draws one asymmetric shape plain and flipped, and the
# painted pixels are counted on each side of each sprite.
#
# Host-side only (the guest runs in the emulator).
#
# Usage: test_art_flip.sh <arcade1-cli> <guest-build-dir>
set -eu

CLI=${1:?usage: test_art_flip.sh <arcade1-cli> <guest-build-dir>}
GUEST=${2:?usage: test_art_flip.sh <arcade1-cli> <guest-build-dir>}
case "$CLI" in /*) ;; *) CLI="$(pwd)/$CLI" ;; esac

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-art-flip: FAILED — $*" >&2; exit 1; }

[ -x "$CLI" ] || fail "no CLI at $CLI"
[ -f "$GUEST/flip_probe.bin" ] || fail "missing $GUEST/flip_probe.bin"

"$CLI" run "$GUEST/flip_probe.bin" --frames 3 --capture "$OUT/scene.ppm" \
    --quiet > /dev/null 2>&1 || fail "the probe guest did not run"

# count the painted pixels in a box (the shape is 0xF800 = red)
count_box() {
    tail -c $((480 * 270 * 3)) "$OUT/scene.ppm" | od -An -v -tu1 | awk \
        -v x0="$1" -v x1="$2" -v y0="$3" -v y1="$4" '
        { for (i = 1; i <= NF; i++) { c[n % 3] = $i; n++
            if (n % 3 == 0) {
                p = int(n / 3) - 1; x = p % 480; y = int(p / 480)
                if (x >= x0 && x <= x1 && y >= y0 && y <= y1 &&
                    c[0] > 200 && c[1] < 120 && c[2] < 120) cnt++ } } }
        END { print cnt + 0 }'
}

# the plain sprite: its pixels are on the LEFT of its box
left=$(count_box 20 23 20 27)
right=$(count_box 24 27 20 27)
# the flipped sprite: its pixels are on the RIGHT of its box
fleft=$(count_box 100 103 20 27)
fright=$(count_box 104 107 20 27)

[ "$left" -gt 0 ] || fail "the plain sprite painted nothing on the left"
[ "$right" -eq 0 ] || fail "the plain sprite painted $right pixels on the right"
[ "$fright" -gt 0 ] || fail "the flipped sprite painted nothing on the right"
[ "$fleft" -eq 0 ] || fail "the flipped sprite painted $fleft pixels on the left"

echo "test-art-flip: ok (the plain shape is $left px on the left, the flipped $fright px on the right)"
