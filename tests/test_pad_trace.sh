#!/bin/sh
# ROADMAP_V4 card 4.3: the pad trace and the edge-trigger trap.
#
# `arcade1 run --pad-trace FILE` writes one line per frame — "frame N pad
# 0xXX" — the pad the guest actually saw. Acceptance (pinned):
#   1. the trace equals the script's expansion on tests/inputs/flappy.txt
#      (the trace is the script, expanded frame by frame);
#   2. the trap and the fix (tests/inputs/edge_{trap,fixed}.txt) diverge
#      exactly as the docs say: the held script flaps 0 times and dies,
#      the released script flaps 4 times and is still alive;
#   3. the traces show why: the held script holds 0x10 across the restart,
#      the fixed script releases (0x00) for one frame;
#   4. the trace format is exactly "frame N pad 0xXX".
#
# Host-side only: the test writes to a temp dir, it never touches the
# committed goldens or ROMs.
#
# Usage: test_pad_trace.sh <arcade1-cli> <guest-build-dir> <riscv-nm>
set -eu

CLI=${1:?usage: test_pad_trace.sh <arcade1-cli> <guest-build-dir> <riscv-nm>}
GUEST=${2:?usage: test_pad_trace.sh <arcade1-cli> <guest-build-dir> <riscv-nm>}
NM=${3:?usage: test_pad_trace.sh <arcade1-cli> <guest-build-dir> <riscv-nm>}

case "$CLI" in /*) ;; *) CLI="$(pwd)/$CLI" ;; esac

HERE=$(dirname "$0")
FLAPPY=$HERE/../tests/roms/flappy.arc1
SCRIPT=$HERE/inputs/flappy.txt
TRAP=$HERE/inputs/edge_trap.txt
FIXED=$HERE/inputs/edge_fixed.txt
PROBE=$GUEST/probe_edge.bin
ELF=$GUEST/probe_edge.elf

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-pad-trace: FAILED — $*" >&2; exit 1; }
CHECKS=0

[ -f "$FLAPPY" ] || fail "missing $FLAPPY"
[ -f "$SCRIPT" ] || fail "missing $SCRIPT"
[ -f "$TRAP" ]   || fail "missing $TRAP"
[ -f "$FIXED" ]  || fail "missing $FIXED"
[ -f "$PROBE" ]  || fail "missing $PROBE (build it: make programs/build/probe_edge.bin)"

# --- 1) the trace equals the script's expansion ---------------------------
FRAMES=520
rc=0
"$CLI" run "$FLAPPY" --input "$SCRIPT" --frames $FRAMES \
    --pad-trace "$OUT/flappy.pad" --quiet > /dev/null 2>&1 || rc=$?
[ "$rc" -eq 0 ] || fail "run --pad-trace exit $rc"

# The expansion, computed here from the script alone: later entries win, a
# frame no entry covers has no buttons held.
python3 - "$SCRIPT" "$FRAMES" > "$OUT/expect.pad" <<'PY'
import sys

script, frames = sys.argv[1], int(sys.argv[2])
BITS = {"up": 0x01, "down": 0x02, "left": 0x04, "right": 0x08,
        "a": 0x10, "b": 0x20, "start": 0x40, "select": 0x80}
entries = []
for line in open(script):
    line = line.split("#", 1)[0].strip()
    if not line:
        continue
    parts = line.split()
    first, last = int(parts[0]), int(parts[1])
    mask = 0
    for word in parts[2:]:
        if word != "none":
            mask |= BITS[word]
    entries.append((first, last, mask))
for frame in range(frames):
    mask = 0
    for first, last, m in entries:          # later lines win
        if first <= frame <= last:
            mask = m
    print("frame %4d pad 0x%02X" % (frame, mask))
PY

cmp -s "$OUT/expect.pad" "$OUT/flappy.pad" || {
    diff "$OUT/expect.pad" "$OUT/flappy.pad" | head -6 >&2
    fail "the pad trace does not match the script's expansion"
}
CHECKS=$((CHECKS + 1))
echo "  ok: the trace equals the script's expansion ($FRAMES frames)"

# --- 4) the trace format --------------------------------------------------
if grep -qvE '^frame +[0-9]+ pad 0x[0-9A-F]{2}$' "$OUT/flappy.pad"; then
    fail "the trace carries a line that is not 'frame N pad 0xXX'"
fi
CHECKS=$((CHECKS + 1))
echo "  ok: the trace format is 'frame N pad 0xXX'"

# --- 2) + 3) the trap and the fix diverge, and the traces show why --------
"$NM" "$ELF" > "$OUT/edge.syms" 2>/dev/null || fail "nm failed on $ELF"

run_probe() {   # $1 = script, $2 = output prefix
    "$CLI" run "$PROBE" --input "$1" --frames 40 --syms "$OUT/edge.syms" \
        --vars state,presses,flaps,restarts,dead_frame --quiet \
        --pad-trace "$2.pad" > "$2.out" 2>/dev/null
}
var() { sed -n "s/^var $1 = \(-*[0-9]*\)$/\1/p" "$2"; }

run_probe "$TRAP" "$OUT/trap"
run_probe "$FIXED" "$OUT/fixed"

# The trap: one press, spent on the restart; no flap; the run dies again.
[ "$(var state "$OUT/trap.out")" = 1 ] || \
    fail "the trap script should end dead (state 1)"
[ "$(var presses "$OUT/trap.out")" = 1 ] || \
    fail "the trap script should show exactly one press edge"
[ "$(var flaps "$OUT/trap.out")" = 0 ] || \
    fail "the trap script should flap 0 times (the edge was spent on the restart)"
[ "$(var restarts "$OUT/trap.out")" = 1 ] || \
    fail "the trap script should restart once"
[ "$(var dead_frame "$OUT/trap.out")" != "-1" ] || \
    fail "the trap script should have died"

# The fix: the release makes the next press a real edge, so the bird flies.
[ "$(var state "$OUT/fixed.out")" = 0 ] || \
    fail "the fixed script should end alive (state 0)"
[ "$(var flaps "$OUT/fixed.out")" = 4 ] || \
    fail "the fixed script should flap 4 times"
[ "$(var presses "$OUT/fixed.out")" = 5 ] || \
    fail "the fixed script should show 5 press edges"
[ "$(var restarts "$OUT/fixed.out")" = 1 ] || \
    fail "the fixed script should restart once"
[ "$(var dead_frame "$OUT/fixed.out")" = "-1" ] || \
    fail "the fixed script should never die"
CHECKS=$((CHECKS + 1))
echo "  ok: the trap dies with 0 flaps; the fix is alive with 4 flaps"

# The traces show the difference: the trap holds A across the restart, the
# fix releases it for one frame.
[ "$(sed -n '7p' "$OUT/trap.pad")" = "frame    6 pad 0x10" ] || \
    fail "the trap trace should hold A at frame 6"
[ "$(sed -n '7p' "$OUT/fixed.pad")" = "frame    6 pad 0x10" ] || \
    fail "the fixed trace should press A at frame 6"
[ "$(sed -n '8p' "$OUT/fixed.pad")" = "frame    7 pad 0x00" ] || \
    fail "the fixed trace should release A at frame 7"
CHECKS=$((CHECKS + 1))
echo "  ok: the traces show the held press and the one-frame release"

echo "test-pad-trace: ok ($CHECKS checks, card 4.3)"
