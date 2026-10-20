#!/bin/sh
# ROADMAP_V3 4.1 (cards 0.1 + 0.3 + 0.4's tests): the F9 experiment as a
# permanent test. tests/golden/conditions.txt must be real: it parses, the
# CLI reproduces the recorded runs from it, a bare run teaches where the
# conditions live, a tampered golden fails loudly, and partial coverage
# fails (it used to exit 0 silently).
#
# Every run works on COPIES in $OUT — nothing under tests/ is ever written
# (the tamper case edits only its own copy).
#
# Usage: test_golden_conditions.sh <arcade1-cli> <rom-dir> <golden-dir>
set -eu

CLI=${1:?usage: test_golden_conditions.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROMDIR=${2:?usage: test_golden_conditions.sh <arcade1-cli> <rom-dir> <golden-dir>}
GOLD=${3:?usage: test_golden_conditions.sh <arcade1-cli> <rom-dir> <golden-dir>}

ROOT=$(cd "$(dirname "$0")/.." && pwd)
case "$CLI"   in /*) ;; *) CLI="$(pwd)/$CLI"       ;; esac
case "$ROMDIR" in /*) ;; *) ROMDIR="$(pwd)/$ROMDIR" ;; esac
case "$GOLD"  in /*) ;; *) GOLD="$(pwd)/$GOLD"     ;; esac

MANIFEST="$GOLD/conditions.txt"
INPUT="$ROOT/docs/m29/scripts/restart.txt"

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-golden-conditions: FAILED — $*" >&2; exit 1; }
CHECKS=0
[ -f "$MANIFEST" ] || fail "missing $MANIFEST"
[ -f "$INPUT" ]    || fail "missing $INPUT"

# --- case 1: manifest sanity — all 10 games, first_frames within the file --
set -- $(awk '!/^#/ && NF {print $1}' "$MANIFEST")
[ $# -eq 10 ] || fail "manifest lists $# games, want 10: $*"
CHECKS=$((CHECKS + 1))
echo "  ok: manifest lists 10 games"

# --- case 7 (same walk): repro vocabulary + rom paths exist ----------------
while read -r game frames rom input repro; do
    case "$game" in \#*|'') continue ;; esac
    flines=$(grep -c '^frame ' "$GOLD/$game.txt" || true)
    [ -n "$flines" ] && [ "$flines" -gt 0 ] || fail "$game: no frame lines in golden"
    [ "$frames" -le "$flines" ] || \
        fail "$game: first_frames $frames exceeds the golden's $flines frame lines"
    case "$repro" in
        cli|harness) ;;
        *) fail "$game: repro '$repro' not in {cli, harness}" ;;
    esac
    [ -f "$ROOT/$rom" ] || fail "$game: rom $rom does not exist"
done < "$MANIFEST"
CHECKS=$((CHECKS + 3))
echo "  ok: first_frames <= frame-line count (10 files)"
echo "  ok: every repro in {cli, harness}"
echo "  ok: every rom path exists"

# manifest-driven fields for the games we replay on the CLI
mframes=$(awk '$1=="m29game"{print $2}' "$MANIFEST")
mrom=$(awk '$1=="m29game"{print $3}' "$MANIFEST")
cframes=$(awk '$1=="color"{print $2}' "$MANIFEST")
crom=$(awk '$1=="color"{print $3}' "$MANIFEST")
aframes=$(awk '$1=="asteroid_run"{print $2}' "$MANIFEST")
arom=$(awk '$1=="asteroid_run"{print $3}' "$MANIFEST")
ainput=$(awk '$1=="asteroid_run"{print $4}' "$MANIFEST")

stage() {   # stage <rom-field> <golden-name> -> $OUT/rom.arc1 + $OUT/golden.txt
    cp "$ROOT/$1" "$OUT/rom.arc1"
    cp "$GOLD/$2" "$OUT/golden.txt"
}

# --- case 2: manifest conditions reproduce the golden (m29game, input) -----
stage "$mrom" "m29game.txt"
rc=0
out=$("$CLI" test "$OUT/rom.arc1" --golden "$OUT/golden.txt" \
        --frames "$mframes" --input "$INPUT" 2>&1) || rc=$?
[ "$rc" -eq 0 ] || fail "m29game with conditions: exit $rc — $out"
echo "$out" | grep -q "$mframes frame lines match" || \
    fail "m29game: no '$mframes frame lines match' — $out"
CHECKS=$((CHECKS + 1))
echo "  ok: m29game + manifest conditions -> exit 0"

# --- case 3: bare run fails AND points at conditions.txt (F9 + card 0.2) ---
rc=0
out=$("$CLI" test "$OUT/rom.arc1" --golden "$OUT/golden.txt" \
        --frames "$mframes" 2>&1) || rc=$?
[ "$rc" -eq 1 ] || fail "bare m29game: exit $rc, want 1 — $out"
echo "$out" | grep -q "conditions.txt" || \
    fail "bare run lacks the conditions.txt hint — $out"
CHECKS=$((CHECKS + 1))
echo "  ok: bare m29game -> exit 1 + conditions.txt hint"

# --- case 4: a tampered golden copy fails loudly ---------------------------
sed '0,/^frame/{s/hash=0x[0-9A-Fa-f]*/hash=0x0BAD0BAD0BAD0BAD/}' \
    "$GOLD/m29game.txt" > "$OUT/golden.txt"
