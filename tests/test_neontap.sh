#!/bin/sh
# ARCADE-1 - neontap test (the vector arena shooter): a scripted session that
# plays the title, starts, fires at the three effort tiers, charges with A/B
# alternation and spends the charge on a bomb; charge+bomb cycles recur through
# the session so the run survives the sector bosses. The session watches a
# whole design credit: Sector 1's waves and Sentinel, then Sectors 2-5, each
# with its arena rule and its boss (Hydra, Orbiter, Weaver, Core), then the
# run looping back into Sector 1 for a second credit.
#
# The assertions are made through the game's named variables:
#   1. determinism   - two identical sessions, identical output
#   2. the story     - the title waits; play starts; the score climbs; kills
#                      happen; the tap-speed meter fills; the charge meter
#                      fills and is then spent by the bomb; the whole enemy
#                      vocabulary of the design document appears, turrets fire
#                      and bombers mine (M1); overdrive latches under fast
#                      tapping; a clean-sweep bomb pays out; the Sentinel
#                      comes out, loses its plates, dies, and the run advances
#                      to Sector 2 (M2); the four later bosses fight with the
#                      grammars the design gives them, every arena rule shows,
#                      the 50,000 mark grants an extra life and the credit
#                      completes so the run loops (M3); the credit's pad is
#                      recorded into the design doc 9 replay stream and the
#                      next credit flies the best run as a ghost (M5)
#   3. record->replay - the recorded session replays bit-identically, and the
#                      ghost's path is the recorded run's path, frame-exact
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

FRAMES=4000
SYMS=$ROM/neontap.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/neontap.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/neontap.elf" > "$OUT/neontap.syms"
    SYMS="$OUT/neontap.syms"
fi

has() { grep -qE "^vars +[0-9]+ .*(^| )$1=$2( |$)" "$OUT/trace.txt"; }

