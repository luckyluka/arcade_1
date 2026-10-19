#!/bin/sh
# ARCADE-1 — map_demo test: the map importer's draw-proof.
#
# map_demo (programs/experimental/map_demo.c) draws three levels compiled
# by tools/arc1_assetc.py from ASCII grids over one four-tile tileset: a
# 4x3 map and a 5x1 map — the fixtures that REPLACED the retracted
# tilemap min-geometry validator (ROADMAP_V3 4.3, docs/experiments/
# tilemap-minsize-retraction.md) — and a 10x3 field whose cells all point
# at ONE deduped tile (the M29 brick lesson; the field's corner char names
# a byte-identical fourth tile that the importer collapses). Findings:
# docs/experiments/arc1-assetc.md (Wave 4). This test pins, pixel-exact
# (pixel-count style of tests/test_barrier.c):
#
#   1. small43 draws — 4x3 cells at (16, 16): 1280 brick + 256 stripe px,
#      the empty row stays sky (both tiny geometries paint)
#   2. small51 draws — 5x1 cells at (16, 80): 256 brick + 256 stripe, and
#      the 'q' cell's flipX mirrors the marker column to tile-local cols
#      15..14 (screen x 62..63 instead of 48..49)
#   3. field draws  — 10x3 cells at (16, 112): 28 tiles' worth of brick
#      (7168 px — the 4 corner cells share ONE blob entry with the fill),
#      stripe at row 1 col 6, marker at row 1 col 8
#   4. totals — no stray pixels anywhere: 8704 brick, 768 stripe, 64
#      marker, and the scene is stable across frames
#
# usage: test_mapdemo.sh <arcade1-cli> <rom-dir>
#
# NOTE: map_demo lives in programs/experimental/ but — unlike walkdemo and
# asset_demo — its test IS part of `make check`: these fixtures are the
# retraction's replacement evidence, and a draw-proof that only runs by
# hand pins nothing.

set -eu

CLI=${1:?usage: test_mapdemo.sh <arcade1-cli> <rom-dir>}
ROM=${2:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-mapdemo: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

GAME=$ROM/map_demo.arc1

[ -x "$CLI" ]  || fail "CLI not built at $CLI"
[ -f "$GAME" ] || fail "missing $GAME (make test-mapdemo)"

SYMS=$ROM/map_demo.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/map_demo.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/map_demo.elf" > "$OUT/map_demo.syms"
    SYMS="$OUT/map_demo.syms"
fi
[ -f "$SYMS" ] || fail "no map_demo.syms and no riscv nm to make one"

CHECKS=0

# The device expands RGB565 as floor(v * 255 / max): SKY (4,12,28),
# brick (24,12,6), marker (6,60,10), stripe (8,24,30).
SKY_R=32;  SKY_G=48;  SKY_B=230
BRK_R=197; BRK_G=48;  BRK_B=49
MRK_R=49;  MRK_G=242; MRK_B=82
STR_R=65;  STR_G=97;  STR_B=246

# scan_color <ppm> <r> <g> <b> [x0 x1 y0 y1] -> "count minx maxx miny maxy"
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

# expect_sky <desc> <n> <x0> <x1> <y0> <y1> — exactly n background pixels
# inside the box (empty cells must stay background, not tile).
expect_sky() {
    sd=$1; sn=$2
    set -- $(scan_color "$OUT/scene.ppm" "$SKY_R" "$SKY_G" "$SKY_B" \
                        "$3" "$4" "$5" "$6")
    [ "$1" -eq "$sn" ] || fail "$sd: want $sn sky px, got $1"
    CHECKS=$((CHECKS + 1))
    echo "  ok: $sd — $sn sky px (empty cells draw nothing)"
}

# --- 1) run: a static scene, 3 frames ---------------------------------------
"$CLI" run "$GAME" --frames 3 --syms "$SYMS" --capture "$OUT/scene.ppm" \
    > "$OUT/run.txt" 2>/dev/null || fail "run failed"

NFB=$(grep '^frame ' "$OUT/run.txt" | awk '{print $4}' | sort -u | wc -l | tr -d ' ')
[ "$NFB" = "1" ] || fail "scene is not stable ($NFB distinct fb hashes in 3 frames)"
CHECKS=$((CHECKS + 1))
note "stable ok — one fb hash in 3 frames"

# --- 2) small43 (4x3) draws: the retracted 'minimum geometry' fixture ------
# row0: o(256) . s(256) .   row1: #### = 4 dedup tiles (1024)   row2: empty
expect_color "small43 brick (o + the 4 deduped #)" $BRK_R $BRK_G $BRK_B \
    1280 16 79 16 47
expect_color "small43 stripe s" $STR_R $STR_G $STR_B 256 48 63 16 31
expect_sky  "small43 empty row" 1024 16 79 48 63

# --- 3) small51 (5x1) draws, and q's flipX mirrors the marker column -------
expect_color "small51 brick o" $BRK_R $BRK_G $BRK_B 256 16 31 80 95
expect_color "small51 stripe s" $STR_R $STR_G $STR_B 256 80 95 80 95
# tile 1's marker column sits at local cols 0..1; flipX mirrors it to
# 15..14 -> screen x 62..63 (unflipped would be 48..49).
expect_color "small51 flipped marker q (cols 15..14)" \
    $MRK_R $MRK_G $MRK_B 32 62 63 80 95

# --- 4) field (10x3) draws from ONE deduped tile ---------------------------
# 26 'b' + 4 corner 'c' cells (byte-identical tile 3, collapsed at build)
# = 28 tiles of brick; 'o' and 's' fill row 1.
expect_color "field brick (28 tiles incl. the corners)" $BRK_R $BRK_G $BRK_B \
    7168 16 175 112 159
expect_color "field stripe s" $STR_R $STR_G $STR_B 256 112 127 128 143
expect_color "field marker o" $MRK_R $MRK_G $MRK_B 32 144 145 128 143

# --- 5) totals: no stray pixels of any scene colour ------------------------
set -- $(scan_color "$OUT/scene.ppm" $BRK_R $BRK_G $BRK_B)
[ "$1" -eq 8704 ] || fail "stray brick: want 8704 px total, got $1"
set -- $(scan_color "$OUT/scene.ppm" $STR_R $STR_G $STR_B)
[ "$1" -eq 768 ] || fail "stray stripe: want 768 px total, got $1"
set -- $(scan_color "$OUT/scene.ppm" $MRK_R $MRK_G $MRK_B)
[ "$1" -eq 64 ] || fail "stray marker: want 64 px total, got $1"
set -- $(scan_color "$OUT/scene.ppm" $SKY_R $SKY_G $SKY_B)
[ "$1" -eq 120064 ] || fail "stray sky: want 120064 px total, got $1"
CHECKS=$((CHECKS + 4))
note "totals ok — 8704 brick, 768 stripe, 64 marker, 120064 sky"

echo "test-mapdemo: ok ($CHECKS checks, Wave 4 card: map import + minsize retraction)"
