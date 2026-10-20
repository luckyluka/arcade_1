#!/bin/sh
# ARCADE-1 — ASTEROID RUN test (golden game 10, ROADMAP_V3 Wave 5): the
# Game #1 narrative through the full asset flow — sprites, Wave 3 animation,
# the Wave 4 imported level, text.h HUD, two enemy types, win, lose, restart.
#
# The committed script (tests/inputs/asteroid_run.txt) plays three acts:
#
#   1. the level   — idle -> fire in place -> sweep while firing; the run
#                    reaches the end of the scrolling level -> state 2 (WIN)
#   2. restart     — START after the win begins a fresh match; hands off,
#                    the aimed comets land -> state 1 (GAME OVER)
#   3. restart     — START again; the passive run dies the same death,
#                    proving the restart left no residue
#
# The three player animation states (idle/boost/fire, pstate 0/1/2) all
# appear in the named-variable trace, and the frame lines this test prints
# are golden game 10's (test-golden greps `^frame ` from its stdout).
#
# usage: test_asteroid_run.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_asteroid_run.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}
GOLD=${3:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-asteroid_run: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }
CHECKS=0
ok() { CHECKS=$((CHECKS + 1)); }

GAME=$ROM/asteroid_run.arc1

[ -x "$CLI" ]  || fail "CLI not built at $CLI"
[ -f "$GAME" ] || fail "missing $GAME"

# Named-variable reads need an `nm` dump: the committed one (the golden ROM
# dir has it), else make one from the freshly built ELF.
SYMS=$ROM/asteroid_run.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/asteroid_run.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/asteroid_run.elf" > "$OUT/asteroid_run.syms"
    SYMS="$OUT/asteroid_run.syms"
fi

SCRIPT=$(dirname "$0")/inputs/asteroid_run.txt
[ -f "$SCRIPT" ] || fail "missing $SCRIPT"

FRAMES=760

if [ -f "$SYMS" ]; then
    # --- 1) determinism ------------------------------------------------------
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" --syms "$SYMS" \
        --vars score,lives,state,scroll,pstate --var-every 10 --quiet \
        > "$OUT/r1.txt" 2>/dev/null
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" --syms "$SYMS" \
        --vars score,lives,state,scroll,pstate --var-every 10 --quiet \
        > "$OUT/r2.txt" 2>/dev/null
    if ! cmp -s "$OUT/r1.txt" "$OUT/r2.txt"; then
        diff "$OUT/r1.txt" "$OUT/r2.txt" | head -5 >&2
        fail "two identical runs produced different output"
    fi
    note "determinism ok — two $FRAMES-frame runs identical"
    ok

    # --- 2) the three acts: verbose, so its frame lines are the golden's -----
    # The output goes to a file for the assertions and to stdout for the
    # golden grep (test-golden takes the `frame ` lines from here).
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" --syms "$SYMS" \
        --vars score,lives,state,scroll,pstate --var-every 10 \
        > "$OUT/main.txt" 2>/dev/null
    cat "$OUT/main.txt"

    grep '^vars' "$OUT/main.txt" > "$OUT/trace.txt"

    # Act 1: the score rises while dodging, hits land but the sweep holds.
    grep -q '^vars *100 score=600 lives=3 state=0 ' "$OUT/trace.txt" ||
        fail "act 1: score should reach 600 by frame 100, got: $(grep '^vars *100' "$OUT/trace.txt")"
    ok
    grep -q '^vars *300 score=1800 lives=2 state=0 ' "$OUT/trace.txt" ||
        fail "act 1: a hit should take lives 3->2 by frame 300, got: $(grep '^vars *300' "$OUT/trace.txt")"
    ok

    # The WIN: the level's end, at full scroll, with the score banked.
    grep -q '^vars *370 score=3000 lives=1 state=2 scroll=370 ' \
        "$OUT/trace.txt" ||
        fail "act 1: expected LEVEL CLEAR (state 2) at frame 370, got: $(grep '^vars *370' "$OUT/trace.txt")"
    note "act 1 ok — sweep+fire clears the level: score 3000, lives 1"
    ok

    # Act 2: START after the win restarts; hands off, the aimed comets
    # take the three lives.
    grep -q '^vars *380 score=0 lives=3 state=0 scroll=0 ' "$OUT/trace.txt" ||
        fail "restart 1: START after the win did not reset (frame 380 trace)"
    ok
    grep -q '^vars *490 .*lives=0 state=1 scroll=102 ' "$OUT/trace.txt" ||
        fail "act 2: expected GAME OVER (state 1) at frame 490, got: $(grep '^vars *490' "$OUT/trace.txt")"
    note "act 2 ok — hands off after the restart, aimed comets end the run"
    ok

    # Act 3: the same deterministic passive death, twice.
    grep -q '^vars *530 score=0 lives=3 state=0 scroll=0 ' "$OUT/trace.txt" ||
        fail "restart 2: START after game over did not reset (frame 530 trace)"
    ok
    grep -q '^vars *640 .*lives=0 state=1 scroll=102 ' "$OUT/trace.txt" ||
        fail "act 3: expected the replayed GAME OVER at frame 640, got: $(grep '^vars *640' "$OUT/trace.txt")"
    note "act 3 ok — the passive death replays identically after restart"
    ok

    # All three Wave 3 player animation states appear: idle (0), boost
    # while sweeping (1), fire while shooting in place (2).
    for p in 0 1 2; do
        grep -q "pstate=$p\$" "$OUT/trace.txt" ||
            fail "player anim state $p (pstate) never appears in the trace"
        ok
    done
    note "animation ok — player states idle/boost/fire all observed"

    grep '^var ' "$OUT/main.txt" > "$OUT/final.txt"
    grep -q '^var score = 0$' "$OUT/final.txt" ||
        fail "expected final score 0, got: $(cat "$OUT/final.txt")"
    grep -q '^var lives = 0$' "$OUT/final.txt" ||
        fail "expected final lives 0, got: $(cat "$OUT/final.txt")"
    grep -q '^var state = 1$' "$OUT/final.txt" ||
        fail "expected the second game over, got: $(cat "$OUT/final.txt")"
    note "named variables ok — the run ends 0/0/1"
    ok