if [ -f "$SYMS" ]; then
    # --- 1) determinism ----------------------------------------------------
    # rec_n / rec_crc are in the mix: the design doc 9 recording is part of
    # what must reproduce exactly, since a replay that recomputes the same
    # stream checksum is the design's verification step
    "$CLI" run "$GAME" --frames 600 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state,rec_n,rec_crc --quiet > "$OUT/r1.txt" 2>/dev/null
    "$CLI" run "$GAME" --frames 600 --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state,rec_n,rec_crc --quiet > "$OUT/r2.txt" 2>/dev/null
    if ! cmp -s "$OUT/r1.txt" "$OUT/r2.txt"; then
        diff "$OUT/r1.txt" "$OUT/r2.txt" | head -5 >&2
        fail "two identical sessions produced different output"
    fi
    note "determinism ok - two 600-frame sessions identical"

    # --- 2) the story ------------------------------------------------------
    # One trace serves every section below: --var-every 1 so the ghost's
    # path can be compared to the recorded run's path frame by frame.
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" --syms "$SYMS" \
        --vars score,state,lives,kills,tap_speed,charge,kinds_seen,kinds_killed,eh_fired,mines_dropped,sector,loop,sweeps,nd,extras,overdrive,boss_seen,boss_k,boss_hp,boss_plates,boss_n,boss_ph,arena,gates,pyl_hits,wall_in,boss_kills,rec_n,rec_crc,gst_score,gh_on,px,py,gpx,gpy \
        --var-every 1 > "$OUT/main.txt" 2>/dev/null
    # The frame lines go to stdout verbatim: the golden regression
    # (tests/golden/neontap.txt) greps them out of this test's log. The vars
    # lines stay out of the log - 4000 of them would drown it.
    grep '^frame ' "$OUT/main.txt"
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
    # M1 enemy grammar: all 11 kinds appear (kinds_seen bits 0..10 = 2047), and
    # the two credits the session runs kill every kind too (kinds_killed =
    # 2047: the weaver chain that used to outlive Sector 1's arena clear is
    # hunted down in the later sectors)
    grep -qE '^vars +[0-9]+ .*kinds_seen=2047( |$)' "$OUT/trace.txt" ||
        fail "every enemy kind in the design document's vocabulary should appear"
    grep -qE '^vars +[0-9]+ .*kinds_killed=2047( |$)' "$OUT/trace.txt" ||
        fail "the session should kill every enemy kind"
    grep -qE '^vars +[0-9]+ .*eh_fired=[1-9][0-9]*( |$)' "$OUT/trace.txt" ||
        fail "turrets should fire at the player"
    grep -qE '^vars +[0-9]+ .*mines_dropped=[1-9][0-9]*( |$)' "$OUT/trace.txt" ||
        fail "bombers should drop mines"
    # M2 scoring: the scripted taps hold speed above 0.8 for 3 s, so overdrive
    # latches; one bomb in the session kills 8+ enemies at once, which pays
    # the clean-sweep bonus (sweeps counts them)
    has overdrive 1 || fail "holding tap speed above 0.8 for 3 s should latch overdrive"
    grep -qE '^vars +[0-9]+ .*sweeps=[1-9][0-9]*( |$)' "$OUT/trace.txt" ||
        fail "a bomb killing 8+ enemies should count as a clean sweep"
    # M2 run structure: waves, then the Sentinel at full plates, then the
    # plates breaking, the core dying and the run advancing to Sector 2
    has boss_seen 1 || fail "the Sentinel should come out at the end of Sector 1's waves"
    has boss_plates 4 || fail "the Sentinel should start with four shield plates"
    # a mid-fight sample: the boss alive AND already missing plates (plain
    # boss_plates=0 also matches before it spawns and after it dies)
    grep -qE '^vars +[0-9]+ .*boss_seen=1 .*boss_plates=[123]( |$)' "$OUT/trace.txt" ||
        fail "the shield plates should break under fire"
    grep -qE '^vars +[0-9]+ .*sector=2( |$)' "$OUT/trace.txt" ||
        fail "killing the Sentinel should advance the run to Sector 2"
    # M3 arenas: every sector past 1 changes the room (design doc 5), and the
    # rules do something measurable - pylons block shots and the ship, the
    # ring's gates admit the waves, the crush walls close to their full inset
    has arena 2 || fail "Sector 2 should be the pylon arena"
    has arena 3 || fail "Sector 3 should be the ring arena"
    has arena 4 || fail "Sector 4 should be the crush arena"
    has arena 5 || fail "Sector 5 should be the dark-grid arena"
    grep -qE '^vars +[0-9]+ .*pyl_hits=[1-9][0-9]*( |$)' "$OUT/trace.txt" ||
        fail "the pylons should block shots and the ship"
    grep -qE '^vars +[0-9]+ .*gates=[1-9][0-9]*( |$)' "$OUT/trace.txt" ||
        fail "the ring's gates should admit the waves"
    has wall_in 90 || fail "the crush walls should close to their full 90 px inset"
    # M3 bosses: each sector's boss comes out and fights with its own grammar
    # (design doc 5) - the Hydra splits into three bodies at 66% of its hits
    # left and again at 33%; the Orbiter is a core with four satellites; the
    # Weaver is a chain you kill head-or-all-segments; the Core is a
    # three-phase fortress whose last phase must be seen
    has boss_k 2 || fail "the Hydra should come out at the end of Sector 2"
    grep -qE '^vars +[0-9]+ .*boss_k=2 .*boss_n=[2-9]( |$)' "$OUT/trace.txt" ||
        fail "the Hydra should split into three bodies"
    has boss_k 3 || fail "the Orbiter should come out at the end of Sector 3"
    grep -qE '^vars +[0-9]+ .*boss_k=3 .*boss_n=4( |$)' "$OUT/trace.txt" ||
        fail "the Orbiter should carry four satellites"
    has boss_k 4 || fail "the Weaver should come out at the end of Sector 4"
    grep -qE '^vars +[0-9]+ .*boss_k=4 .*boss_n=[5-8]( |$)' "$OUT/trace.txt" ||
        fail "the Weaver should be a chain of segments"
    has boss_k 5 || fail "the Core should come out at the end of Sector 5"
    grep -qE '^vars +[0-9]+ .*boss_k=5 .*boss_ph=2( |$)' "$OUT/trace.txt" ||
        fail "the Core's second phase should arrive"
    grep -qE '^vars +[0-9]+ .*boss_k=5 .*boss_ph=3( |$)' "$OUT/trace.txt" ||
        fail "the Core's third phase should arrive"
    # M3 run economy: the whole credit plays out - five bosses fall, untouched
    # sectors pay the no-damage bonus, the 50,000 mark grants the extra life
    # the run then lives on, and Sector 5's boss dying loops the run back to
    # Sector 1 with loop = 1
    has boss_kills 5 || fail "the credit should kill all five bosses"
    grep -qE '^vars +[0-9]+ .*nd=[1-9][0-9]*( |$)' "$OUT/trace.txt" ||
        fail "a sector should be cleared without a hit"
    has extras 1 || fail "the 50,000 mark should grant an extra life"
    has sector 5 || fail "the run should reach Sector 5"
    has loop 1 || fail "completing five sectors should loop the run"
    # M5 recording: the credit's pad is packed a byte per frame into the
    # design doc 9 stream - it is as long as the credit was - and the second
    # credit flies the first one as the ghost, carrying its score
    grep -qE '^vars +[0-9]+ .*rec_n=[0-9]{3,}( |$)' "$OUT/trace.txt" ||
        fail "the credit's frames should be recorded into the replay stream"
    has gh_on 1 || fail "the second credit should fly the first one as a ghost"
    # gst_score is the ghost's score = the best run's final score, and the
    # trace's own best score must agree (the ghost is the best run, design 9)
    awk '{ for (i = 3; i <= NF; i++) { split($i, a, "="); v[a[1]] = a[2] }
           if (v["score"] + 0 > best) best = v["score"] + 0
           if (v["gst_score"] + 0 > 0) gst = v["gst_score"] + 0 }
         END { exit (gst == best && gst > 0) ? 0 : 1 }' "$OUT/trace.txt" ||
        fail "the ghost should carry the best run's final score"
    note "story ok - title, play, meters, vocabulary, overdrive, sweep, all five sectors, all five bosses, the loop, the recording, the ghost"

    # --- 3) record -> replay ----------------------------------------------
    "$CLI" run "$GAME" --frames 900 --input "$SCRIPT" --record "$OUT/rec.txt" --quiet > /dev/null 2>&1
    "$CLI" run "$GAME" --frames 900 --input "$OUT/rec.txt" --syms "$SYMS" --vars score,state,rec_n,rec_crc > "$OUT/re.txt" 2>/dev/null
    "$CLI" run "$GAME" --frames 900 --input "$SCRIPT" --syms "$SYMS" --vars score,state,rec_n,rec_crc > "$OUT/main.f0" 2>/dev/null
    grep '^frame ' "$OUT/main.f0" > "$OUT/main.f"
    grep '^frame ' "$OUT/re.txt"  > "$OUT/re.f"
    cmp -s "$OUT/main.f" "$OUT/re.f" || fail "the recorded session does not replay bit-identically"
    # the design doc 11.6 round-trip, stated the way doc 9 states verification:
    # the replayed run recomputes the same stream length, the same stream
    # checksum and the same score as the run it was recorded from (the CLI's
    # footer prints each named variable's final value)
    grep '^var ' "$OUT/main.f0" > "$OUT/main.v"
    grep '^var ' "$OUT/re.txt"  > "$OUT/re.v"
    cmp -s "$OUT/main.v" "$OUT/re.v" ||
        fail "the replay did not recompute the same rec_crc / rec_n / score"
    note "record->replay ok"

    # --- 3b) M5: the guest's own replay, observed as the ghost -------------
    # ship_step is a pure function of the recorded stream, so the ghost's k-th
    # play frame must fly exactly where the player's k-th play frame flew -
    # the design doc 11.6 round-trip, watched from inside the machine. The
    # per-frame story trace is what makes the two sequences align by play
    # index; the awk only reads its vars lines (the CLI also appends a
    # `var NAME = VALUE` footer and a `ran ...` summary line).
    awk '
        /^vars / {
          for (i = 3; i <= NF; i++) { split($i, a, "="); v[a[1]] = a[2] }
          if (v["state"] == 2) over = 1                     # credit 1 has ended
          if (v["state"] == 1 && !over) { n1++; x1[n1] = v["px"]; y1[n1] = v["py"] }
          if (v["gh_on"] == 1) { n2++; x2[n2] = v["gpx"]; y2[n2] = v["gpy"] }
        }
        END {
            if (n2 < 200) { print "the ghost flew only " n2 " frames"; exit 1 }
            if (n2 > n1)  { print "the ghost outlived the run it replays"; exit 1 }
            for (k = 1; k <= n2; k++) if (x1[k] != x2[k] || y1[k] != y2[k]) {
                print "ghost path diverges at play frame " k \
                      " (player " x1[k] "," y1[k] " ghost " x2[k] "," y2[k] ")"
                exit 1
            }
        }' "$OUT/trace.txt" || fail "the ghost should fly the recorded run's exact path"
    note "ghost ok - the best run replays as the ghost, frame-exact path"
