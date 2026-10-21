/* flip_probe — the device's horizontal flip, proved (ROADMAP_V4 card 7.4/7.7).
 *
 * The library's enemies are drawn facing right, and the claim on the
 * library's behalf is that the DEVICE mirrors them for free: `sprite4`'s
 * flags bit0 flips X. A pack of sprites therefore counts twice.
 *
 * This guest draws one asymmetric 4-bit shape twice — plain and flipped —
 * and the test counts the painted pixels on each side of each sprite.
 */

#include "arcade1.h"

/* An 8x8 4-bit shape, pointing LEFT: two pixels per byte, the LEFT pixel
 * in the HIGH nibble. */
static const unsigned char shape[8 * 4] = {
    0xC0, 0x00, 0x00, 0x00,
    0xCF, 0x00, 0x00, 0x00,
    0xFF, 0x30, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00,
    0xFF, 0x30, 0x00, 0x00,
    0xCF, 0x00, 0x00, 0x00,
    0xC0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

#define PLAIN_X 20
#define FLIP_X 100
#define Y 20

int main(void)
{
    for (;;) {
        arcade1_clear(0x0000u);
        /* the plain shape */
        arcade1_sprite4(PLAIN_X, Y, shape, 8, 8, 0, 0);
        /* the same shape, mirrored by the device (flags bit0 = flip X) */
        arcade1_sprite4(FLIP_X, Y, shape, 8, 8, 0, 1);
        arcade1_present();
    }
    return 0;
}
