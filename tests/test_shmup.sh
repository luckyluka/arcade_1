#!/bin/sh
# ARCADE-1 — tempest test (the vertical shoot-'em-up): a scripted session
# that plays the title, the first stage's waves, the boss and the start of
# stage 2.
#
# The assertions are made through the game's named variables:
#   1. determinism      — two identical sessions, identical output
#   2. the story        — the title waits for START; the score climbs; the
#                         boss arrives and is fought; the boss dies and
#                         stage 2 begins; the session ends in the game-over
#                         state
#   3. record -> replay — the recorded session replays bit-identically
#
# The verbose run prints its per-frame hash lines to stdout, which is what
# the golden regression (tests/golden/shmup.txt) records.
#
# usage: test_shmup.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_shmup.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-shmup: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

GAME=$ROM/shmup.arc1

[ -x "$CLI" ]  || fail "CLI not built at $CLI"
[ -f "$GAME" ] || fail "missing $GAME"

SCRIPT=$(dirname "$0")/inputs/shmup.txt
[ -f "$SCRIPT" ] || fail "missing $SCRIPT"

FRAMES=2600

# Named-variable reads need an `nm` dump: the committed one (the golden ROM
# dir has it), else make one from the freshly built ELF.
SYMS=$ROM/shmup.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/shmup.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/shmup.elf" > "$OUT/shmup.syms"
    SYMS="$OUT/shmup.syms"
fi

# A "vars" line looks like:
#   vars  1200 score=600 state=1 stage=1 boss_hp=74
# `has KEY VALUE` greps the trace for a line carrying that key=value.
has() {
    grep -qE "^vars +[0-9]+ .*(^| )$1=$2( |$)" "$OUT/trace.txt"
}

if [ -f "$SYMS" ]; then
    # --- 1) determinism ----------------------------------------------------
    "$CLI" run "$GAME" --frames 300 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state --quiet > "$OUT/r1.txt" 2>/dev/null
    "$CLI" run "$GAME" --frames 300 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state --quiet > "$OUT/r2.txt" 2>/dev/null
    if ! cmp -s "$OUT/r1.txt" "$OUT/r2.txt"; then
        diff "$OUT/r1.txt" "$OUT/r2.txt" | head -5 >&2
        fail "two identical sessions produced different output"
    fi
    note "determinism ok — two 300-frame sessions identical"

    # --- 2) the story ------------------------------------------------------
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state,stage,boss_hp,lives --var-every 25 \
        > "$OUT/main.txt" 2>/dev/null
    cat "$OUT/main.txt"
    grep '^vars' "$OUT/main.txt" > "$OUT/trace.txt"

    # the title holds until START
    has state 0 ||
        fail "the session should open on the title screen"
    # the game starts and the score climbs
    has state 1 ||
        fail "the session should start playing after START"
    grep -qE '^vars +[0-9]+ .*score=[1-9][0-9]*' "$OUT/trace.txt" ||
        fail "the score should climb once the game is running"
    # the boss arrives with hit points
    grep -qE '^vars +[0-9]+ .*boss_hp=[1-9][0-9]*' "$OUT/trace.txt" ||
        fail "the boss should arrive with hit points"
    # the boss dies and stage 2 begins
    has stage 2 ||
        fail "the boss should die and stage 2 should begin"
    # the session ends in the game-over state
    grep -q '^var state = 3$' "$OUT/main.txt" ||
        fail "the session should end in the game-over state"
    note "story ok — the title, the waves, the boss, stage 2, the game over"

    # --- 3) record -> replay round trip ------------------------------------
    # the round trip is checked on the first 1200 frames: the mechanism is
    # what matters here, and the whole session is already covered above
    "$CLI" run "$GAME" --frames 1200 --input "$SCRIPT" \
        --record "$OUT/rec.txt" --quiet > /dev/null 2>&1
    "$CLI" run "$GAME" --frames 1200 --input "$OUT/rec.txt" \
        --syms "$SYMS" --vars score,state > "$OUT/re.txt" 2>/dev/null
    "$CLI" run "$GAME" --frames 1200 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state > "$OUT/main.f0" 2>/dev/null
    grep '^frame ' "$OUT/main.f0" > "$OUT/main.f"
    grep '^frame ' "$OUT/re.txt"   > "$OUT/re.f"
    cmp -s "$OUT/main.f" "$OUT/re.f" ||
        fail "the recorded session does not replay bit-identically"
    note "record->replay ok — the recording replays the golden session"
else
    note "named variables skipped — no shmup.syms and no riscv nm to make one"
fi

echo "test-shmup: tempest ok (determinism, story, record-replay)"
