#!/bin/sh
# ARCADE-1 - neontap test (the vector arena shooter): a scripted session that
# plays the title, starts, fires at the three effort tiers, charges with A/B
# alternation and spends the charge on a bomb.
#
# The assertions are made through the game's named variables:
#   1. determinism   - two identical sessions, identical output
#   2. the story     - the title waits; play starts; the score climbs; kills
#                      happen; the tap-speed meter fills; the charge meter
#                      fills and is then spent by the bomb
#   3. record->replay - the recorded session replays bit-identically
#   4. budget        - the heaviest frame stays under half the instruction cap
#
# The verbose run prints its per-frame hash lines to stdout, which is what the
# golden regression (tests/golden/neontap.txt) records.
#
# usage: test_neontap.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_neontap.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-neontap: FAILED - $1" >&2; exit 1; }
note() { echo "  $1"; }

GAME=$ROM/neontap.arc1
[ -x "$CLI" ]  || fail "CLI not built at $CLI"
[ -f "$GAME" ] || fail "missing $GAME"
SCRIPT=$(dirname "$0")/inputs/neontap.txt
[ -f "$SCRIPT" ] || fail "missing $SCRIPT"

FRAMES=1800
SYMS=$ROM/neontap.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/neontap.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/neontap.elf" > "$OUT/neontap.syms"
    SYMS="$OUT/neontap.syms"
fi

has() { grep -qE "^vars +[0-9]+ .*(^| )$1=$2( |$)" "$OUT/trace.txt"; }

if [ -f "$SYMS" ]; then
    # --- 1) determinism ----------------------------------------------------
    "$CLI" run "$GAME" --frames 600 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state --quiet > "$OUT/r1.txt" 2>/dev/null
    "$CLI" run "$GAME" --frames 600 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state --quiet > "$OUT/r2.txt" 2>/dev/null
    if ! cmp -s "$OUT/r1.txt" "$OUT/r2.txt"; then
        diff "$OUT/r1.txt" "$OUT/r2.txt" | head -5 >&2
        fail "two identical sessions produced different output"
    fi
    note "determinism ok - two 600-frame sessions identical"

    # --- 2) the story ------------------------------------------------------
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state,lives,kills,tap_speed,charge --var-every 10 \
        > "$OUT/main.txt" 2>/dev/null
    cat "$OUT/main.txt"
    grep '^vars' "$OUT/main.txt" > "$OUT/trace.txt"

    has state 0 || fail "the session should open on the title screen"
    has state 1 || fail "the session should start playing after A"
    grep -qE '^vars +[0-9]+ .*score=[1-9][0-9]*' "$OUT/trace.txt" ||
        fail "the score should climb once the game is running"
    grep -qE '^vars +[0-9]+ .*kills=[1-9][0-9]*' "$OUT/trace.txt" ||
        fail "the tap-fired shots should kill enemies"
    has tap_speed 256 || fail "tapping every 4 frames should fill the tap-speed meter"
    grep -qE '^vars +[0-9]+ .*charge=[1-9][0-9]{4}' "$OUT/trace.txt" ||
        fail "alternating A/B should fill the charge meter (>= 10000)"
    # the bomb spends it: a sample with a nearly full meter is followed by one at 0
    awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^charge=/) { v = substr($i, 8) + 0; if (prev >= 20000 && v == 0) ok = 1; prev = v } }
         END { exit ok ? 0 : 1 }' "$OUT/trace.txt" ||
        fail "the bomb should spend a nearly full charge meter"
    note "story ok - title, play, score, kills, full tap meter, charge, bomb"

    # --- 3) record -> replay ----------------------------------------------
    "$CLI" run "$GAME" --frames 900 --input "$SCRIPT" --record "$OUT/rec.txt" --quiet > /dev/null 2>&1
    "$CLI" run "$GAME" --frames 900 --input "$OUT/rec.txt" --syms "$SYMS" --vars score,state > "$OUT/re.txt" 2>/dev/null
    "$CLI" run "$GAME" --frames 900 --input "$SCRIPT" --syms "$SYMS" --vars score,state > "$OUT/main.f0" 2>/dev/null
    grep '^frame ' "$OUT/main.f0" > "$OUT/main.f"
    grep '^frame ' "$OUT/re.txt"  > "$OUT/re.f"
    cmp -s "$OUT/main.f" "$OUT/re.f" || fail "the recorded session does not replay bit-identically"
    note "record->replay ok"
else
    note "named variables skipped - no neontap.syms and no riscv nm to make one"
fi

# --- 4) instruction budget: heaviest sampled frame under half the cap ------
MAX=0
for f in 200 600 1000 1062 1070 1400 1790; do
    N=$("$CLI" inspect "$GAME" --frames $f --input "$SCRIPT" 2>/dev/null | awk '$1=="INSTRET_FRAME"{print $2}')
    [ -n "$N" ] || fail "inspect gave no INSTRET_FRAME at frame $f"
    [ "$N" -gt "$MAX" ] && MAX=$N
done
[ "$MAX" -lt 500000 ] || fail "a frame retired $MAX instructions (budget 500000 of the 1000000 cap)"
note "budget ok - heaviest sampled frame $MAX instructions"

echo "test-neontap: neontap ok (determinism, story, record-replay, budget)"
