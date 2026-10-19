#!/bin/sh
# ARCADE-1 — asset_demo test: the typed-accessor experiment's mechanics.
#
# asset_demo (programs/experimental/asset_demo.c) draws a 16x16 four-frame
# hero sheet compiled by tools/arc1_assetc.py; every draw travels through
# the emitted per-frame sprite4_ref array (programs/asset.h). Findings:
# docs/experiments/arcade1-assetc.md (Wave 2). This test pins what the
# experiment claimed:
#
#   1. metadata  — assets.json's origin [8, 16] and collision [1, 1, 14, 14]
#                  reach the target through the refs (read back as globals)
#   2. placement — asset_sprite4() lands frame 0's red block at exactly
#                  (42..45, 42..45); frame 1's green block at (410..413, …);
#                  the feet origin puts frame 2's bottom bar on y = 119,
#                  one row above the FEET_Y = 120 line
#   3. flip      — flags bit0 mirrors the same ref within its 16 px: the
#                  red block reappears at (110..113, 42..45), cols 13..10
#   4. collision — the debug box behind frame 3 fills 14x14 minus the 16
#                  sprite pixels drawn on top (180 blue, 16 white)
#
# usage: test_assetdemo.sh <arcade1-cli> <rom-dir>
#
# NOTE: asset_demo is an EXPERIMENT (built by `make experimental`, not by
# `make check`); this test is likewise run by hand: `make test-assetdemo`.

set -eu

