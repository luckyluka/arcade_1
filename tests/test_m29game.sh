#!/bin/sh
# ARCADE-1 — m29game test (golden game 6): a full match, a restart, and the
# HUD that the M29 fresh-agent test got wrong.
#
# m29game is the game that test's agent wrote out of nothing but the shipped
# CLI + docs (record copy: docs/m29/game.c). It exercises all four 4-bit
# paths — TILEMAP4 bricks, SPRITE4 paddle/ball, a 6-bank palette and the
# 3x5 text font on plain rects.
#
#   1. determinism      — two identical runs, identical output
#   2. a full match     — lives 3 -> game over, score pinned
#   3. START restarts   — game over ~660 comes back as a fresh match at 700
#   4. the HUD          — the LIVES label must not sit under the life boxes
#
# (4) is the M29 bug: draw_text advances 8 px per glyph at scale 2, so
# "LIVES" at the old SCREEN_W-74 reached x=443 while the life boxes start at
# x=434 — and because the boxes are painted *after* the label, they ate its
# last two glyphs. The label now ends at x=427.)
#
# usage: test_m29game.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_m29game.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}
GOLD=${3:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-m29game: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

GAME=$ROM/m29game.arc1

[ -x "$CLI" ]   || fail "CLI not built at $CLI"
[ -f "$GAME" ]  || fail "missing $GAME"

# Named-variable reads need an `nm` dump: the committed one (the golden ROM
# dir has it), else make one from the freshly built ELF.
SYMS=$ROM/m29game.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/m29game.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/m29game.elf" > "$OUT/m29game.syms"
    SYMS="$OUT/m29game.syms"
fi

# The match script: dodge until the three lives are gone, press START once
# (frame 650), then idle through a second match.
SCRIPT=$(dirname "$0")/../docs/m29/scripts/restart.txt
[ -f "$SCRIPT" ] || fail "missing $SCRIPT"

if [ -f "$SYMS" ]; then
    # --- 1) determinism ------------------------------------------------------
    "$CLI" run "$GAME" --frames 1800 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,lives,state --var-every 100 --quiet > "$OUT/r1.txt" 2>/dev/null
    "$CLI" run "$GAME" --frames 1800 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,lives,state --var-every 100 --quiet > "$OUT/r2.txt" 2>/dev/null
    if ! cmp -s "$OUT/r1.txt" "$OUT/r2.txt"; then
        diff "$OUT/r1.txt" "$OUT/r2.txt" | head -5 >&2
        fail "two identical match runs produced different output"
    fi
    note "determinism ok — two 1800-frame matches identical"

    # --- 2+3) the match: verbose, so its frame lines are the golden's --------
    # The output goes to a file for the assertions and to stdout for the
    # golden grep (test-golden takes the `frame ` lines from here).
    "$CLI" run "$GAME" --frames 1800 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,lives,state --var-every 100 > "$OUT/main.txt" 2>/dev/null
    cat "$OUT/main.txt"

    grep '^vars' "$OUT/main.txt" > "$OUT/trace.txt"
    grep -q '^vars *600 .*lives=0 state=1$' "$OUT/trace.txt" ||
        fail "match: expected game over at frame 600, got: $(grep '^vars *600' "$OUT/trace.txt")"
    grep -q '^vars *700 .*score=0 lives=3 state=0$' "$OUT/trace.txt" ||
        fail "restart: START at 650 did not reset the game (frame 700 trace)"
    grep -q '^vars *1300 .*lives=0 state=1$' "$OUT/trace.txt" ||
        fail "second match: expected game over at frame 1300"

    grep '^var ' "$OUT/main.txt" > "$OUT/final.txt"
    grep -q '^var score = 60$' "$OUT/final.txt" ||
        fail "expected final score 60, got: $(cat "$OUT/final.txt")"
    grep -q '^var lives = 0$' "$OUT/final.txt" ||
        fail "expected final lives 0, got: $(cat "$OUT/final.txt")"
    grep -q '^var state = 1$' "$OUT/final.txt" ||
        fail "expected the second game over, got: $(cat "$OUT/final.txt")"
    note "match ok — game over, START restarts, second match ends 60/0/1"
else
    # Without symbols the golden still needs frame lines: run verbose anyway.
    "$CLI" run "$GAME" --frames 1800 --input "$SCRIPT" > "$OUT/main.txt" 2>/dev/null
    cat "$OUT/main.txt"
    note "named variables skipped — no m29game.syms and no riscv nm to make one"
fi

# --- 4) HUD: LIVES must not sit under the life boxes ------------------------
# Frame 5: three lives, state playing, HUD fully drawn.
"$CLI" run "$GAME" --frames 5 --capture "$OUT/hud.ppm" --quiet > /dev/null 2>&1
[ "$(head -c 2 "$OUT/hud.ppm")" = "P6" ] || fail "HUD capture is not a P6 PPM"

# Scan the payload for the two HUD colours. They are RGB565 components in
# the game (label RGB(24,26,30), boxes RGB(12,24,31)) converted to RGB888
# exactly the way the CLI's PPM writer converts them (r*255/31, ...).
READ=$(tail -c $((480 * 270 * 3)) "$OUT/hud.ppm" | od -An -v -tu1 | awk -v \
    lr=$((24 * 255 / 31)) -v lg=$((26 * 255 / 63)) -v lb=$((30 * 255 / 31)) \
    -v sr=$((12 * 255 / 31)) -v sg=$((24 * 255 / 63)) -v sb=$((31 * 255 / 31)) '
    { for (i = 1; i <= NF; i++) { c[n % 3] = $i; n++
        if (n % 3 == 0) {
            p = int(n / 3) - 1; x = p % 480; y = int(p / 480)
            if (y >= 6 && y <= 15) {
                if (c[0] == lr && c[1] == lg && c[2] == lb) {
                    if (x >= 390 && x <= 427) box++
                    if (x >= 430 && x <= 459) bad++
                }
                if (c[0] == sr && c[1] == sg && c[2] == sb &&
                    x >= 434 && x <= 459 && y >= 8 && y <= 13) sq++
            }
        } } }
    END { printf "%d %d %d\n", box + 0, bad + 0, sq + 0 }')
set -- $READ
BOX=$1; BAD=$2; SQ=$3

# The full label is 196 label-coloured pixels; with the boxes painted over
# it (the old SCREEN_W-74 position) only 108 survive, and 76 label pixels
# leak right of the box into the life-box area.
[ "$BOX" -ge 150 ] ||
    fail "LIVES label is drawn incomplete ($BOX px in its slot, want >= 150)"
[ "$BAD" -eq 0 ] ||
    fail "LIVES label overlaps the life boxes ($BAD label px at x>=430, want 0)"
[ "$SQ" -eq 108 ] ||
    fail "life boxes wrong: $SQ px (3 boxes x 6x6 = 108)"
note "hud ok — LIVES fully drawn (in slot $BOX px), no overlap, 3 boxes"

echo "test-m29game: m29game ok (determinism, match, restart, HUD)"
