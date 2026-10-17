#!/bin/sh
# ARCADE-1 — milestone 28, AI SDK v0.1: headless `arcade1 run` test.
#
# Exercises the developer CLI end-to-end and asserts the SDK's determinism
# contract (AI_NATIVE_DEVELOPMENT.md): the same image + the same input script
# always yield the same per-frame hashes and the same pixels, and the runner is
# faithful to the reference VM's golden regression.
#
# Checks:
#   1. determinism       — two identical runs produce byte-identical output
#   2. golden faithfulness— per-frame hashes == tests/golden/color.txt (pass 1)
#   3. frame capture      — --capture writes a valid full-frame P6 PPM (480x270)
#   4. scripted input     — a button script changes a game's state hash, and the
#                           same script is itself deterministic
#   5. exit codes         — 0 success, 1 bad image/file, 2 usage error
#   6. named variables    — --syms/--vars read a game's RAM globals by name,
#                           --var-every traces them, bad names/symbols error out
#   7. record/replay       — --record writes the applied buttons in the script
#                           format (normalized); a recording replays bit-identically
#
# usage: test_run.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_run.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}
GOLD=${3:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-run: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

COLOR=$ROM/color_demo.arc1
BREAK=$ROM/breakout.arc1

[ -x "$CLI" ]   || fail "CLI not built at $CLI"
[ -f "$COLOR" ] || fail "missing $COLOR"
[ -f "$BREAK" ] || fail "missing $BREAK"

# --- 1) determinism --------------------------------------------------------
"$CLI" run "$COLOR" --frames 120 > "$OUT/r1.txt"
"$CLI" run "$COLOR" --frames 120 > "$OUT/r2.txt"
if ! cmp -s "$OUT/r1.txt" "$OUT/r2.txt"; then
    diff "$OUT/r1.txt" "$OUT/r2.txt" | head -5 >&2
    fail "two identical runs produced different output"
fi
note "determinism ok — two runs byte-identical"

# --- 2) golden faithfulness ------------------------------------------------
# The runner's per-frame hash lines must match tests/golden/color.txt's first
# pass (frames 0..119) exactly — proving `arcade1 run` reproduces the reference
# VM's golden hashes.
grep '^frame ' "$OUT/r1.txt" > "$OUT/frames.txt"
head -120 "$GOLD/color.txt" > "$OUT/want.txt"
if ! diff -u "$OUT/want.txt" "$OUT/frames.txt" > "$OUT/d.txt"; then
    head -10 "$OUT/d.txt" >&2
    fail "runner per-frame hashes differ from tests/golden/color.txt"
fi
note "golden faithfulness ok — $(wc -l < "$OUT/frames.txt") frame hashes match tests/golden/color.txt"

# --- 3) frame capture ------------------------------------------------------
# PPM P6 header for the pinned 480x270 frame (machine.h, M25): "P6\n480 270\n255\n"
"$CLI" run "$COLOR" --frames 10 --capture "$OUT/f.ppm" --quiet > "$OUT/c.txt"
PAYLOAD=$((480 * 270 * 3))                 # RGB888 pixels
SIZE=$(wc -c < "$OUT/f.ppm" | tr -d ' ')
HDR=$((SIZE - PAYLOAD))
[ "$(head -c 2 "$OUT/f.ppm")" = "P6" ] || fail "capture is not a P6 PPM"
head -n 3 "$OUT/f.ppm" | grep -q '480 270' || fail "capture header is not 480x270"
[ "$HDR" -ge 10 ] && [ "$HDR" -le 24 ] || fail "capture header length $HDR looks wrong"
note "frame capture ok — $SIZE-byte P6 PPM ($HDR-byte header + $PAYLOAD-byte frame)"

# --- 4) scripted input -----------------------------------------------------
# breakout steers its paddle from the pad; holding "right" for 30 frames moves
# paddle_x (a RAM global), so the state hash must differ from a no-input run.
printf '# hold right for the whole run\n0 29 right\n' > "$OUT/inp.txt"
"$CLI" run "$BREAK" --frames 30 --quiet > "$OUT/n0.txt"
"$CLI" run "$BREAK" --frames 30 --quiet --input "$OUT/inp.txt" > "$OUT/n1.txt"
"$CLI" run "$BREAK" --frames 30 --quiet --input "$OUT/inp.txt" > "$OUT/n2.txt"
if ! cmp -s "$OUT/n1.txt" "$OUT/n2.txt"; then
    fail "the same input script was not deterministic"
fi
if cmp -s "$OUT/n0.txt" "$OUT/n1.txt"; then
    fail "scripted input did not change the run (no-input == with-input)"
fi
note "scripted input ok — button script changes state hash, reproducibly"

# --- 5) exit codes ---------------------------------------------------------
rc=0; "$CLI" run "$OUT/nope.arc1" --frames 1 2>/dev/null || rc=$?
[ "$rc" -eq 1 ] || fail "missing file should exit 1, got $rc"
rc=0; "$CLI" run "$COLOR" --bogus 2>/dev/null || rc=$?
[ "$rc" -eq 2 ] || fail "unknown option should exit 2, got $rc"
rc=0; "$CLI" 2>/dev/null || rc=$?
[ "$rc" -eq 2 ] || fail "no command should exit 2, got $rc"
rc=0; "$CLI" frobnicate 2>/dev/null || rc=$?
[ "$rc" -eq 2 ] || fail "unknown command should exit 2, got $rc"
note "exit codes ok — 0 / 1 / 2 as documented"

# --- 6) named game variables ------------------------------------------------
# --syms/--vars read a game's RAM globals by name from an `nm` dump (the same
# <game>.syms the golden regression uses). Use the committed dump if this ROM
# dir has one (tests/roms), else generate it from the freshly built ELF.
SYMS="$ROM/breakout.syms"
if [ ! -f "$SYMS" ] && [ -f "$ROM/breakout.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/breakout.elf" > "$OUT/breakout.syms"
    SYMS="$OUT/breakout.syms"
fi
if [ -f "$SYMS" ]; then
    "$CLI" run "$BREAK" --frames 30 --syms "$SYMS" --vars score,lives --quiet > "$OUT/v0.txt"
    grep -q '^var score = 0$' "$OUT/v0.txt" || fail "--vars: expected 'var score = 0'"
    grep -q '^var lives = 3$' "$OUT/v0.txt" || fail "--vars: expected 'var lives = 3'"
    "$CLI" run "$BREAK" --frames 30 --syms "$SYMS" --vars all --var-every 15 --quiet > "$OUT/v1.txt"
    grep -q '^vars *0 ' "$OUT/v1.txt" || fail "--var-every: no trace line at frame 0"
    grep -q 'score=' "$OUT/v1.txt" || fail "--var-every: trace does not include score"
    rc=0; "$CLI" run "$BREAK" --vars score --quiet 2>/dev/null || rc=$?
    [ "$rc" -eq 2 ] || fail "--vars without --syms should exit 2, got $rc"
    rc=0; "$CLI" run "$BREAK" --syms "$SYMS" --vars nosuch --quiet 2>/dev/null || rc=$?
    [ "$rc" -eq 1 ] || fail "unknown symbol should exit 1, got $rc"
    rc=0; "$CLI" run "$BREAK" --syms "$SYMS" --vars main --quiet 2>/dev/null || rc=$?
    [ "$rc" -eq 1 ] || fail "non-RAM symbol should exit 1, got $rc"
    note "named variables ok — --syms/--vars reads globals, trace + error exits"
else
    note "named variables skipped — no breakout.syms and no riscv nm to make one"
fi

# --- 7) record/replay --------------------------------------------------------
# --record run-length encodes the applied per-frame buttons into the input
# script format. Overlapping entries (later lines win) must normalize into
# canonical runs, an idle session records as "none", two recordings of the
# same session are byte-identical, and a recording fed back through --input
# must replay the original session bit-for-bit (same per-frame hashes).
printf '# overlapping entries: later line wins\n0 10 right\n5 29 a\n' > "$OUT/inp7.txt"
"$CLI" run "$BREAK" --frames 30 --input "$OUT/inp7.txt" --record "$OUT/rec1.txt" --quiet > /dev/null
grep -q '^0 4 right$' "$OUT/rec1.txt" || fail "record: expected '0 4 right'"
grep -q '^5 29 a$'    "$OUT/rec1.txt" || fail "record: expected '5 29 a'"
"$CLI" run "$BREAK" --frames 30 --record "$OUT/rec2.txt" --quiet > /dev/null
grep -q '^0 29 none$' "$OUT/rec2.txt" || fail "record: idle session should record 'none'"
"$CLI" run "$BREAK" --frames 30 --input "$OUT/inp7.txt" --record "$OUT/rec3.txt" --quiet > /dev/null
if ! cmp -s "$OUT/rec1.txt" "$OUT/rec3.txt"; then
    diff "$OUT/rec1.txt" "$OUT/rec3.txt" >&2
    fail "two recordings of the same session differ"
fi
"$CLI" run "$BREAK" --frames 30 --input "$OUT/inp7.txt" > "$OUT/h1.txt" 2>/dev/null
"$CLI" run "$BREAK" --frames 30 --input "$OUT/rec1.txt"   > "$OUT/h2.txt" 2>/dev/null
grep '^frame ' "$OUT/h1.txt" > "$OUT/h1f.txt"
grep '^frame ' "$OUT/h2.txt" > "$OUT/h2f.txt"
if ! cmp -s "$OUT/h1f.txt" "$OUT/h2f.txt"; then
    diff "$OUT/h1f.txt" "$OUT/h2f.txt" | head -5 >&2
    fail "replaying the recording changed the session's hashes"
fi
rc=0; "$CLI" run "$BREAK" --frames 10 --record "$OUT/nope/x.txt" 2>/dev/null || rc=$?
[ "$rc" -eq 1 ] || fail "unwritable record file should exit 1, got $rc"
note "record/replay ok — normalized recording replays bit-identically"

echo "test-run: arcade1 run ok (determinism, golden, capture, input, exit codes, variables, record)"
