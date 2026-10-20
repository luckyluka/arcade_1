#!/bin/sh
# ROADMAP_V4 card 4.2: golden authoring from the CLI.
#
# `arcade1 golden <game> --out FILE` writes the per-frame hash lines of a
# run — the format `arcade1 test --golden FILE` compares against, and the
# format the committed tests/golden files use. Acceptance (pinned):
#   1. the authored golden has frame lines only (no summary line);
#   2. it is byte-identical to the committed golden's prefix — the CLI
#      writes exactly the committed format;
#   3. `golden` then `test --golden` round-trips (exit 0);
#   4. a tampered golden exits 1 and names the first differing frame;
#   5. the documented grammar holds: non-`frame ` lines are ignored, and a
#      run shorter than the golden's first pass is reported, not a pass;
#   6. usage errors exit 2.
#
# Host-side only: the test writes to a temp dir, it never touches the
# committed goldens or ROMs.
#
# Usage: test_golden_cli.sh <arcade1-cli> <rom-dir> <golden-dir>
set -eu

CLI=${1:?usage: test_golden_cli.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROMDIR=${2:?usage: test_golden_cli.sh <arcade1-cli> <rom-dir> <golden-dir>}
GOLD=${3:?usage: test_golden_cli.sh <arcade1-cli> <rom-dir> <golden-dir>}

case "$CLI" in /*) ;; *) CLI="$(pwd)/$CLI" ;; esac

GAME=$ROMDIR/flappy.arc1
SCRIPT=$(dirname "$0")/inputs/flappy.txt
COMMITTED=$GOLD/flappy.txt

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-golden-cli: FAILED — $*" >&2; exit 1; }
CHECKS=0

[ -f "$GAME" ]      || fail "missing $GAME"
[ -f "$SCRIPT" ]    || fail "missing $SCRIPT"
[ -f "$COMMITTED" ] || fail "missing $COMMITTED"

FRAMES=60

# --- 1) the authored golden is frame lines only ---------------------------
rc=0
"$CLI" golden "$GAME" --input "$SCRIPT" --frames $FRAMES \
    --out "$OUT/g.txt" > "$OUT/g.log" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || fail "golden exit $rc — $(cat "$OUT/g.log")"
[ "$(wc -l < "$OUT/g.txt")" -eq "$FRAMES" ] || \
    fail "the golden has $(wc -l < "$OUT/g.txt") lines, want $FRAMES"
if grep -qv '^frame ' "$OUT/g.txt"; then
    fail "the golden carries a line that is not a frame line"
fi
CHECKS=$((CHECKS + 1))
echo "  ok: the authored golden is frame lines only ($FRAMES lines)"

# --- 2) it is byte-identical to the committed golden's prefix --------------
head -n "$FRAMES" "$COMMITTED" > "$OUT/prefix.txt"
cmp -s "$OUT/prefix.txt" "$OUT/g.txt" || {
    diff "$OUT/prefix.txt" "$OUT/g.txt" | head -5 >&2
    fail "the authored golden differs from the committed golden's prefix"
}
CHECKS=$((CHECKS + 1))
echo "  ok: byte-identical to the committed golden's first $FRAMES lines"

# --- 3) golden then test --golden round-trips ------------------------------
rc=0
"$CLI" test "$GAME" --input "$SCRIPT" --frames $FRAMES \
    --golden "$OUT/g.txt" > "$OUT/t.log" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || fail "test --golden exit $rc — $(cat "$OUT/t.log")"
CHECKS=$((CHECKS + 1))
echo "  ok: golden -> test --golden round-trips"

# --- 4) a tampered golden exits 1 and names the first differing frame -----
sed '12s/hash=0x[0-9A-F]*/hash=0xDEADBEEFDEADBEEF/' "$OUT/g.txt" \
    > "$OUT/bad.txt"
rc=0
"$CLI" test "$GAME" --input "$SCRIPT" --frames $FRAMES \
    --golden "$OUT/bad.txt" > "$OUT/bad.log" 2>&1 || rc=$?
[ "$rc" -eq 1 ] || fail "a tampered golden exits $rc, want 1"
grep -q 'frame line 12 differs' "$OUT/bad.log" || \
    fail "the report does not name the first differing frame: $(cat "$OUT/bad.log")"
CHECKS=$((CHECKS + 1))
echo "  ok: a tampered golden exits 1 and names the first differing frame"

# --- 5) the documented grammar -------------------------------------------
# 5a) lines that do not start with "frame " are ignored.
{ echo "# flappy session — 60 frames"; echo ""; cat "$OUT/g.txt"; } \
    > "$OUT/commented.txt"
rc=0
"$CLI" test "$GAME" --input "$SCRIPT" --frames $FRAMES \
    --golden "$OUT/commented.txt" > "$OUT/c.log" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || fail "a golden with comment lines exits $rc, want 0"
# 5b) a run shorter than the golden's first pass is reported, not a pass.
rc=0
"$CLI" test "$GAME" --input "$SCRIPT" --frames 10 \
    --golden "$OUT/g.txt" > "$OUT/s.log" 2>&1 || rc=$?
[ "$rc" -eq 1 ] || fail "a short run exits $rc, want 1"
grep -q 'of 60 golden frame lines match' "$OUT/s.log" || \
    fail "the short run is not reported: $(cat "$OUT/s.log")"
CHECKS=$((CHECKS + 1))
echo "  ok: non-frame lines ignored; a short run is reported, not a pass"

# --- 6) usage errors exit 2 ----------------------------------------------
rc=0; "$CLI" golden "$GAME" --input "$SCRIPT" >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "golden without --out: exit $rc, want 2"
rc=0; "$CLI" golden "$GAME" --out "$OUT/x.txt" --quiet >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "golden --quiet: exit $rc, want 2"
CHECKS=$((CHECKS + 1))
echo "  ok: usage errors exit 2"

echo "test-golden-cli: ok ($CHECKS checks, card 4.2)"
