#!/bin/sh
# ROADMAP_V4 card 4.4: colour truncation is visible, and invisible no more.
#
# `arcade1_rgb(r, g, b)` builds an RGB565 colour; a 24-bit literal in the
# colour register is cut to 16 bits SILENTLY (round 5 finding 1). The host
# now counts those writes. Acceptance (pinned):
#   1. a guest that writes 0xE8C0A0 reports VIDEO_COLOR_TRUNC >= 1 and
#      `run --strict` fails (exit 1) with a warning;
#   2. a clean guest reports 0 and passes --strict silently;
#   3. the diagnostic changes no pixel: the truncating probe and the probe
#      that writes the already-masked colours have the SAME fb hash;
#   4. the committed goldens and ROMs are untouched (the repo's standing
#      gate: `git diff --stat -- tests/golden tests/roms` is empty).
#
# Host-side only: the test writes to a temp dir.
#
# Usage: test_color_trunc.sh <arcade1-cli> <guest-build-dir> <rom-dir>
set -eu

CLI=${1:?usage: test_color_trunc.sh <arcade1-cli> <guest-build-dir> <rom-dir>}
GUEST=${2:?usage: test_color_trunc.sh <arcade1-cli> <guest-build-dir> <rom-dir>}
ROMDIR=${3:?usage: test_color_trunc.sh <arcade1-cli> <guest-build-dir> <rom-dir>}

case "$CLI" in /*) ;; *) CLI="$(pwd)/$CLI" ;; esac

PROBE=$GUEST/probe_trunc.bin
MASKED=$GUEST/probe_trunc_masked.bin
CLEAN=$ROMDIR/color_demo.arc1

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-color-trunc: FAILED — $*" >&2; exit 1; }
CHECKS=0

[ -f "$PROBE" ]  || fail "missing $PROBE"
[ -f "$MASKED" ] || fail "missing $MASKED"
[ -f "$CLEAN" ]  || fail "missing $CLEAN"

# --- 1) the truncating guest is counted, and --strict fails ---------------
rc=0
"$CLI" inspect "$PROBE" --frames 60 > "$OUT/i.txt" 2>/dev/null || rc=$?
[ "$rc" -eq 0 ] || fail "inspect exit $rc"
n=$(awk '$1=="VIDEO_COLOR_TRUNC"{print $2}' "$OUT/i.txt")
[ -n "$n" ] || fail "inspect does not report VIDEO_COLOR_TRUNC"
[ "$n" -ge 1 ] || fail "VIDEO_COLOR_TRUNC is $n, want >= 1"
rc=0
"$CLI" run "$PROBE" --frames 60 --strict > /dev/null 2>"$OUT/strict.err" || rc=$?
[ "$rc" -eq 1 ] || fail "run --strict exit $rc, want 1"
grep -q 'colour writes carried bits above 15' "$OUT/strict.err" || \
    fail "no truncation warning: $(cat "$OUT/strict.err")"
CHECKS=$((CHECKS + 1))
echo "  ok: the 24-bit literal is counted ($n writes) and --strict fails"

# --- 2) a clean guest is silent -------------------------------------------
rc=0
"$CLI" inspect "$CLEAN" --frames 120 > "$OUT/c.txt" 2>/dev/null || rc=$?
[ "$rc" -eq 0 ] || fail "inspect (clean) exit $rc"
n=$(awk '$1=="VIDEO_COLOR_TRUNC"{print $2}' "$OUT/c.txt")
[ "$n" = "0" ] || fail "a clean guest reports VIDEO_COLOR_TRUNC=$n, want 0"
rc=0
"$CLI" run "$CLEAN" --frames 120 --strict > /dev/null 2>"$OUT/c.err" || rc=$?
[ "$rc" -eq 0 ] || fail "run --strict (clean) exit $rc — $(cat "$OUT/c.err")"
[ -s "$OUT/c.err" ] && fail "a clean guest wrote to stderr: $(cat "$OUT/c.err")"
CHECKS=$((CHECKS + 1))
echo "  ok: a clean guest is silent and passes --strict"

# --- 3) the diagnostic changes no pixel -----------------------------------
"$CLI" run "$PROBE"  --frames 30 --quiet > "$OUT/tr.txt"  2>/dev/null
"$CLI" run "$MASKED" --frames 30 --quiet > "$OUT/mk.txt"  2>/dev/null
fb_tr=$(sed -n 's/.*fb hash=0x\([0-9A-F]*\).*/\1/p' "$OUT/tr.txt")
fb_mk=$(sed -n 's/.*fb hash=0x\([0-9A-F]*\).*/\1/p' "$OUT/mk.txt")
[ -n "$fb_tr" ] && [ "$fb_tr" = "$fb_mk" ] || \
    fail "the framebuffer changed: truncating fb=$fb_tr masked fb=$fb_mk"
n=$("$CLI" inspect "$MASKED" --frames 30 | awk '$1=="VIDEO_COLOR_TRUNC"{print $2}')
[ "$n" = "0" ] || fail "the masked probe still reports VIDEO_COLOR_TRUNC=$n"
CHECKS=$((CHECKS + 1))
echo "  ok: same framebuffer with and without the truncation (fb hash $fb_tr)"

echo "test-color-trunc: ok ($CHECKS checks, card 4.4)"
