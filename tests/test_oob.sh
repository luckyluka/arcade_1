#!/bin/sh
# M29: host-side video diagnostics for out-of-window source addresses.
#
# The device fetches image bytes from the integer addresses the guest hands
# it (ADDR, MAP_ADDR, TILE_ADDR). Only ROM (0..0x3FFFF) and RAM
# (0x20000000..0x2000FFFF) are guest memory; anything else reads back as
# zero, so the draw silently paints nothing. Before M29 that mistake was
# invisible: no error, no warning, an empty screen.
#
# arcade1_video.c now counts those skipped draws (video.stats.skipped_ops),
# `arcade1 run` reports them as one stderr warning line, and --strict turns
# them into a non-zero exit. This test builds a guest that draws one good
# sprite and one from address 0x40000 (just past the ROM) and checks all
# three behaviours, plus that a clean guest stays silent.
#
# Usage: test_oob.sh <cli-binary>
set -eu

CLI=${1:?usage: test_oob.sh <cli-binary>}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

cp "$CLI" "$OUT/arcade1"
cd "$OUT"

# --- oob.c: one sprite from RAM (must draw), one from 0x40000 (must not) ---
cat > oob.c <<'EOF'
/* Two SPRITE4 draws per frame: the first from a RAM table the guest fills
 * (16 pixels), the second from address 0x40000 — one byte past the ROM,
 * outside every guest memory window (256 examined pixels, all fetching
 * zero, so all 256 draw nothing). The device's behaviour is unchanged by
 * the diagnostics: the bad sprite was always a silent no-op. */
#include "arcade1.h"
#define RGB(r, g, b) (unsigned)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31))
static unsigned char s4[4 * (4 / 2)];
int main(void)
{
    int t;
    arcade1_pal(1, RGB(0, 63, 0));   /* colour = palette[bank*16 + nibble] */
    for (t = 0; t < (int)sizeof s4; t++)
        s4[t] = 0x11;
    for (;;) {
        arcade1_clear(RGB(0, 0, 0));
        arcade1_sprite4(10, 10, s4, 4, 4, 0, 0);            /* good: 16 px */
        arcade1_sprite4(60, 10, (const void *)0x40000, 16, 16, 0, 0);  /* bad */
        arcade1_present();
    }
}
EOF

# --- ok.c: the same game with every source in guest memory -----------------
sed 's|(const void \*)0x40000|s4|; s|, 16, 16, 0, 0);  /\* bad \*/|, 4, 4, 0, 0);|' oob.c > ok.c

fail() { echo "test-oob: FAILED — $*" >&2; exit 1; }

./arcade1 build oob.c oob.arc1 > b1.txt || { cat b1.txt >&2; fail "building oob.c"; }
./arcade1 build ok.c   ok.arc1  > b2.txt || { cat b2.txt >&2; fail "building ok.c"; }

# --- 1) default: runs, warns once, still exits 0 ---------------------------
./arcade1 run oob.arc1 --frames 4 --quiet > run1.txt 2> warn1.txt || \
    fail "run of the out-of-window guest must still exit 0 by default"
[ "$(wc -l < warn1.txt)" = "1" ] || fail "want exactly one warning line, got: $(cat warn1.txt)"
grep -q "outside ROM/RAM" warn1.txt || fail "warning does not name the cause: $(cat warn1.txt)"
grep -q -- "--strict" warn1.txt || fail "warning does not mention --strict"
grep -q "warning" run1.txt && fail "warning leaked onto stdout (it must go to stderr)"
note_n=$(sed -n 's/.*warning: \([0-9]*\) draws.*/\1/p' warn1.txt)
[ "${note_n:-0}" = "1024" ] || fail "want 1024 skipped draws (4 frames x 256 px), got '$note_n'"

# --- 2) --strict: same run, non-zero exit ----------------------------------
./arcade1 run oob.arc1 --frames 4 --quiet --strict > run2.txt 2> warn2.txt && \
    fail "--strict must exit non-zero on skipped draws"
grep -q "outside ROM/RAM" warn2.txt || fail "--strict run did not warn"

# --- 3) a clean guest: no warning, --strict exits 0 ------------------------
./arcade1 run ok.arc1 --frames 4 --quiet --strict > run3.txt 2> warn3.txt || \
    fail "clean guest failed under --strict"
[ ! -s warn3.txt ] || fail "clean guest warned: $(cat warn3.txt)"

# --- 4) the diagnostics changed nothing on screen --------------------------
# The bad sprite could never draw (it fetches zeros), so the framebuffer
# must hold exactly the good sprite's 16 pixels.
./arcade1 run oob.arc1 --frames 4 --capture oob.ppm --quiet > /dev/null 2>&1
PX=$(tail -c $((480 * 270 * 3)) oob.ppm | od -An -v -tu1 | awk '
    { for (i = 1; i <= NF; i++) { b[n % 3] = $i; n++
        if (n % 3 == 0 && (b[0] || b[1] || b[2])) px++ } }
    END { print px + 0 }')
[ "$PX" = "16" ] || fail "picture changed: $PX pixels, want 16"

echo "test-oob: ok — warning on stderr, --strict fails, clean guest silent (16 px)"
