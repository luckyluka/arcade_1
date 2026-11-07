#!/bin/sh
# ARCADE-1 — play-window test: `arcade1 run --live` (ROADMAP_CREATION_FLOW §10,
# ROADMAP_V3 §4.5). The card's acceptance, pinned:
#
#   1. optional   — a build without SDL3 (arcade1_nosdl) prints a clean
#                   message and exits 3; every other command still works
#   2. rules      — --live and --input never drive the same run (they must
#                   not share mutable state); usage errors still exit 2
#   3. idle       — a live session (dummy video driver) runs the shared
#                   frame loop: identical per-frame hashes to a scripted
#                   run, and its --record output replays bit-identically
#                   through --input
#   4. playable   — REAL keys through XTEST (when a display is available):
#                   holding Right moves walkdemo's x (keyboard -> pad ->
#                   game), the recording carries the button runs, and the
#                   replay reproduces the live session's per-frame hashes
#                   AND its final x — the recorded session replays with
#                   identical per-frame hashes, exactly as the card says
#   5. R and ESC  — end an unbounded session leaving a complete script
#   6. audio      — the window queues the frame's samples through SDL3
#                   (card 8.2): a dummy device reports a plausible queue
#                   count and the hashes are unmoved; no device degrades
#                   to a warning + video-only session, exit 0 (only "no
#                   play window" may exit 3)
#
# usage: test_playwindow.sh <arcade1-cli> <arcade1-nosdl> <build-dir>
#
# NOTE: by hand — `make test-playwindow`. NOT part of `make check`: SDL3 and
# an X display are optional dependencies (§4.5), and section 4 pops a real
# window for ~5 s, sending real key events at it (no GUI automation tools are
# required: tests/xkey.c talks to XTEST directly, built on demand).

set -eu

CLI=${1:?usage: test_playwindow.sh <arcade1-cli> <arcade1-nosdl> <build-dir>}
NOSDL=${2:?}
BUILD=${3:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-playwindow: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }
CHECKS=0
ok() { CHECKS=$((CHECKS + 1)); }

GAME=$BUILD/walkdemo.arc1
[ -x "$CLI" ]   || fail "CLI not built at $CLI"
[ -x "$NOSDL" ] || fail "no-SDL CLI not built at $NOSDL"
[ -f "$GAME" ]  || fail "missing $GAME (make experimental)"

# --- 1) absence: built without SDL3 --------------------------------------
rc=0; "$NOSDL" run "$GAME" --live > "$OUT/abs.out" 2> "$OUT/abs.err" || rc=$?
[ "$rc" -eq 3 ] || fail "no-SDL --live should exit 3, got $rc"
grep -q "no play window" "$OUT/abs.err" ||
    fail "absence message should say 'no play window': $(cat "$OUT/abs.err")"
grep -q "SDL3" "$OUT/abs.err" ||
    fail "absence message should name SDL3: $(cat "$OUT/abs.err")"
ok
note "absence ok — no-SDL build: clean message, exit 3"

# The rest of the CLI is intact in that build.
"$NOSDL" run "$GAME" --frames 5 --quiet > /dev/null ||
    fail "no-SDL CLI must still run headlessly"
ok
rc=0; "$NOSDL" run "$OUT/nope.arc1" --live 2>/dev/null || rc=$?
[ "$rc" -eq 1 ] || fail "a missing image should exit 1 before absence, got $rc"
ok
note "absence ok — headless runs intact; content errors precede window errors"

# --- 2) argument rules ----------------------------------------------------
rc=0; "$CLI" run "$GAME" --live --input "$OUT/none.txt" 2> "$OUT/mix.err" || rc=$?
[ "$rc" -eq 2 ] || fail "--live with --input should exit 2, got $rc"
grep -q "must not drive the same run" "$OUT/mix.err" ||
    fail "mix error should explain the rule: $(cat "$OUT/mix.err")"
ok
rc=0; "$CLI" run --live 2>/dev/null || rc=$?
[ "$rc" -eq 2 ] || fail "usage (no image) should still exit 2, got $rc"
ok
note "rules ok — --live/--input mutually exclusive, usage unchanged"

# --- 3) idle live session (headless: dummy video driver) ------------------
# Same machine, same frames, different input source: the live loop must be
# the scripted loop with live keys (here: none) on top.
SDL_VIDEODRIVER=dummy "$CLI" run "$GAME" --live --frames 60 \
    --record "$OUT/live_rec.txt" > "$OUT/live.txt" 2>/dev/null ||
    fail "dummy-driver --live run failed"
grep -q '^ran 60 frames' "$OUT/live.txt" ||
    fail "live run should report 'ran 60 frames': $(tail -2 "$OUT/live.txt")"
grep -q "^recorded live session -> $OUT/live_rec.txt$" "$OUT/live.txt" ||
    fail "live run should report the recording path"
ok
grep -q '^# arcade1 run --live' "$OUT/live_rec.txt" ||
    fail "live recording should carry the --live header"
