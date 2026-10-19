#!/bin/sh
# ROADMAP_V3 card A1: `arcade1 build` writes <image>.syms, and run/test/inspect
# use it.
#
# 1. build writes game.syms whose header CRC equals the image header CRC
# 2. `--vars` without `--syms` auto-loads the sidecar
# 3. a .syms from a different build is refused (exit 2, teaching message)
# 4. header-less .syms (the tests/roms legacy dumps) still work
# 5. no sidecar for the image -> exit 2 with the build hint
#
# Like test_build.sh this runs from a directory containing only the arcade1
# binary and the guest source: the build must be self-contained there too.
#
# Usage: test_syms.sh <arcade1-cli> <rom-dir> <golden-dir>
set -eu

CLI=${1:?usage: test_syms.sh <arcade1-cli> <rom-dir> <golden-dir>}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

mkdir "$OUT/tmp"
export TMPDIR="$OUT/tmp"
cp "$CLI" "$OUT/arcade1"
cd "$OUT"

fail() { echo "test-syms: FAILED — $*" >&2; exit 1; }

cat > game.c <<'EOF'
/* Two globals, both written every frame: kept by the optimizer and stable
 * to read at any frame, so the expected --vars values are exact. */
#include "arcade1.h"
int score = 42;
int lives = 7;
int main(void)
{
    for (;;) {
        score = 42;
        lives = 7;
        arcade1_present();
    }
}
EOF
# same shape, different payload CRC (score's initializer changes)
sed 's/= 42;/= 43;/' game.c > game2.c

# --- 1) build writes game.syms, header CRC == image header CRC ---------------
./arcade1 build game.c game.arc1 > build.txt 2>&1 || {
    cat build.txt >&2; fail "arcade1 build failed here"; }
[ -f game.arc1 ] || fail "no game.arc1 produced"
[ -f game.syms ]  || fail "build did not write game.syms"
grep -q '^# arc1-syms crc32=0x[0-9a-f][0-9a-f]*$' game.syms ||
    fail "missing/ malformed syms header: $(head -1 game.syms)"
# both are data globals of this game (RAM window, data type)
grep -Eq '^2000[0-9a-f]{4} [Dd] score$' game.syms || fail "score not in game.syms"
grep -Eq '^2000[0-9a-f]{4} [Dd] lives$' game.syms || fail "lives not in game.syms"
# image payload CRC is the little-endian u32 at byte 12 of the header
SYMS_CRC=0x$(sed -n 's/^# arc1-syms crc32=0x//p' game.syms)
IMG_CRC=0x$(od -An -v -tu1 -j12 -N4 game.arc1 |
            awk '{printf "%02x%02x%02x%02x", $4, $3, $2, $1}')
[ "$SYMS_CRC" = "$IMG_CRC" ] ||
    fail "header CRCs differ: syms=$SYMS_CRC image=$IMG_CRC"
[ -z "$(ls -A "$TMPDIR")" ] || fail "build left temp files behind"

# --- 2) --vars without --syms auto-loads game.syms ---------------------------
./arcade1 run game.arc1 --frames 3 --vars score,lives --quiet > v1.txt ||
    fail "auto-load run failed"
grep -q '^var score = 42$' v1.txt || fail "auto-load: no 'var score = 42': $(cat v1.txt)"
grep -q '^var lives = 7$'  v1.txt || fail "auto-load: no 'var lives = 7': $(cat v1.txt)"

# --- 3) .syms from another build -> exit 2 + message -------------------------
./arcade1 build game2.c game2.arc1 > build2.txt 2>&1 || fail "second build failed"
[ -f game2.syms ] || fail "second build did not write game2.syms"
rc=0; ./arcade1 run game.arc1 --frames 2 --syms game2.syms \
      --vars score --quiet 2> err1.txt || rc=$?
[ "$rc" -eq 2 ] || fail "cross-build .syms: want exit 2, got $rc"
grep -q 'belongs to a different build' err1.txt ||
    fail "cross-build: missing message: $(cat err1.txt)"

# --- 4) header-less .syms still work -----------------------------------------
tail -n +2 game.syms > plain.syms
./arcade1 run game.arc1 --frames 2 --syms plain.syms --vars score --quiet > v2.txt ||
    fail "header-less .syms run failed"
grep -q '^var score = 42$' v2.txt || fail "header-less: $(cat v2.txt)"

# --- 5) no sidecar for this image -> exit 2 + build hint ---------------------
cp game.arc1 bare.arc1
rc=0; ./arcade1 run bare.arc1 --frames 2 --vars score --quiet 2> err2.txt || rc=$?
[ "$rc" -eq 2 ] || fail "no sidecar: want exit 2, got $rc"
grep -q 'no symbols for bare.arc1' err2.txt ||
    fail "no sidecar: missing message: $(cat err2.txt)"

echo "test-syms: ok — build writes .syms, auto-load, CRC check, header-less path"
