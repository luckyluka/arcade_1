#!/bin/sh
# HD_EXPERIMENT.md card H1 — the guard that lets the experiment into `make
# check` (§0 rule 3: "not part of make check except the small guard test in
# H1 that proves the default build is unchanged").
#
# The experiment builds SEPARATE binaries (build/arcade1-hd2, build/arcade1-hd4)
# with -DARCADE1_HD_SCALE=k; the default build never sees the flag. This test
# proves both halves of that claim, cheaply:
#
#   1. the default machine carries NO scale: `arcade1 info` reports the pinned
#      480x270 / 129600-pixel screen, and a real capture is a 480x270 image —
#      so a leaked -DARCADE1_HD_SCALE, or a framebuffer sized by it, fails here
#      rather than silently changing every hash;
#   2. the default build's hashes are UNCHANGED: the frame lines it prints for
#      a committed game match the frozen tests/golden file byte for byte.
#
# Deliberately small, and deliberately overlapping test-golden: this is the
# experiment's one admission into `make check`, and what it guards should be
# obvious at a glance. The k = 2 / k = 4 acceptance lives in
# experiments/hd/test_scale.sh, run by `make hd-experiment`, NOT here.
#
# usage: tests/test_hd_default.sh <arcade1-cli> <rom-dir> <golden-dir>
set -u

CLI=${1:?usage: test_hd_default.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROMDIR=${2:?}
GOLDEN=${3:?}
INPUTS=$(dirname "$0")/inputs
GAME=petalblade
FRAMES=60

fail=0
ok()  { printf '  ok: %s\n' "$*"; }
bad() { printf '  FAIL: %s\n' "$*"; fail=1; }

OUT=$(mktemp -d) || exit 2
trap 'rm -rf "$OUT"' EXIT INT TERM

# --- 1. the default build carries no scale ---------------------------------
INFO=$("$CLI" info 2>&1) || bad "arcade1 info exited $?"

if printf '%s\n' "$INFO" | grep -q '^  screen *480x270 (RGB565, 129600 pixels)$'
then
    ok "info reports the pinned 480x270 / 129600-pixel screen (no HD scale)"
else
    bad "info does not report 480x270 / 129600 pixels — a scale leaked into"
    printf '%s\n' "$INFO" | grep '^  screen' | sed 's/^/       /'
fi

if "$CLI" run "$ROMDIR/$GAME.arc1" --frames 1 \
       --capture "$OUT/boot.ppm" > /dev/null 2>&1; then
    # P6 header written by the CLI: magic, width, height, maxval, raster.
    if awk 'NR==1 && $0=="P6"{m=1} NR==2 && $0=="480 270"{w=1}
            NR==3 && $0=="255"{v=1} END{exit !(m&&w&&v)}' "$OUT/boot.ppm"
    then
        ok "a real capture is 480x270 (the framebuffer really is 480x270)"
    else
        bad "a capture is not 480x270 — header:"
        head -3 "$OUT/boot.ppm" | od -c | sed 's/^/       /'
    fi
else
    bad "arcade1 run --capture exited $?"
fi

# --- 2. the default build's hashes are unchanged ---------------------------
if [ ! -f "$GOLDEN/$GAME.txt" ]; then
    bad "missing golden $GOLDEN/$GAME.txt"
elif "$CLI" run "$ROMDIR/$GAME.arc1" --frames "$FRAMES" \
        --input "$INPUTS/$GAME.txt" 2>/dev/null | grep '^frame ' \
        > "$OUT/got.txt"; then
    head -n "$FRAMES" "$GOLDEN/$GAME.txt" > "$OUT/want.txt"
    if diff -q "$OUT/got.txt" "$OUT/want.txt" > /dev/null 2>&1; then
        ok "first $FRAMES frame hashes of $GAME match tests/golden/$GAME.txt exactly"
    else
        bad "the default build's frame hashes differ from tests/golden/$GAME.txt"
        diff "$OUT/got.txt" "$OUT/want.txt" 2>&1 | head -4 | sed 's/^/       /'
    fi
else
    bad "arcade1 run for $GAME exited $?"
fi

if [ "$fail" -ne 0 ]; then
    echo "test-hd-default: FAILED — the default build is not unchanged"
    exit 1
fi
echo "test-hd-default: ok (HD experiment's make check guard, card H1)"
exit 0
