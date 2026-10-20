#!/bin/sh
# ROADMAP_V4 card 4.1: `arcade1 info` — one source of truth for the
# machine's constants. Acceptance (pinned):
#   1. `arcade1 info` prints the machine's geometry, memory map, instruction
#      cap, tick rate and the embedded SDK headers, and exits 0;
#   2. `arcade1 info --json` parses with `python3 -m json.tool`;
#   3. the C test asserts every numeric field equals the macro the VM is
#      compiled with, and that the SDK header list equals the embedded set;
#   4. usage errors exit 2.
#
# Host-side only: the test reads reports, it never writes a golden/rom.
#
# Usage: test_info.sh <arcade1-cli> <test-info-binary>
set -eu

CLI=${1:?usage: test_info.sh <arcade1-cli> <test-info-binary>}
CBIN=${2:?usage: test_info.sh <arcade1-cli> <test-info-binary>}

case "$CLI" in /*) ;; *) CLI="$(pwd)/$CLI" ;; esac
case "$CBIN" in /*) ;; *) CBIN="$(pwd)/$CBIN" ;; esac

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-info: FAILED — $*" >&2; exit 1; }
CHECKS=0

# --- 1) the human-readable report ----------------------------------------
rc=0
"$CLI" info > "$OUT/info.txt" 2>"$OUT/info.err" || rc=$?
[ "$rc" -eq 0 ] || fail "info exit $rc, want 0 — $(cat "$OUT/info.err")"
grep -Eq '480x270 \(RGB565, 129600 pixels\)' "$OUT/info.txt" || \
    fail "no screen line"
grep -Eq '60 fps' "$OUT/info.txt"          || fail "no tick-rate line"
grep -Eq '1000000 per frame' "$OUT/info.txt" || fail "no instruction-cap line"
grep -Eq '0x00000000 +262144 bytes' "$OUT/info.txt" || fail "no ROM line"
grep -Eq '0x20000000 +65536 bytes'  "$OUT/info.txt" || fail "no RAM line"
grep -Eq '0x40000000 +4096 bytes'   "$OUT/info.txt" || fail "no MMIO line"
for h in arcade1.h gfx.h arcade1_map.h asset.h text.h; do
    grep -q "^  $h$" "$OUT/info.txt" || fail "no sdk header $h"
done
CHECKS=$((CHECKS + 1))
echo "  ok: info prints geometry, memory map, cap and the sdk headers"

# --- 2) the JSON parses ---------------------------------------------------
rc=0
"$CLI" info --json > "$OUT/info.json" 2>"$OUT/json.err" || rc=$?
[ "$rc" -eq 0 ] || fail "info --json exit $rc — $(cat "$OUT/json.err")"
python3 -m json.tool < "$OUT/info.json" > /dev/null 2>&1 || \
    fail "--json does not parse: $(cat "$OUT/info.json")"
CHECKS=$((CHECKS + 1))
echo "  ok: --json parses with python3 -m json.tool"

# --- 3) the numbers equal the macros, the header list the embedded set ----
rc=0
"$CBIN" "$OUT/info.json" > "$OUT/c.txt" 2>"$OUT/c.err" || rc=$?
[ "$rc" -eq 0 ] || fail "test_info exit $rc — $(cat "$OUT/c.err")"
grep -q "test-info: ok" "$OUT/c.txt" || fail "test_info did not report ok"
CHECKS=$((CHECKS + 1))
echo "  ok: every numeric field equals the macro; sdk headers == embedded set"

# --- 4) usage errors exit 2 ----------------------------------------------
rc=0; "$CLI" info --bogus >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "unknown option: exit $rc, want 2"
CHECKS=$((CHECKS + 1))
echo "  ok: usage errors exit 2"

echo "test-info: ok ($CHECKS checks, card 4.1)"
