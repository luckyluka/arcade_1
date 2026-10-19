#!/bin/sh
# Wave B card B2 (ROADMAP_V3 §5): command records and pixel provenance.
# Acceptance (pinned):
#   1. a test guest draws a sprite, then a FILL covering it
#      (tests/guests/probe_cmds.c, built by the Makefile);
#   2. --at on a covered pixel names the FILL;
#   3. --commands lists both in order with PCs inside the guest function's
#      symbol range (main's range, from the .elf via nm);
#   4. hashes of runs with and without --commands are identical;
#   5. older goldens unchanged (repo's standing git gate), make check green.
# Also pins: --json combinations parse and agree with the text report,
# malformed --at exits 2, CLEAR owns an untouched pixel, an off-screen
# pixel reports id=0 (never written).
#
# Usage: test_inspect_commands.sh <arcade1-cli> <probe.arc1> <probe.elf> <nm>
set -eu

CLI=${1:?usage: test_inspect_commands.sh <arcade1-cli> <probe.arc1> <probe.elf> <nm>}
ARC1=${2:?usage: test_inspect_commands.sh <arcade1-cli> <probe.arc1> <probe.elf> <nm>}
ELF=${3:?usage: test_inspect_commands.sh <arcade1-cli> <probe.arc1> <probe.elf> <nm>}
NM=${4:?usage: test_inspect_commands.sh <arcade1-cli> <probe.arc1> <probe.elf> <nm>}

