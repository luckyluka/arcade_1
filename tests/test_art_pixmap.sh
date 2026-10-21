#!/bin/sh
# ROADMAP_V4 card 7.3: the .px text pixmaps.
#
# Acceptance (pinned):
#   1. the round trip: a .px file -> the strip PNG -> arc1-assetc -> the
#      generated header -> a guest that draws it, and the painted pixels
#      are exactly the pixmap's non-transparent pixels;
#   2. every teaching error is pinned: the unknown character, the short
#      row, the wrong frame size, and the missing header field;
#   3. the tool is deterministic (two runs, identical bytes);
#   4. the five sample sprites are committed, each with a provenance.
#
# Host-side only, except the probe guest, which runs in the emulator.
#
# Usage: test_art_pixmap.sh <arcade1-cli> <guest-build-dir> [repo-root]
set -eu

CLI=${1:?usage: test_art_pixmap.sh <arcade1-cli> <guest-build-dir> [repo-root]}
GUEST=${2:?usage: test_art_pixmap.sh <arcade1-cli> <guest-build-dir> [repo-root]}
ROOT=${3:-$(cd "$(dirname "$0")/.." && pwd)}
case "$ROOT" in /*) ;; *) ROOT="$(pwd)/$ROOT" ;; esac

ART=$ROOT/tools/arc1-art
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-art-pixmap: FAILED — $*" >&2; exit 1; }
CHECKS=0

[ -x "$ART" ] || fail "no arc1-art at $ART"
[ -f "$GUEST/px_probe.bin" ] || fail "missing $GUEST/px_probe.bin (make it)"

# --- 1) the round trip -----------------------------------------------------
# The samples are the sources; the build output in programs/px/ is the
# truth. Regenerate into a temp dir and compare with the committed one.
"$ART" px2assets "$ROOT"/art/library/icons/pixmaps/*.px --out-dir "$OUT/px" > /dev/null || \
    fail "px2assets failed"
for f in assets.json coin.png heart.png key.png potion.png arrow.png; do
    cmp -s "$OUT/px/$f" "$ROOT/programs/px/$f" || \
        fail "the regenerated $f differs from the committed one"
done
CHECKS=$((CHECKS + 1))
echo "  ok: the pixmaps regenerate the committed strips and the manifest"

python3 "$ROOT/tools/arc1_assetc.py" --assets "$OUT/px/assets.json" \
    --out-dir "$OUT/px" > /dev/null || fail "the manifest did not compile"
cmp -s "$OUT/px/assets.h" "$ROOT/programs/px/assets.h" || \
    fail "the regenerated header differs from the committed one"
CHECKS=$((CHECKS + 1))
echo "  ok: the manifest compiles to the committed header"

# The guest draws the heart; the painted pixels must be the pixmap's
# non-transparent pixels (counted from the .px file itself).
"$CLI" run "$GUEST/px_probe.bin" --frames 3 --capture "$OUT/scene.ppm" \
    --quiet > /dev/null 2>&1 || fail "the probe guest did not run"
want=$(grep -v '^#' "$ROOT/art/library/icons/pixmaps/heart.px" | tr -d '\n' | tr -cd '.' | wc -c)
total=$(grep -v '^#' "$ROOT/art/library/icons/pixmaps/heart.px" | tr -d '\n' | tr -cd '.' | wc -c)
opaque=$(grep -v '^#' "$ROOT/art/library/icons/pixmaps/heart.px" | tr -d '\n' | tr -cd '.' | wc -c)
chars=$(grep -v '^#' "$ROOT/art/library/icons/pixmaps/heart.px" | tr -d '\n' | tr -cd '0-9a-f' | wc -c)
want=$chars
# count the painted pixels in the 8x8 box at (20,20)
painted=$(tail -c $((480 * 270 * 3)) "$OUT/scene.ppm" | od -An -v -tu1 | awk \
    -v x0=20 -v y0=20 -v w=8 '
    { for (i = 1; i <= NF; i++) { c[n % 3] = $i; n++
        if (n % 3 == 0) {
            p = int(n / 3) - 1; x = p % 480; y = int(p / 480)
            if (x >= x0 && x < x0 + w && y >= y0 && y < y0 + w &&
                (c[0] || c[1] || c[2])) cnt++ } } }
    END { print cnt + 0 }')
[ "$painted" = "$want" ] || \
    fail "the guest painted $painted pixels, the pixmap has $want"
CHECKS=$((CHECKS + 1))
echo "  ok: the round trip paints $painted pixels — the pixmap's own count"

# --- 2) the teaching errors, pinned ---------------------------------------
# unknown character
printf '# name: bad   size: 2x2   palette: warm16   bank: 3\ng1\n' \
    > "$OUT/bad_char.px"
rc=0
out=$("$ART" info "$OUT/bad_char.px" 2>&1) || rc=$?
[ "$rc" -eq 1 ] || fail "an unknown character should exit 1, got $rc"
case "$out" in
    *"unknown character 'g' (use . or 0-9a-f)"*) ;;
    *) fail "the unknown-character message is wrong: $out" ;;
esac
# short row
printf '# name: bad   size: 8x8   palette: warm16   bank: 3\n1234567\n' \
    > "$OUT/bad_row.px"
rc=0
out=$("$ART" info "$OUT/bad_row.px" 2>&1) || rc=$?
[ "$rc" -eq 1 ] || fail "a short row should exit 1, got $rc"
case "$out" in
    *"row has 7 pixels, expected 8"*) ;;
    *) fail "the short-row message is wrong: $out" ;;
esac
# wrong frame size: frame 2 has 7 rows where 8 are declared
{
    printf '# name: bad   size: 8x8   palette: warm16   bank: 3\n'
    i=0; while [ $i -lt 8 ]; do printf '12345678\n'; i=$((i+1)); done
    printf -- '--\n'
    i=0; while [ $i -lt 7 ]; do printf '12345678\n'; i=$((i+1)); done
} > "$OUT/bad_frame.px"
rc=0
out=$("$ART" info "$OUT/bad_frame.px" 2>&1) || rc=$?
[ "$rc" -eq 1 ] || fail "a wrong frame size should exit 1, got $rc"
case "$out" in
    *"frame 2 is 8x7, expected 8x8"*) ;;
    *) fail "the frame-size message is wrong: $out" ;;
esac
# a missing header field
printf '# name: bad   size: 8x8\n12345678\n' > "$OUT/bad_header.px"
rc=0
out=$("$ART" info "$OUT/bad_header.px" 2>&1) || rc=$?
[ "$rc" -eq 1 ] || fail "a missing header field should exit 1, got $rc"
case "$out" in
    *"no 'palette:' field"*) ;;
    *) fail "the missing-field message is wrong: $out" ;;
esac
# a usage error exits 2
rc=0; "$ART" >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "no arguments should exit 2, got $rc"
CHECKS=$((CHECKS + 1))
echo "  ok: every teaching error is pinned (character, row, frame, field)"

# --- 3) determinism --------------------------------------------------------
"$ART" px2assets "$ROOT"/art/library/icons/pixmaps/*.px --out-dir "$OUT/px2" > /dev/null
for f in assets.json coin.png heart.png key.png potion.png arrow.png; do
    cmp -s "$OUT/px/$f" "$OUT/px2/$f" || fail "two runs differ: $f"
done
CHECKS=$((CHECKS + 1))
echo "  ok: two runs of arc1-art are identical"

# --- 4) the five samples, with provenance ---------------------------------
for s in coin heart key potion arrow; do
    f="$ROOT/art/library/icons/pixmaps/$s.px"
    [ -f "$f" ] || fail "missing the sample $s"
    grep -q '^# provenance:' "$f" || fail "the sample $s has no provenance"
    grep -q '"tool": "agent-authored"' "$f" || \
        fail "the sample $s is not marked agent-authored"
done
CHECKS=$((CHECKS + 1))
echo "  ok: the five samples are committed, each with a provenance"

echo "test-art-pixmap: ok ($CHECKS checks, card 7.3)"