CLI=${1:?usage: test_assetdemo.sh <arcade1-cli> <rom-dir>}
ROM=${2:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-assetdemo: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

GAME=$ROM/asset_demo.arc1

[ -x "$CLI" ]  || fail "CLI not built at $CLI"
[ -f "$GAME" ] || fail "missing $GAME (make experimental)"

SYMS=$ROM/asset_demo.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/asset_demo.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/asset_demo.elf" > "$OUT/asset_demo.syms"
    SYMS="$OUT/asset_demo.syms"
fi
[ -f "$SYMS" ] || fail "no asset_demo.syms and no riscv nm to make one"

CHECKS=0

# scan_color <ppm> <r> <g> <b> [x0 x1 y0 y1] -> "count minx maxx miny maxy"
# The region defaults to the whole 480x270 screen.
scan_color() {
    tail -c $((480 * 270 * 3)) "$1" | od -An -v -tu1 | awk \
        -v pr="$2" -v pg="$3" -v pb="$4" \
        -v x0="${5:-0}" -v x1="${6:-479}" -v y0="${7:-0}" -v y1="${8:-269}" '
        { for (i = 1; i <= NF; i++) { c[n % 3] = $i; n++
            if (n % 3 == 0) {
                p = int(n / 3) - 1; x = p % 480; y = int(p / 480)
                if (c[0] == pr && c[1] == pg && c[2] == pb &&
                    x >= x0 && x <= x1 && y >= y0 && y <= y1) {
                    cnt++
                    if (mn == "" || x < mn) mn = x
                    if (mx == "" || x > mx) mx = x
                    if (my == "" || y < my) my = y
                    if (My == "" || y > My) My = y
                } } } }
        END { printf "%d %d %d %d %d\n", cnt + 0, mn + 0, mx + 0, \
             my + 0, My + 0 }'
}

# expect_color <desc> <r> <g> <b> <n> <x0> <x1> <y0> <y1> — exactly n pixels
# of the colour inside the box, whose bbox must fill it (so a shifted or
# duplicated draw cannot pass by counting alone).
expect_color() {
    desc=$1; er=$2; eg=$3; eb=$4; en=$5
    ex0=$6; ex1=$7; ey0=$8; ey1=$9
    set -- $(scan_color "$OUT/scene.ppm" "$er" "$eg" "$eb" \
                        "$ex0" "$ex1" "$ey0" "$ey1")
    [ "$1" -eq "$en" ] || \
        fail "$desc: want $en px, got $1 (bbox $2..$3 x, $4..$5 y)"
    [ "$2" -eq "$ex0" ] && [ "$3" -eq "$ex1" ] && [ "$4" -eq "$ey0" ] && \
        [ "$5" -eq "$ey1" ] || \
        fail "$desc: bbox ($2..$3, $4..$5) != wanted ($ex0..$ex1, $ey0..$ey1)"
    CHECKS=$((CHECKS + 1))
    echo "  ok: $desc — $1 px at ($2..$3, $4..$5)"
}

# --- 1) run: 3 frames of a static scene, capture + metadata globals ---------
"$CLI" run "$GAME" --frames 3 --syms "$SYMS" \
    --vars origin_x,origin_y,coll_w,coll_h --capture "$OUT/scene.ppm" \
    > "$OUT/run.txt" 2>/dev/null || fail "run failed"

for v in "origin_x 8" "origin_y 16" "coll_w 14" "coll_h 14"; do
    grep -q "^var ${v%% *} = ${v##* }$" "$OUT/run.txt" || \
        fail "metadata: want 'var $v' (got $(grep '^var' "$OUT/run.txt" | tr '\n' ' '))"
    CHECKS=$((CHECKS + 1))
done
note "metadata ok — origin (8, 16), collision (14x14) read back from the refs"

NFB=$(grep '^frame ' "$OUT/run.txt" | awk '{print $4}' | sort -u | wc -l | tr -d ' ')
[ "$NFB" = "1" ] || fail "scene is not stable ($NFB distinct fb hashes in 3 frames)"
CHECKS=$((CHECKS + 1))

# --- 2) placement: frame 0 at (40, 40), frame 1 at (400, 40) ----------------
# frame 0's red block is sheet cols 2..5, rows 2..5 -> screen (42..45, 42..45)
expect_color "frame 0 red block" 255 0 0 16 42 45 42 45
# frame 1's green block is sheet cols 10..13, rows 2..5 -> (410..413, 42..45)
expect_color "frame 1 green block" 0 255 0 16 410 413 42 45

# --- 3) flip-X: the same ref mirrored within its 16 px -----------------------
# flipped cols 13..10 land at screen x 110..113 (100 + 13 .. 100 + 10)
set -- $(scan_color "$OUT/scene.ppm" 255 0 0 80 139 0 269)
[ "$1" -eq 16 ] || fail "flipped red block: want 16 px right of x=80, got $1"
[ "$2" -eq 110 ] && [ "$3" -eq 113 ] || \
    fail "flipped red block: x $2..$3, want 110..113 (mirror of 42..45)"
CHECKS=$((CHECKS + 1))
echo "  ok: flip-X — the frame-0 ref mirrors to x 110..113 (16 px)"

# --- 4) origin placement: frame 2's bottom bar sits on the feet line --------
# the manifest's origin [8, 16] draws at (200-8, 120-16): the bar (rows
# 14..15) lands at y 118..119, its bottom row exactly FEET_Y-1.
expect_color "feet bar above the line" 0 0 255 12 197 202 118 119

# --- 5) collision box behind frame 3, sprite on top -------------------------
# the box [1, 1, 14, 14] fills (301..314, 61..74) = 196 px; the white 4x4
# block drawn on top takes back 16 -> 180 blue remain in the box.
set -- $(scan_color "$OUT/scene.ppm" 0 0 255 301 314 61 74)
[ "$1" -eq 180 ] || fail "collision box: want 180 blue px (14x14 - 16), got $1"
CHECKS=$((CHECKS + 1))
echo "  ok: collision box — 14x14 fill minus the 16 sprite px = 180 blue"
expect_color "sprite on top of its box" 255 255 255 16 302 305 70 73

# no stray pixels of the scene's colours anywhere else
set -- $(scan_color "$OUT/scene.ppm" 0 0 255)
[ "$1" -eq 192 ] || fail "stray blue: want 192 px total (12 bar + 180 box), got $1"
set -- $(scan_color "$OUT/scene.ppm" 255 0 0)
[ "$1" -eq 32 ] || fail "stray red: want 32 px total, got $1"
set -- $(scan_color "$OUT/scene.ppm" 0 255 0)
[ "$1" -eq 16 ] || fail "stray green: want 16 px total, got $1"
set -- $(scan_color "$OUT/scene.ppm" 255 255 255)
[ "$1" -eq 16 ] || fail "stray white: want 16 px total, got $1"
CHECKS=$((CHECKS + 4))
echo "  ok: no stray pixels — totals 192 blue, 32 red, 16 green, 16 white"

echo "test-assetdemo: ok ($CHECKS checks, Wave 2 card: metadata + typed accessors)"