case "$CLI" in /*) ;; *) CLI="$(pwd)/$CLI" ;; esac
case "$ARC1" in /*) ;; *) ARC1="$(pwd)/$ARC1" ;; esac
case "$ELF"  in /*) ;; *) ELF="$(pwd)/$ELF"   ;; esac

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-inspect-commands: FAILED — $*" >&2; exit 1; }
CHECKS=0
[ -f "$ARC1" ] || fail "missing $ARC1"
[ -f "$ELF" ]  || fail "missing $ELF"

# --- 1 + 3) --commands: sequential records, SPRITE before FILL, PCs in main
rc=0
"$CLI" inspect "$ARC1" --frames 3 --commands > "$OUT/cmds.txt" 2>/dev/null || rc=$?
[ "$rc" -eq 0 ] || fail "inspect --commands exit $rc, want 0"

# ids count 1,2,3,... in execution order; every line carries a pc=0x........
awk '$1 == "CMD" {
        if ($2 != ++n) exit 1
        if ($0 !~ /pc=0x[0-9A-F]{8}/) exit 1
     }
     END { if (n < 2) exit 1 }' "$OUT/cmds.txt" ||
    fail "CMD lines malformed or fewer than 2"

si=$(awk '$1=="CMD" && $3=="SPRITE" { print $2; exit }' "$OUT/cmds.txt")
fi=$(awk '$1=="CMD" && $3=="FILL"   { print $2; exit }' "$OUT/cmds.txt")
[ -n "$si" ] && [ -n "$fi" ] || fail "missing SPRITE or FILL record"
[ "$si" -lt "$fi" ] || fail "SPRITE (#$si) must be listed before FILL (#$fi)"

# The probe's geometry is fixed: CLEAR wipes 480*270, the 8x8 solid sprite
# writes 64 pixels, the 40x40 FILL writes 1600 — pins px_written per record.
grep -Eq '^CMD 1 CLEAR .* px=129600 ' "$OUT/cmds.txt" || fail "CLEAR px!=129600"
grep -Eq "^CMD $si SPRITE .* px=64 "  "$OUT/cmds.txt" || fail "SPRITE px!=64"
grep -Eq "^CMD $fi FILL .* px=1600 "  "$OUT/cmds.txt" || fail "FILL px!=1600"

# Every recorded PC must sit inside main's symbol range: [main, next symbol)
"$NM" "$ELF" > "$OUT/syms.txt" || fail "nm $ELF failed"
main_hex=$(awk '$3=="main" { print $1; exit }' "$OUT/syms.txt")
[ -n "$main_hex" ] || fail "no main symbol in $ELF"
main=$((0x$main_hex))
end=$((0xfffffffe))
while read -r a t n; do
    case "$a" in [0-9a-f][0-9a-f]*) ;; *) continue ;; esac
    av=$((0x$a))
    if [ "$av" -gt "$main" ] && [ "$av" -lt "$end" ]; then end=$av; fi
done < "$OUT/syms.txt"
[ "$end" -lt $((0xfffffffe)) ] || fail "no symbol above main"
for pc in $(awk '$1=="CMD" { for (i = 1; i <= NF; i++)
        if ($i ~ /^pc=/) { sub("pc=", "", $i); print $i } }' "$OUT/cmds.txt"); do
    pcv=$(($pc))                     # pc carries its own 0x prefix
    [ "$pcv" -ge "$main" ] && [ "$pcv" -lt "$end" ] ||
        fail "record pc $pc outside main's range [$main_hex..$end)"
done
CHECKS=$((CHECKS + 1))
echo "  ok: --commands: SPRITE before FILL, px pinned, PCs inside main"

# --- 2) --at names the last writer of a covered pixel -----------------------
rc=0
"$CLI" inspect "$ARC1" --frames 3 --at 12,12 > "$OUT/at.txt" 2>/dev/null || rc=$?
[ "$rc" -eq 0 ] || fail "inspect --at 12,12 exit $rc, want 0"
atid=$(awk '$1=="AT" && $2=="12,12" { sub("id=", "", $3); print $3 }' "$OUT/at.txt")
[ -n "$atid" ] && [ "$atid" -ge 1 ] || fail "--at 12,12 reports id=$atid"
awk -v id="$atid" '$1=="CMD" && $2==id && $3=="FILL" { found=1 }
                   END { exit !found }' "$OUT/at.txt" ||
    fail "--at 12,12 (sprite+FILL pixel) must name the FILL, got id=$atid"

# An on-screen pixel the sprite and FILL never touch: the CLEAR owns it.
"$CLI" inspect "$ARC1" --frames 3 --at 200,200 > "$OUT/at2.txt" 2>/dev/null
awk '$1=="AT" && $2=="200,200" && $3=="id=1"' "$OUT/at2.txt" >/dev/null ||
    fail "--at 200,200 must name the CLEAR (id=1)"
# Off-screen: trivially never written, id=0 plus the explain line.
"$CLI" inspect "$ARC1" --frames 3 --at 999,999 > "$OUT/at3.txt" 2>/dev/null
awk '$1=="AT" && $2=="999,999" && $3=="id=0"' "$OUT/at3.txt" >/dev/null ||
    fail "--at 999,999 must report id=0"
grep -q 'pixel never written' "$OUT/at3.txt" || fail "missing never-written note"
CHECKS=$((CHECKS + 1))
echo "  ok: --at: FILL owns the covered pixel, CLEAR the rest, off-screen id=0"

# --- 4) hashes identical with and without --commands (and == run's) ---------
h_plain=$("$CLI" inspect "$ARC1" --frames 3 | awk '$1=="STATE_HASH"{print $2}')
f_plain=$("$CLI" inspect "$ARC1" --frames 3 | awk '$1=="FB_HASH"{print $2}')
h_cmds=$("$CLI" inspect "$ARC1" --frames 3 --commands |
         awk '$1=="STATE_HASH"{print $2}')
f_cmds=$("$CLI" inspect "$ARC1" --frames 3 --commands |
         awk '$1=="FB_HASH"{print $2}')
run_line=$("$CLI" run "$ARC1" --frames 3 --quiet | grep 'final state hash=')
h_run=$(printf '%s\n' "$run_line" | sed -n 's/.*final state hash=\(0x[0-9A-Fa-f]*\).*/\1/p')
f_run=$(printf '%s\n' "$run_line" | sed -n 's/.*fb hash=\(0x[0-9A-Fa-f]*\).*/\1/p')
[ -n "$h_plain" ] && [ -n "$h_cmds" ] && [ -n "$h_run" ] || fail "missing hashes"
[ "$h_plain" = "$h_cmds" ] || fail "STATE_HASH differs: $h_plain vs $h_cmds"
[ "$h_cmds" = "$h_run" ]   || fail "inspect hash != run hash: $h_cmds vs $h_run"
[ "$f_plain" = "$f_cmds" ] || fail "FB_HASH differs: $f_plain vs $f_cmds"
[ "$f_cmds" = "$f_run" ]   || fail "inspect fb != run fb: $f_cmds vs $f_run"
CHECKS=$((CHECKS + 1))
echo "  ok: STATE_HASH/FB_HASH identical: inspect = inspect --commands = run"

# --- 5) --json combinations parse and agree; usage errors exit 2 ------------
"$CLI" inspect "$ARC1" --frames 3 --commands --json > "$OUT/c.json" 2>/dev/null
python3 -m json.tool < "$OUT/c.json" >/dev/null 2>&1 ||
    fail "--commands --json does not parse"
grep -q '"COMMANDS"' "$OUT/c.json" || fail "--json missing COMMANDS"
"$CLI" inspect "$ARC1" --frames 3 --at 12,12 --json > "$OUT/a.json" 2>/dev/null
python3 -m json.tool < "$OUT/a.json" >/dev/null 2>&1 ||
    fail "--at --json does not parse"
jatid=$(python3 -c 'import json, sys
print(json.load(open(sys.argv[1]))["AT"]["id"])' "$OUT/a.json")
[ "$jatid" = "$atid" ] || fail "json AT id $jatid != text $atid"

rc=0; "$CLI" inspect "$ARC1" --at 12  >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "--at without Y: exit $rc, want 2"
rc=0; "$CLI" inspect "$ARC1" --at x,12 >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "--at bad X: exit $rc, want 2"
CHECKS=$((CHECKS + 1))
echo "  ok: --json COMMANDS/AT parse and agree; malformed --at exits 2"

echo "test-inspect-commands: ok ($CHECKS checks, card B2)"