else
    note "named variables skipped - no neontap.syms and no riscv nm to make one"
fi

# --- 4) instruction budget: heaviest sampled frame under half the cap ------
# Samples across the whole credit: quiet early play (200), the first bomb
# expanding (1062), the Sentinel fight (1200), the Hydra mid-split (1610),
# the ring arena's waves and gates (1950), the crush walls fully closed
# (2370), the Weaver chain (2450), the Core in phase 2 (2900) and phase 3
# with the vocabulary it summons (3400), and the ghost flying in credit 2
# (3900) - the M5 replay work runs every play frame alongside the real ship.
MAX=0
for f in 200 1062 1200 1610 1950 2370 2450 2900 3400 3900; do
    N=$("$CLI" inspect "$GAME" --frames $f --input "$SCRIPT" 2>/dev/null | awk '$1=="INSTRET_FRAME"{print $2}')
    [ -n "$N" ] || fail "inspect gave no INSTRET_FRAME at frame $f"
    [ "$N" -gt "$MAX" ] && MAX=$N
done
[ "$MAX" -lt 500000 ] || fail "a frame retired $MAX instructions (budget 500000 of the 1000000 cap)"
note "budget ok - heaviest sampled frame $MAX instructions"

echo "test-neontap: neontap ok (determinism, story, record-replay, budget)"
