#!/bin/sh
# Wave B card B1 (ROADMAP_V3 §5): `arcade1 inspect` structured last-frame
# report. Acceptance (pinned):
#   1. on tests/roms/m29game.arc1 every key is present (FRAME, PC,
#      INSTRET_FRAME, STRAY, VIDEO_CMDS, VIDEO_PIXELS_WRITTEN,
#      VIDEO_OOB_DRAWS, ROM_USED, VAR);
#   2. values agree with `arcade1 run --strict` for the same run;
#   3. --json parses with `python3 -m json.tool`;
#   4. the register dump appears only with --regs;
#   5. exit 0 ok, 2 usage.
# Host-side only: the test reads reports, it never writes a golden/rom
# (older goldens unchanged is asserted by the repo's standing git gate).
#
# Usage: test_inspect.sh <arcade1-cli> <rom-dir> <golden-dir>
set -eu

CLI=${1:?usage: test_inspect.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROMDIR=${2:?usage: test_inspect.sh <arcade1-cli> <rom-dir> <golden-dir>}
GOLD=${3:?usage: test_inspect.sh <arcade1-cli> <rom-dir> <golden-dir>}

ROOT=$(cd "$(dirname "$0")/.." && pwd)
case "$CLI" in /*) ;; *) CLI="$(pwd)/$CLI" ;; esac

M29=$ROOT/tests/roms/m29game.arc1
INPUT=$ROOT/docs/m29/scripts/restart.txt

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-inspect: FAILED — $*" >&2; exit 1; }
CHECKS=0
[ -f "$M29" ]    || fail "missing $M29"
[ -f "$INPUT" ]  || fail "missing $INPUT"

# --- 1) every key present on m29game (canonical conditions) ----------------
rc=0
"$CLI" inspect "$M29" --frames 600 --input "$INPUT" \
    --vars score,lives,state > "$OUT/r.txt" 2>/dev/null || rc=$?
[ "$rc" -eq 0 ] || fail "inspect exit $rc, want 0"

grep -Eq '^FRAME 600$'                  "$OUT/r.txt" || fail "FRAME key"
grep -Eq '^PC 0x[0-9A-F]{8}$'             "$OUT/r.txt" || fail "PC key"
grep -Eq '^INSTRET_FRAME [1-9][0-9]*$'    "$OUT/r.txt" || fail "INSTRET_FRAME key"
grep -Eq '^STRAY [0-9]+$'                 "$OUT/r.txt" || fail "STRAY key"
grep -Eq '^VIDEO_CMDS CLEAR=[0-9]+ FILL=[0-9]+ SPRITE=[0-9]+ TILEMAP=[0-9]+ SPRITE4=[0-9]+ TILEMAP4=[0-9]+ PRESENT=[0-9]+$' \
                                          "$OUT/r.txt" || fail "VIDEO_CMDS key"
grep -Eq '^VIDEO_PIXELS_WRITTEN [0-9]+$'  "$OUT/r.txt" || fail "VIDEO_PIXELS_WRITTEN key"
grep -Eq '^VIDEO_OOB_DRAWS [0-9]+$'       "$OUT/r.txt" || fail "VIDEO_OOB_DRAWS key"
grep -Eq '^ROM_USED [0-9]+ / 262144$'     "$OUT/r.txt" || fail "ROM_USED key"
for v in score lives state; do
    grep -Eq "^VAR $v -?[0-9]+$" "$OUT/r.txt" || fail "VAR $v line"
done
# registers are NOT in the default output (they moved behind --regs)
grep -Eq '^ *x[0-9]+ = 0x' "$OUT/r.txt" && \
    fail "register dump leaked into the default report"
CHECKS=$((CHECKS + 1))
echo "  ok: every key present on m29game (600 frames + input + vars)"

# --- 4) the register dump appears only with --regs -------------------------
rc=0
"$CLI" inspect "$M29" --frames 60 --regs > "$OUT/reg.txt" 2>/dev/null || rc=$?
[ "$rc" -eq 0 ] || fail "inspect --regs exit $rc"
grep -Eq '^FRAME 60$' "$OUT/reg.txt"      || fail "FRAME 60 with --regs"
grep -Eq '^  x31 = 0x[0-9A-F]{8}$' "$OUT/reg.txt" || fail "x31 with --regs"
CHECKS=$((CHECKS + 1))
echo "  ok: register dump only with --regs"

# --- 2) values agree with `arcade1 run --strict` for the same run ----------
rc=0
"$CLI" run "$M29" --frames 600 --input "$INPUT" --strict --quiet \
    >/dev/null 2>"$OUT/strict.err" || rc=$?
[ "$rc" -eq 0 ] || fail "run --strict exit $rc — $(cat "$OUT/strict.err")"
oob=$(awk '$1=="VIDEO_OOB_DRAWS"{print $2}' "$OUT/r.txt")
[ "$oob" = 0 ] || fail "inspect reports VIDEO_OOB_DRAWS=$oob, --strict passed"
CHECKS=$((CHECKS + 1))
echo "  ok: VIDEO_OOB_DRAWS=0 agrees with run --strict exit 0"

# --- 3) --json parses with python3 -m json.tool ----------------------------
rc=0
"$CLI" inspect "$M29" --frames 60 --vars score --json \
    > "$OUT/i.json" 2>/dev/null || rc=$?
[ "$rc" -eq 0 ] || fail "inspect --json exit $rc"
python3 -m json.tool < "$OUT/i.json" >/dev/null 2>&1 || \
    fail "--json does not parse: $(cat "$OUT/i.json")"
for k in FRAME PC INSTRET_FRAME STRAY VIDEO_CMDS VIDEO_PIXELS_WRITTEN \
         VIDEO_OOB_DRAWS ROM_USED VAR; do
    grep -q "\"$k\"" "$OUT/i.json" || fail "--json missing key $k"
done
CHECKS=$((CHECKS + 1))
echo "  ok: --json parses with python3 -m json.tool, same keys"

# --- 5) exit 0 ok, 2 usage -------------------------------------------------
rc=0; "$CLI" inspect >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "no arguments: exit $rc, want 2"
rc=0; "$CLI" inspect "$M29" --bogus >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "unknown option: exit $rc, want 2"
rc=0; "$CLI" inspect "$M29" --frames 0 >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "--frames 0: exit $rc, want 2"
CHECKS=$((CHECKS + 1))
echo "  ok: usage errors exit 2 (no args, unknown option, --frames 0)"

echo "test-inspect: ok ($CHECKS checks, card B1)"
