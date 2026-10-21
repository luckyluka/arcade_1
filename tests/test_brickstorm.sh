#!/bin/sh
# ARCADE-1 — brickstorm test (the breakout remake): two scripted sessions.
#
#   1. determinism      — two identical sessions, identical output
#   2. the story        — the title, level 1 played to the game over, the
#                         level picker, the boss and the win
#   3. the progression  — a stage cleared: the level picker to stage 8,
#                         which the paddle sweep clears, and the game
#                         moves on to stage 9
#   4. record -> replay — the recorded session replays bit-identically
#
# The verbose run prints its per-frame hash lines to stdout, which is what
# the golden regression (tests/golden/brickstorm.txt) records.
#
# usage: test_brickstorm.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_brickstorm.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-brickstorm: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

GAME=$ROM/brickstorm.arc1

[ -x "$CLI" ]  || fail "CLI not built at $CLI"
[ -f "$GAME" ] || fail "missing $GAME"

SCRIPT=$(dirname "$0")/inputs/brickstorm.txt
CLEAR=$(dirname "$0")/inputs/brickstorm_clear.txt
[ -f "$SCRIPT" ] || fail "missing $SCRIPT"
[ -f "$CLEAR" ] || fail "missing $CLEAR"

FRAMES=2400

# Named-variable reads need an `nm` dump: the committed one (the golden ROM
# dir has it), else make one from the freshly built ELF.
SYMS=$ROM/brickstorm.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/brickstorm.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/brickstorm.elf" > "$OUT/brickstorm.syms"
    SYMS="$OUT/brickstorm.syms"
fi

# A "vars" line looks like:
#   vars  1200 score=3080 state=2 level=1 lives=0 boss_hp=0
# `has KEY VALUE` greps the trace for a line carrying that key=value.
has() {
    grep -qE "^vars +[0-9]+ .*(^| )$1=$2( |$)" "$OUT/trace.txt"
}

if [ -f "$SYMS" ]; then
    # --- 1) determinism ----------------------------------------------------
    "$CLI" run "$GAME" --frames 250 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state --quiet > "$OUT/r1.txt" 2>/dev/null
    "$CLI" run "$GAME" --frames 250 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state --quiet > "$OUT/r2.txt" 2>/dev/null
    if ! cmp -s "$OUT/r1.txt" "$OUT/r2.txt"; then
        diff "$OUT/r1.txt" "$OUT/r2.txt" | head -5 >&2
        fail "two identical sessions produced different output"
    fi
    note "determinism ok — two 250-frame sessions identical"

    # --- 2) the story ------------------------------------------------------
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state,level,lives,boss_hp --var-every 25 \
        > "$OUT/main.txt" 2>/dev/null
    cat "$OUT/main.txt"
    grep '^vars' "$OUT/main.txt" > "$OUT/trace.txt"

    # the title holds until START
    has state 0 ||
        fail "the session should open on the title screen"
    # the score climbs
    grep -qE '^vars +[0-9]+ .*score=[1-9][0-9]*' "$OUT/trace.txt" ||
        fail "the score should climb while the ball is in play"
    # the game over (it happens mid-session: the title comes back, the
    # level picker picks the boss stage)
    has state 5 ||
        fail "the session should reach the game over"
    # the level picker: the boss stage
    has level 13 ||
        fail "the level picker should reach the boss stage"
    # the boss is fought and the game is won
    grep -qE '^vars +[0-9]+ .*boss_hp=[1-9][0-9]*' "$OUT/trace.txt" ||
        fail "the boss should arrive with hit points"
    has state 6 ||
        fail "the session should end with the boss beaten"
    note "story ok — the title, level 1, the game over, the boss, the win"

    # --- 3) the level progression ------------------------------------------
    "$CLI" run "$GAME" --frames 1750 --input "$CLEAR" --syms "$SYMS" \
        --vars level,bricks_left,state --var-every 25 > "$OUT/clear.txt" 2>/dev/null
    grep -qE '^vars +[0-9]+ level=9 ' "$OUT/clear.txt" ||
        fail "a cleared stage should move the game on to the next one"
    note "progression ok — a stage cleared, the game moved on"

    # --- 4) record -> replay round trip ------------------------------------
    "$CLI" run "$GAME" --frames 700 --input "$SCRIPT" \
        --record "$OUT/rec.txt" --quiet > /dev/null 2>&1
    "$CLI" run "$GAME" --frames 700 --input "$OUT/rec.txt" \
        --syms "$SYMS" --vars score,state > "$OUT/re.txt" 2>/dev/null
    "$CLI" run "$GAME" --frames 700 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state > "$OUT/main2.txt" 2>/dev/null
    grep '^frame ' "$OUT/main2.txt" > "$OUT/main.f"
    grep '^frame ' "$OUT/re.txt"   > "$OUT/re.f"
    cmp -s "$OUT/main.f" "$OUT/re.f" ||
        fail "the recorded session does not replay bit-identically"
    note "record->replay ok — the recording replays the golden session"
else
    note "named variables skipped — no brickstorm.syms and no riscv nm to make one"
fi

echo "test-brickstorm: brickstorm ok (determinism, story, progression, record-replay)"