else
    # Without symbols the golden still needs frame lines: run verbose anyway.
    "$CLI" run "$GAME" --frames $FRAMES --input "$SCRIPT" > "$OUT/main.txt" 2>/dev/null
    cat "$OUT/main.txt"
    note "named variables skipped — no asteroid_run.syms and no riscv nm to make one"
fi

# --- 3) the win overlay: LEVEL CLEAR panel (ROADMAP A3-style draw proof) ----
# Frame 375: state 2 — the black panel with cyan edges and white text.
"$CLI" run "$GAME" --frames 375 --input "$SCRIPT" --capture "$OUT/win.ppm" \
    --quiet > /dev/null 2>&1
[ "$(head -c 2 "$OUT/win.ppm")" = "P6" ] || fail "win capture is not a P6 PPM"
ok

# Scan the payload for the panel colours (RGB565 -> the PPM writer's
# floor(v*255/max) expansion: black 0,0,0; cyan 0x07FF -> 0,255,255).
READ=$(tail -c $((480 * 270 * 3)) "$OUT/win.ppm" | od -An -v -tu1 | awk '
    { for (i = 1; i <= NF; i++) { c[n % 3] = $i; n++
        if (n % 3 == 0) {
            p = int(n / 3) - 1; x = p % 480; y = int(p / 480)
            if (c[0] == 0 && c[1] == 0 && c[2] == 0) black++
            if (c[0] == 0 && c[1] == 255 && c[2] == 255) cyan++
            if (x >= 196 && x <= 283 && y >= 116 && y <= 123 &&
                c[0] == 255 && c[1] == 255 && c[2] == 255) white++
        } } }
    END { printf "%d %d %d\n", black + 0, cyan + 0, white + 0 }')
set -- $READ
BLACK=$1; CYAN=$2; WHITE=$3

[ "$BLACK" -ge 15000 ] ||
    fail "win overlay: expected >= 15000 black panel pixels, got $BLACK"
[ "$CYAN" -ge 1000 ] ||
    fail "win overlay: expected >= 1000 cyan edge pixels, got $CYAN"
[ "$WHITE" -ge 80 ] ||
    fail "win overlay: LEVEL CLEAR text missing (got $WHITE white pixels in rows 116-123)"
note "win overlay ok — panel $BLACK black, $CYAN cyan edge, LEVEL CLEAR drawn"
ok

# --- 4) the playing frame: HUD text + the imported level on screen ----------
# Frame 30: act 1 firing in place — HUD drawn, level scrolled 30 px,
# bullets in flight, ship in its fire pose.
"$CLI" run "$GAME" --frames 30 --input "$SCRIPT" --capture "$OUT/play.ppm" \
    --quiet > /dev/null 2>&1
[ "$(head -c 2 "$OUT/play.ppm")" = "P6" ] || fail "play capture is not a P6 PPM"
ok

READ=$(tail -c $((480 * 270 * 3)) "$OUT/play.ppm" | od -An -v -tu1 | awk '
    { for (i = 1; i <= NF; i++) { c[n % 3] = $i; n++
        if (n % 3 == 0) {
            p = int(n / 3) - 1; x = p % 480; y = int(p / 480)
            if (y <= 23 && c[0] == 255 && c[1] == 255 && c[2] == 255) hud++
            if (c[0] == 16 && c[1] == 24 && c[2] == 115) sky++
            if (y >= 60 && c[0] == 255 && c[1] == 255 && c[2] == 255) stars++
            if (c[0] == 255 && c[1] == 255 && c[2] == 0) bullets++
            if (y >= 230 && y <= 260 && c[0] == 82 && c[1] == 202 &&
                c[2] == 255) ship++
        } } }
    END { printf "%d %d %d %d %d\n", hud + 0, sky + 0, stars + 0,
          bullets + 0, ship + 0 }')
set -- $READ
HUD=$1; SKY=$2; STARS=$3; BULLETS=$4; SHIP=$5

[ "$HUD" -ge 200 ] ||
    fail "HUD text missing (got $HUD white pixels in rows 0-23)"
[ "$SKY" -ge 20000 ] ||
    fail "level background missing (got $SKY deep-space pixels)"
[ "$STARS" -ge 100 ] ||
    fail "level tiles missing (got $STARS white pixels below y=60)"
[ "$BULLETS" -ge 20 ] ||
    fail "bullets missing (got $BULLETS yellow pixels)"
[ "$SHIP" -ge 40 ] ||
    fail "ship sprite missing (got $SHIP hull pixels at the ship row)"
note "playing frame ok — HUD $HUD, sky $SKY, stars $STARS, bullets $BULLETS, ship $SHIP"
ok

echo "test-asteroid_run: ok ($CHECKS checks, Wave 5 card: Game #1 golden 10)"
