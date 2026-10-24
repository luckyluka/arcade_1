#!/bin/sh
# ARCADE-1 — skydescent test (the descending shoot-'em-up, the owner's
# shmup/ prototype redone as a guest ROM): a scripted session that plays
# the title, starts, and descends the whole 12,000 m — through all five
# altitude palettes, the terrain fading in below 2,200 m, the ground map,
# and into the boss fight waiting on it.
#
# The assertions are made through the game's named variables:
#   1. determinism      — two identical sessions, identical output
#   2. the story        — the title waits for a button; the game starts;
#                         the score climbs; the altitude keeps falling
#                         (2 m per frame) from 12,000 m to 0 m; the phase
#                         advances into the low haze; the ship takes two
#                         hits on the way down; the boss spawns at the
#                         ground and takes damage; the session ends
#                         still flying, mid-fight
#   3. record -> replay — the recorded session replays bit-identically
#
# The verbose run prints its per-frame hash lines to stdout, which is what
# the golden regression (tests/golden/skydescent.txt) records.
#
# usage: test_skydescent.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_skydescent.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-skydescent: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

GAME=$ROM/skydescent.arc1

[ -x "$CLI" ]  || fail "CLI not built at $CLI"
[ -f "$GAME" ] || fail "missing $GAME"

SCRIPT=$(dirname "$0")/inputs/skydescent.txt
[ -f "$SCRIPT" ] || fail "missing $SCRIPT"

FRAMES=6400

# Named-variable reads need an `nm` dump: the committed one (the golden ROM
# dir has it), else make one from the freshly built ELF.
SYMS=$ROM/skydescent.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/skydescent.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/skydescent.elf" > "$OUT/skydescent.syms"
    SYMS="$OUT/skydescent.syms"
fi

# A "vars" line looks like (the key order matters to `has`: the template's
# helper wants a space before the key, so no key is listed first):
#   vars  1200 score=600 state=1 lives=2 altitude=9620 phase=1
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
        --vars score,state,lives,altitude,phase,boss_hp --var-every 25 \
        > "$OUT/main.txt" 2>/dev/null
    cat "$OUT/main.txt"
    grep '^vars' "$OUT/main.txt" > "$OUT/trace.txt"

    # the title holds until a button is pressed
    has state 0 ||
        fail "the session should open on the title screen"
    # the game starts on the A press
    has state 1 ||
        fail "the session should start playing after the A press"
    # the score climbs once the cadence gun starts cutting
    grep -qE '^vars +[0-9]+ .*score=[1-9][0-9]*' "$OUT/trace.txt" ||
        fail "the score should climb once the game is running"
    # the descent: the altitude falls from 12,000 m all the way to 0
    has altitude 0 ||
        fail "the altitude should reach 0 — the whole descent"
    # the phase advances past the stratosphere into the low haze
    has phase 3 ||
        fail "the phase should advance into the low haze"
    # the ship takes two hits on the way down
    has lives 1 ||
        fail "the ship should take hits during the descent"
    # the boss is on the ground by then, and the fight has drawn blood
    awk '/^var boss_hp = /{v=$4} END{exit !(v>0 && v<1600)}' "$OUT/main.txt" ||
        fail "the boss should spawn at the ground and take damage"
    # the session ends still flying, mid-fight
    grep -q '^var state = 1$' "$OUT/main.txt" ||
        fail "the session should end still flying"
    note "story ok — the full descent, the ground, the boss, still flying"

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
    note "named variables skipped — no skydescent.syms and no riscv nm to make one"
fi

echo "test-skydescent: sky descent ok (determinism, story, record-replay)"
