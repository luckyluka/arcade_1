#!/bin/sh
# ARCADE-1 — petalblade_rig test (the art pass of ROADMAP_V4 card 7.7):
# a scripted session of three acts — a death, a restart, and a win at the
# torii gate.
#
# The rig variant of Petal Blade: the same game, the same session and the
# same story, with the hero rebuilt by the rig animator. The variant's
# golden is its own; the promoted game's golden is untouched.
#
# Drives petalblade_rig (<rom-dir>/petalblade_rig.arc1) through the arcade1
# CLI and asserts the game story through named variables:
#   1. determinism      — two identical sessions, identical output
#   2. the story        — the walk into the oni costs all three lives
#                         (state 2), START restarts (state 0, lives 3), and
#                         the run ends at the torii (state 1, score 70)
#   3. record -> replay — the recorded session replays bit-identically
# The verbose run prints its per-frame hash lines to stdout, which is what
# the golden regression (tests/golden/petalblade_rig.txt) records.
#
# usage: test_petalblade_rig.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_petalblade_rig.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}
GOLD=${3:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-petalblade_rig: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

GAME=$ROM/petalblade_rig.arc1

[ -x "$CLI" ]  || fail "CLI not built at $CLI"
[ -f "$GAME" ] || fail "missing $GAME"

SCRIPT=$(dirname "$0")/inputs/petalblade.txt
[ -f "$SCRIPT" ] || fail "missing $SCRIPT"

FRAMES=1202

# Named-variable reads need an `nm` dump: the committed one (the golden ROM
# dir has it), else make one from the freshly built ELF.
SYMS=$ROM/petalblade_rig.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/petalblade_rig.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/petalblade_rig.elf" > "$OUT/petalblade_rig.syms"
    SYMS="$OUT/petalblade_rig.syms"
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

    # --- 2) the story: death, restart, win ---------------------------------
    # Primary run: verbose, so its frame lines are the golden's. The output
    # goes to a file for the assertions and to stdout for the golden grep.
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state,lives --var-every 20 > "$OUT/main.txt" 2>/dev/null
    cat "$OUT/main.txt"
    grep '^vars' "$OUT/main.txt" > "$OUT/trace.txt"

    # act 1: the walk into the oni costs every life -> state 2 (dead)
    grep -qE '^vars *[0-9]+ .*state=2 lives=0$' "$OUT/trace.txt" ||
        fail "the session should reach the dead state (state 2, lives 0): $(head -3 "$OUT/trace.txt")"
    # act 2: START restarts -> state 0 again, with three lives
    awk '/state=2 lives=0$/ {dead=1}
         dead && /state=0 lives=3$/ {found=1}
         END {exit !found}' "$OUT/trace.txt" ||
        fail "START should restart the game with three lives"
    # act 3: the run ends at the torii -> state 1 (win)
    grep -q '^var state = 1$' "$OUT/main.txt" ||
        fail "the session should end in the win state (state 1)"
    grep -q '^var score = 70$' "$OUT/main.txt" ||
        fail "the winning run should score 70 (got $(grep '^var score' "$OUT/main.txt"))"
    grep -q '^var lives = 3$' "$OUT/main.txt" ||
        fail "the winning run should end with three lives"
    note "story ok — dies, restarts on START, wins at the torii with 70 points"

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
    note "named variables skipped — no petalblade_rig.syms and no riscv nm to make one"
fi

echo "test-petalblade_rig: ok (determinism, story, record-replay)"
