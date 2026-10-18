/* M29 regression probe: a small SPRITE4 image the guest fills in RAM.
 *
 * A 4x4 sprite is 8 bytes of image data — small enough that GCC fully
 * unrolls the fill loop, sees the array address only escape through an
 * integer cast into a volatile MMIO register, and (without the M29 draw
 * barrier in programs/gfx.h) deletes the whole loop as dead. The device
 * then draws an all-zero (transparent) sprite: 0 pixels instead of 16.
 *
 * tests/test_barrier.c counts the drawn pixels: 16 with the barrier,
 * 0 without it.
 */
#include "arcade1.h"

#define RGB(r, g, b) (unsigned)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31))

#define SPRW 4
#define SPRH 4

static unsigned char s4[SPRH * (SPRW / 2)];   /* 4-bit: two pixels/byte   */

int main(void)
{
    static const unsigned short pal[32] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, RGB(0, 63, 0), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    int t;

    gfx_pal_upload(0, pal, (int)(sizeof pal / sizeof pal[0]));

    for (t = 0; t < (int)(sizeof s4); t++)
        s4[t] = 0x11;                       /* every nibble = colour 1   */

    for (;;) {
        arcade1_clear(RGB(0, 0, 0));
        arcade1_sprite4(10, 10, s4, SPRW, SPRH, 1, 0);
        arcade1_present();
    }
}
