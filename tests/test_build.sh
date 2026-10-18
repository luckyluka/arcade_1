#!/bin/sh
# M29: `arcade1 build` must be self-contained.
#
# The fresh-agent test hit a build that only worked inside the checkout:
# cmd_build shelled out to programs/crt0.S, programs/link.ld, the headers
# and build/packarc1. From any other directory (or without build/ present)
# it failed, which is exactly what an AI agent hits first.
#
# This test does what that agent did: copy the CLI into an empty directory
# containing nothing but the binary and one game.c, and build there. Then
# it runs the image and counts the drawn pixels, so the test also proves
# the SDK the CLI carries is the real one — a blank screen would mean the
# embedded crt0/headers drifted from programs/.
#
# Usage: test_build.sh <cli-binary>
set -eu

CLI=${1:?usage: test_build.sh <cli-binary>}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# A private TMPDIR, so "the build left nothing behind" is checkable.
mkdir "$OUT/tmp"
export TMPDIR="$OUT/tmp"

cp "$CLI" "$OUT/arcade1"
cd "$OUT"

cat > game.c <<'EOF'
/* 4x4 SPRITE4 from a table the guest fills at run time: 16 drawn pixels.
 * Small on purpose — that is the shape GCC deletes the fill for when the
 * SDK stops bracketing its draws with a barrier (see tests/test_barrier.c). */
#include "arcade1.h"
#define RGB(r, g, b) (unsigned)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31))
static unsigned char s4[4 * (4 / 2)];
int main(void)
{
    int t;
    /* A 4-bit pixel's colour is palette[bank*16 + nibble]; bank 0 is the
     * power-on default this sprite draws from, so entry 1 colours it. */
    arcade1_pal(1, RGB(0, 63, 0));
    for (t = 0; t < (int)sizeof s4; t++)
        s4[t] = 0x11;
    for (;;) {
        arcade1_clear(RGB(0, 0, 0));
        arcade1_sprite4(10, 10, s4, 4, 4, 0, 0);
        arcade1_present();
    }
}
EOF

fail() { echo "test-build: FAILED — $*" >&2; exit 1; }

# --- 1) build in a directory with no checkout, no build/, no programs/ -----
[ -e programs ] && fail "test setup: a programs/ dir appeared"
[ -e build ]    && fail "test setup: a build/ dir appeared"

./arcade1 build game.c game.arc1 > build.txt || { cat build.txt >&2; fail "arcade1 build failed here"; }
[ -f game.arc1 ] || fail "no game.arc1 produced"

# The image must be a real ARC1 (magic at offset 0).
[ "$(head -c 4 game.arc1)" = "ARC1" ] || fail "output is not an ARC1 image"

# Still nothing else in the directory, and the scratch dir was cleaned up.
[ -e programs ] && fail "build created a programs/ dir"
[ -e build ]    && fail "build created a build/ dir"
[ -z "$(ls -A "$TMPDIR")" ] || fail "build left temp files behind: $(ls -A "$TMPDIR")"

# --- 2) the image runs and draws -------------------------------------------
./arcade1 run game.arc1 --frames 3 --capture f.ppm --quiet > run.txt || \
    { cat run.txt >&2; fail "arcade1 run failed on the built image"; }
[ "$(head -c 2 f.ppm)" = "P6" ] || fail "capture is not a P6 PPM"

# Count non-black pixels in the payload (RGB888, 480x270).
PX=$(tail -c $((480 * 270 * 3)) f.ppm | od -An -v -tu1 | awk '
    { for (i = 1; i <= NF; i++) { b[n % 3] = $i; n++
        if (n % 3 == 0 && (b[0] || b[1] || b[2])) px++ } }
    END { print px + 0 }')
[ "$PX" = "16" ] || fail "built image painted $PX pixels, want 16"

# --- 3) defaults and argument handling -------------------------------------
./arcade1 build game.c > default.txt || fail "build with no output name failed"
[ -f program.arc1 ] || fail "default output name is not program.arc1"
./arcade1 build 2> usage.txt && fail "build with no source should fail"
[ "$(cat usage.txt)" != "" ] || fail "build printed no usage on error"

echo "test-build: ok — built and ran a game from an empty directory (16 px)"
