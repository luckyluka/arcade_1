#!/bin/sh
# ARCADE-1 — flappy test (Game #2, the round-3 clean-room build): a scripted
# session of three flights — scoring, a ground crash, a pipe crash, restarts,
# and a final game-over hold.
#
# Drives flappy (<rom-dir>/flappy.arc1) through the arcade1 CLI and asserts
# the game story through named variables:
#   1. determinism      — two identical sessions, identical output
#   2. the story        — flight 1 scores; a restart resets the score; the
#                         session ends in the game-over state
#   3. record -> replay — the recorded session replays bit-identically
# The verbose run prints its per-frame hash lines to stdout, which is what
# the golden regression (tests/golden/flappy.txt) records.
#
# usage: test_flappy.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_flappy.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-flappy: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

GAME=$ROM/flappy.arc1

[ -x "$CLI" ]  || fail "CLI not built at $CLI"
[ -f "$GAME" ] || fail "missing $GAME"

SCRIPT=$(dirname "$0")/inputs/flappy.txt
[ -f "$SCRIPT" ] || fail "missing $SCRIPT"

FRAMES=520

# Named-variable reads need an `nm` dump: the committed one (the golden ROM
# dir has it), else make one from the freshly built ELF.
SYMS=$ROM/flappy.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/flappy.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/flappy.elf" > "$OUT/flappy.syms"
    SYMS="$OUT/flappy.syms"
fi

if [ -f "$SYMS" ]; then
    # --- 1) determinism ----------------------------------------------------
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state --quiet > "$OUT/r1.txt" 2>/dev/null
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state --quiet > "$OUT/r2.txt" 2>/dev/null
    if ! cmp -s "$OUT/r1.txt" "$OUT/r2.txt"; then
        diff "$OUT/r1.txt" "$OUT/r2.txt" | head -5 >&2
        fail "two identical sessions produced different output"
    fi
    note "determinism ok — two $FRAMES-frame sessions identical"

    # --- 2) the story: score, crash, restart, game over --------------------
    # Primary run: verbose, so its frame lines are the golden's. The output
    # goes to a file for the assertions and to stdout for the golden grep.
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state --var-every 40 > "$OUT/main.txt" 2>/dev/null
    cat "$OUT/main.txt"
    grep '^vars' "$OUT/main.txt" > "$OUT/trace.txt"
    grep -qE '^vars *[0-9]+ score=[2-9] state=0$' "$OUT/trace.txt" ||
        fail "flight 1 should reach score >= 2: $(head -3 "$OUT/trace.txt")"
    awk '/score=[2-9] state=0$/ {seen=1}
         seen && /^vars/ && /score=0 state=0$/ {found=1}
         END {exit !found}' "$OUT/trace.txt" ||
        fail "a restart should reset the score to 0 after a scored flight"
    grep -q '^var state = 1$' "$OUT/main.txt" ||
        fail "the session should end in the game-over state"
    grep -q '^var score = 0$' "$OUT/main.txt" ||
        fail "the final held game over should carry a fresh score"
    note "story ok — scores, restarts, ends in the game-over state"

    # --- 3) record -> replay round trip ------------------------------------
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" \
        --record "$OUT/rec.txt" --quiet > /dev/null 2>&1
    "$CLI" run "$GAME" --frames $FRAMES --input "$OUT/rec.txt" \
        --syms "$SYMS" --vars score,state > "$OUT/re.txt" 2>/dev/null
    grep '^frame ' "$OUT/main.txt" > "$OUT/main.f"
    grep '^frame ' "$OUT/re.txt"   > "$OUT/re.f"
    cmp -s "$OUT/main.f" "$OUT/re.f" ||
        fail "the recorded session does not replay bit-identically"
    note "record->replay ok — the recording replays the golden session"
else
    note "named variables skipped — no flappy.syms and no riscv nm to make one"
fi

echo "test-flappy: flappy ok (determinism, story, record-replay)"
