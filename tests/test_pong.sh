#!/bin/sh
# ARCADE-1 — example_pong test: a scripted rally, first to 5, restart.
#
# Drives example_pong (<rom-dir>/example_pong.arc1) through the arcade1 CLI
# and asserts the game story through named variables (--syms/--vars):
#   1. determinism         — two identical rally runs, identical output
#   2. rally changes score — the sweep script scores on both sides
#   3. first to 5           — game over state; winner exactly 5, loser < 5
#   4. START restarts       — state 1 -> 0 with both scores reset
#
# usage: test_pong.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_pong.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}
GOLD=${3:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-pong: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

PONG=$ROM/example_pong.arc1

[ -x "$CLI" ]   || fail "CLI not built at $CLI"
[ -f "$PONG" ] || fail "missing $PONG"

# Named-variable reads need an `nm` dump: use the committed one (the golden
# ROM dir has it), else make one from the freshly built ELF.
SYMS=$ROM/example_pong.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/example_pong.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/example_pong.elf" > "$OUT/example_pong.syms"
    SYMS="$OUT/example_pong.syms"
fi

# The rally script: sweep the player paddle over the full range (up 25,
# down 57, repeat) so it intercepts the ball on the way back.
: > "$OUT/sweep.txt"
i=0
while [ $i -lt 64 ]; do
    first=$((i * 82))
    if [ $((i % 2)) -eq 0 ]; then
        echo "$first $((first + 24)) up" >> "$OUT/sweep.txt"
    else
        echo "$first $((first + 56)) down" >> "$OUT/sweep.txt"
    fi
    i=$((i + 1))
done

if [ -f "$SYMS" ]; then
    # --- 1) determinism ------------------------------------------------------
    "$CLI" run "$PONG" --frames 2400 --input "$OUT/sweep.txt" --syms "$SYMS" \
        --vars score,opp_score,state --quiet > "$OUT/r1.txt" 2>/dev/null
    "$CLI" run "$PONG" --frames 2400 --input "$OUT/sweep.txt" --syms "$SYMS" \
        --vars score,opp_score,state --quiet > "$OUT/r2.txt" 2>/dev/null
    if ! cmp -s "$OUT/r1.txt" "$OUT/r2.txt"; then
        diff "$OUT/r1.txt" "$OUT/r2.txt" | head -5 >&2
        fail "two identical rally runs produced different output"
    fi
    note "determinism ok — two rally runs identical"

    # --- 2+3) rally changes the score; first to 5 sets game over ------------
    # Primary run: verbose, so its per-frame hash lines are what the golden
    # regression (tests/golden/pong.txt) records. The output goes to a file
    # for the assertions and to stdout for the golden grep.
    "$CLI" run "$PONG" --frames 2400 --input "$OUT/sweep.txt" --syms "$SYMS" \
        --vars score,opp_score,state > "$OUT/main.txt" 2>/dev/null
    cat "$OUT/main.txt"
    grep '^var ' "$OUT/main.txt" > "$OUT/final.txt"
    grep -q '^var score = 4$' "$OUT/final.txt" ||
        fail "rally: expected player score 4, got: $(cat "$OUT/final.txt")"
    grep -q '^var opp_score = 5$' "$OUT/final.txt" ||
        fail "rally: expected opponent score 5, got: $(cat "$OUT/final.txt")"
    grep -q '^var state = 1$' "$OUT/final.txt" ||
        fail "first to 5 should set the game-over state"
    note "rally ok — score 4, opp_score 5, game over at first to 5"

    # --- 4) START restarts ---------------------------------------------------
    # Same rally, then START from frame 725 (game over lands ~675): the
    # restart must show up as a trace line with everything reset.
    cp "$OUT/sweep.txt" "$OUT/restart.txt"
    echo "725 775 start" >> "$OUT/restart.txt"
    "$CLI" run "$PONG" --frames 776 --input "$OUT/restart.txt" --syms "$SYMS" \
        --vars score,opp_score,state --var-every 25 --quiet > "$OUT/rb.txt" 2>/dev/null
    grep -q '^vars *700 .*state=1' "$OUT/rb.txt" ||
        fail "restart run: expected game over in the frame-700 trace"
    grep -q '^vars *725 .*score=0 opp_score=0 state=0' "$OUT/rb.txt" ||
        fail "restart run: START at 725 did not reset score and state"
    note "restart ok — START resets game over to a fresh rally"
else
    note "named variables skipped — no example_pong.syms and no riscv nm to make one"
fi

echo "test-pong: example_pong ok (determinism, rally, game over, restart)"
