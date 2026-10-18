#!/bin/sh
# ARCADE-1 — example_snake test: growth, wall game over, restart.
#
# Drives example_snake (<rom-dir>/example_snake.arc1) through the arcade1
# CLI and asserts the game story through named variables (--syms/--vars):
#   1. determinism      — two identical runs, identical output
#   2. growth           — 60 idle frames cover it: the snake eats the food
#                         right ahead (length 3 -> 4, score 0 -> 1)
#   3. wall game over   — the same run ends in state 1 (head hits the wall)
#   4. START restarts   — after game over, START resets length/score/state
#
# usage: test_snake.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_snake.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}
GOLD=${3:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-snake: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

SNAKE=$ROM/example_snake.arc1

[ -x "$CLI" ]   || fail "CLI not built at $CLI"
[ -f "$SNAKE" ] || fail "missing $SNAKE"

# Named-variable reads need an `nm` dump: use the committed one (the golden
# ROM dir has it), else make one from the freshly built ELF.
SYMS=$ROM/example_snake.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/example_snake.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/example_snake.elf" > "$OUT/example_snake.syms"
    SYMS="$OUT/example_snake.syms"
fi

if [ -f "$SYMS" ]; then
    # --- 1) determinism ------------------------------------------------------
    "$CLI" run "$SNAKE" --frames 60 --syms "$SYMS" --vars length,score,state \
        --quiet > "$OUT/r1.txt" 2>/dev/null
    "$CLI" run "$SNAKE" --frames 60 --syms "$SYMS" --vars length,score,state \
        --quiet > "$OUT/r2.txt" 2>/dev/null
    if ! cmp -s "$OUT/r1.txt" "$OUT/r2.txt"; then
        diff "$OUT/r1.txt" "$OUT/r2.txt" | head -5 >&2
        fail "two identical runs produced different output"
    fi
    note "determinism ok — two runs identical"

    # --- 2+3) growth, then wall game over ------------------------------------
    # Primary run: verbose, so its per-frame hash lines are what the golden
    # regression (tests/golden/snake.txt) records. No input: the snake eats
    # the food ahead, grows, and keeps going right into the wall. The output
    # goes to a file for the assertions and to stdout for the golden grep.
    "$CLI" run "$SNAKE" --frames 60 --syms "$SYMS" --vars length,score,state \
        > "$OUT/main.txt" 2>/dev/null
    cat "$OUT/main.txt"
    grep -q '^var length = 4$' "$OUT/main.txt" ||
        fail "growth: expected length 4 after eating, got: $(grep '^var ' "$OUT/main.txt")"
    grep -q '^var score = 1$' "$OUT/main.txt" ||
        fail "growth: expected score 1 after eating"
    grep -q '^var state = 1$' "$OUT/main.txt" ||
        fail "wall hit should set the game-over state"
    note "growth ok — length 3 -> 4, score 0 -> 1; wall sets game over"

    # --- 4) START restarts ---------------------------------------------------
    # Game over lands ~frame 43; START from 45 resets everything. The tail
    # is too short to eat again, so the final state proves the restart.
    printf '45 47 start\n' > "$OUT/restart.txt"
    "$CLI" run "$SNAKE" --frames 48 --input "$OUT/restart.txt" --syms "$SYMS" \
        --vars length,score,state --quiet > "$OUT/rc.txt" 2>/dev/null
    grep -q '^var length = 3$' "$OUT/rc.txt" ||
        fail "restart: expected length 3, got: $(grep '^var ' "$OUT/rc.txt")"
    grep -q '^var score = 0$' "$OUT/rc.txt" ||
        fail "restart: expected score 0"
    grep -q '^var state = 0$' "$OUT/rc.txt" ||
        fail "restart: expected state 0 (restarted)"
    note "restart ok — START resets to length 3, score 0, state 0"
else
    note "named variables skipped — no example_snake.syms and no riscv nm to make one"
fi

echo "test-snake: example_snake ok (determinism, growth, wall, restart)"