rc=0
out=$("$CLI" test "$OUT/rom.arc1" --golden "$OUT/golden.txt" \
        --frames "$mframes" --input "$INPUT" 2>&1) || rc=$?
[ "$rc" -eq 1 ] || fail "tampered golden: exit $rc, want 1 — $out"
echo "$out" | grep -q "frame line [0-9]* differs" || \
    fail "tampered golden: no 'frame line … differs' — $out"
CHECKS=$((CHECKS + 1))
echo "  ok: tampered golden -> exit 1 + 'frame line … differs'"

# --- case 5: partial coverage fails (was the silent F9 trap) ---------------
stage "$mrom" "m29game.txt"
rc=0
out=$("$CLI" test "$OUT/rom.arc1" --golden "$OUT/golden.txt" \
        --frames 100 --input "$INPUT" 2>&1) || rc=$?
[ "$rc" -eq 1 ] || fail "partial run: exit $rc, want 1 — $out"
echo "$out" | grep -q "100 of $mframes" || \
    fail "partial run: no '100 of $mframes' — $out"
CHECKS=$((CHECKS + 1))
echo "  ok: --frames 100 -> exit 1 + '100 of $mframes'"

# --- case 6: a no-input cli entry reproduces (color, 120 frames) -----------
stage "$crom" "color.txt"
rc=0
out=$("$CLI" test "$OUT/rom.arc1" --golden "$OUT/golden.txt" \
        --frames "$cframes" 2>&1) || rc=$?
[ "$rc" -eq 0 ] || fail "color with conditions: exit $rc — $out"
echo "$out" | grep -q "$cframes frame lines match" || \
    fail "color: no '$cframes frame lines match' — $out"
CHECKS=$((CHECKS + 1))
echo "  ok: color (no input) -> exit 0"

# --- case 8: asteroid_run + its committed input reproduce (Wave 5) ---------
case "$ainput" in
    /*) ain="$ainput" ;;
    *)  ain="$ROOT/$ainput" ;;
esac
[ -f "$ain" ] || fail "asteroid_run input $ainput does not exist"
stage "$arom" "asteroid_run.txt"
rc=0
out=$("$CLI" test "$OUT/rom.arc1" --golden "$OUT/golden.txt" \
        --frames "$aframes" --input "$ain" 2>&1) || rc=$?
[ "$rc" -eq 0 ] || fail "asteroid_run with conditions: exit $rc — $out"
echo "$out" | grep -q "$aframes frame lines match" || \
    fail "asteroid_run: no '$aframes frame lines match' — $out"
CHECKS=$((CHECKS + 2))
echo "  ok: asteroid_run input path exists"
echo "  ok: asteroid_run + manifest conditions -> exit 0"

echo "test-golden-conditions: ok ($CHECKS checks, conditions manifest enforced)"