grep -q '^0 59 none$' "$OUT/live_rec.txt" ||
    fail "an idle session should record '0 59 none': $(cat "$OUT/live_rec.txt")"
ok
"$CLI" run "$GAME" --frames 60 > "$OUT/idle.txt" 2>/dev/null
grep '^frame ' "$OUT/live.txt"  > "$OUT/live.f"
grep '^frame ' "$OUT/idle.txt" > "$OUT/idle.f"
cmp -s "$OUT/live.f" "$OUT/idle.f" ||
    fail "live (idle keys) and scripted (no input) per-frame hashes differ"
ok
"$CLI" run "$GAME" --frames 60 --input "$OUT/live_rec.txt" > "$OUT/re.txt" 2>/dev/null
grep '^frame ' "$OUT/re.txt" > "$OUT/re.f"
cmp -s "$OUT/live.f" "$OUT/re.f" ||
    fail "replaying the idle live recording changed the session's hashes"
ok
note "idle ok — live ≡ scripted loop; recording replays bit-identically"

# --- 4) real keys: keyboard -> pad -> game -> script -> identical replay ---
# Needs: a display, XTEST, libXtst (xkey declares its prototypes), and an
# nm dump for --vars x. Any missing piece = a noted skip, not a failure.
SYMS=$BUILD/walkdemo.syms
if [ ! -f "$SYMS" ] && [ -f "$BUILD/walkdemo.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$BUILD/walkdemo.elf" > "$OUT/walkdemo.syms"
    SYMS="$OUT/walkdemo.syms"
fi
[ -f "$SYMS" ] || SYMS=

XKEY=
if [ -n "${DISPLAY:-}" ] && [ -n "$SYMS" ] &&
   command -v pkg-config >/dev/null 2>&1 && pkg-config --exists x11; then
    XTST=$(ls /usr/lib/*/libXtst.so.* /usr/local/lib/libXtst.so.* \
           2>/dev/null | head -1 || true)
    if [ -n "$XTST" ] &&
       "${CC:-cc}" -O1 -o "$OUT/xkey" "$(dirname "$0")/xkey.c" \
            $(pkg-config --cflags x11) $(pkg-config --libs x11) "$XTST" \
            2> "$OUT/xkey.build.err"; then
        XKEY=$OUT/xkey
    fi
fi

if [ -z "$XKEY" ]; then
    note "key session SKIPPED: needs DISPLAY, --vars-capable symbols, X11 +"
    note "  libXtst dev (xkey); section 3 already pinned the loop/record/replay"
else
    # 4a) hold Right (walkdemo walks: x += SPEED per frame), tap A, hold Left.
    # A beat between sessions: the WM refocuses whatever it likes when the
    # previous window closes, and xkey must not race that.
    "$CLI" run "$GAME" --frames 240 --syms "$SYMS" --vars x \
        > "$OUT/x_idle.txt" 2>/dev/null
    XIDLE=$(grep '^var x = ' "$OUT/x_idle.txt" | sed 's/.*= //')
    sleep 1
    timeout 30 "$CLI" run "$GAME" --live --frames 240 --record "$OUT/x_rec.txt" \
        --syms "$SYMS" --vars x > "$OUT/x_live.txt" 2>/dev/null &
    pid=$!
    "$XKEY" ARCADE-1 arcade1 Right:600 z:120 Left:400 > "$OUT/xkey.out" 2> "$OUT/xkey.err" ||
        fail "xkey: $(cat "$OUT/xkey.err")"
    rc=0; wait "$pid" || rc=$?
    [ "$rc" -eq 0 ] || fail "live X session exited $rc: $(cat "$OUT/x_live.txt")"
    ok

    grep -qE '^[0-9]+ [0-9]+ right' "$OUT/x_rec.txt" ||
        fail "recording should carry a 'right' run: $(cat "$OUT/x_rec.txt")"
    grep -qE '^[0-9]+ [0-9]+ left' "$OUT/x_rec.txt" ||
        fail "recording should carry a 'left' run: $(cat "$OUT/x_rec.txt")"
    ok
    XLIVE=$(grep '^var x = ' "$OUT/x_live.txt" | sed 's/.*= //')
    [ -n "$XLIVE" ] && [ "$XLIVE" != "$XIDLE" ] ||
        fail "holding Right/Left did not move walkdemo's x (still $XIDLE)"
    ok
    timeout 60 "$CLI" run "$GAME" --frames 240 --input "$OUT/x_rec.txt" \
        --syms "$SYMS" --vars x > "$OUT/x_re.txt" 2>/dev/null
    grep '^frame ' "$OUT/x_live.txt" > "$OUT/x_live.f"
    grep '^frame ' "$OUT/x_re.txt"   > "$OUT/x_re.f"
    cmp -s "$OUT/x_live.f" "$OUT/x_re.f" ||
        fail "replaying the keyed session changed its per-frame hashes"
    grep -q "^var x = $XLIVE\$" "$OUT/x_re.txt" ||
        fail "replay's final x ($XLIVE) does not match the live session's"
    ok
    note "keys ok — Right/Left moved x ($XIDLE -> $XLIVE); keyed session"

    # 4b) R ends an unbounded session writing a complete script.
    sleep 1
    timeout 30 "$CLI" run "$GAME" --live --record "$OUT/r_rec.txt" \
        > "$OUT/r_live.txt" 2>/dev/null &
    pid=$!
    "$XKEY" ARCADE-1 arcade1 r:150 > /dev/null 2> "$OUT/xkey.err" ||
        fail "xkey (R): $(cat "$OUT/xkey.err")"
    rc=0; wait "$pid" || rc=$?
    [ "$rc" -eq 0 ] || fail "R should end the session with exit 0, got $rc"
    N=$(sed -n 's/^ran \([0-9]*\) frames.*/\1/p' "$OUT/r_live.txt")
    [ -n "$N" ] && [ "$N" -ge 10 ] ||
        fail "R-ended session should report a plausible frame count, got '$N'"
    timeout 60 "$CLI" run "$GAME" --frames "$N" --input "$OUT/r_rec.txt" \
        > "$OUT/r_re.txt" 2>/dev/null
    grep '^frame ' "$OUT/r_live.txt" > "$OUT/r_live.f"
    grep '^frame ' "$OUT/r_re.txt"   > "$OUT/r_re.f"
    cmp -s "$OUT/r_live.f" "$OUT/r_re.f" ||
        fail "R's recording does not replay the session ($N frames)"
    ok
    note "keys ok — R ended the session ($N frames), script replays identically"

    # 4c) ESC quits, also leaving a complete script.
    sleep 1
    timeout 30 "$CLI" run "$GAME" --live --record "$OUT/e_rec.txt" \
        > "$OUT/e_live.txt" 2>/dev/null &
    pid=$!
    "$XKEY" ARCADE-1 arcade1 Escape:150 > /dev/null 2> "$OUT/xkey.err" ||
        fail "xkey (ESC): $(cat "$OUT/xkey.err")"
    rc=0; wait "$pid" || rc=$?
    [ "$rc" -eq 0 ] || fail "ESC should quit with exit 0, got $rc"
    M=$(sed -n 's/^ran \([0-9]*\) frames.*/\1/p' "$OUT/e_live.txt")
    [ -n "$M" ] && [ "$M" -ge 10 ] ||
        fail "ESC-ended session should report a plausible frame count, got '$M'"
    grep -q '^[0-9]* [0-9]*' "$OUT/e_rec.txt" ||
        fail "ESC left no runs in the recording"
    ok
    note "keys ok — ESC quit cleanly, recording complete"
fi

# --- 6) audio: the window plays the frame's samples (card 8.2) -------------
# SDL_AUDIODRIVER=dummy makes this headless-safe: the dummy device opens
# without hardware and drains at the sample rate. Acceptance: the session
# ends with a summary line and a plausible queue count, and its per-frame
# hashes still equal the scripted run's — audio is presentation; it moves
# nothing. A named-bad driver must degrade to a warning and a VIDEO-ONLY
# session with exit 0: only "no play window" may exit 3.
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$CLI" run "$GAME" --live \
    --frames 60 > "$OUT/aud.txt" 2> "$OUT/aud.err" ||
    fail "dummy-driver --live run with audio failed"
grep -q '^ran 60 frames' "$OUT/aud.txt" ||
    fail "the audio run should report 'ran 60 frames': $(tail -2 "$OUT/aud.txt")"
AUDIO_N=$(sed -n 's/^arcade1 run --live: audio: \([0-9]*\) frames queued.*/\1/p' \
    "$OUT/aud.err")
[ -n "$AUDIO_N" ] && [ "$AUDIO_N" -ge 1 ] && [ "$AUDIO_N" -le 60 ] ||
    fail "audio summary missing or implausible: '$AUDIO_N' ($(cat "$OUT/aud.err"))"
grep '^frame ' "$OUT/aud.txt" > "$OUT/aud.f"
cmp -s "$OUT/aud.f" "$OUT/idle.f" ||
    fail "playing audio changed the live run's per-frame hashes"
ok
note "audio ok — 60-frame session queued $AUDIO_N frame(s) of sound; hashes unmoved"

rc=0; SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=nosuchdriver "$CLI" run "$GAME" \
    --live --frames 10 > "$OUT/noaud.txt" 2> "$OUT/noaud.err" || rc=$?
[ "$rc" -eq 0 ] ||
    fail "no audio device should NOT exit $rc (video-only means exit 0)"
grep -q 'playing video only' "$OUT/noaud.err" ||
    fail "no-audio message should say 'playing video only': $(cat "$OUT/noaud.err")"
grep -q '^ran 10 frames' "$OUT/noaud.txt" ||
    fail "the video-only session did not run: $(tail -2 "$OUT/noaud.txt")"
ok
note "audio ok — no device degrades to a warning + video-only, exit 0"

echo "test-playwindow: ok ($CHECKS checks, Wave 6 card: play window --live)"
