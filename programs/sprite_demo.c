/* ARCADE-1 milestone 19 guest program — sprite blits.
 *
 * The point of milestone 14 was drawing rectangles; here the same machine
 * draws 1-bpp images through the SPRITE command (docs.md M19). The harness
 * (tests/test_sprite.c) verifies every pixel exactly, so the image and the
 * placement are chosen to pin the semantics:
 *
 *   - MSB = leftmost pixel, rows padded to whole bytes (imgA is 10x6, so
 *     every row is 2 bytes and the low 6 bits of the second byte are
 *     padding — never drawn).
 *   - Clear bits are transparent: the background survives underneath.
 *   - Sources: ROM image and a RAM copy of it both draw identically.
 *   - Clipping: hangs off right+bottom, hangs off top+left (negative origin
 *     shifts which image pixels land on screen), fully off-screen, w=0/h=0.
 *   - A source address outside ROM/RAM (here the MMIO window) draws
 *     nothing and has no side effects on the devices.
 *
 * Layout:
 *   gfx_clear(blue)                        background
 *   gfx_sprite(20, 30, imgA, 10, 6, red)    32 red pixels (set bits)
 *   gfx_sprite(-4, -2, imgA, 10, 6, green)  negative origin, 16 visible px
 *   gfx_sprite(315, 237, imgA, 10, 6, white) hangs off right+bottom, 8 px
 *   gfx_sprite(400, 100, ...)               fully right of the screen: none
 *   gfx_sprite(-20, -10, ...)               fully above the screen: none
 *   gfx_sprite(100, 100, ..., 0, 6, ...)    zero width: none
 *   gfx_sprite(100, 100, ..., 10, 0, ...)   zero height: none
 *   gfx_sprite(200, 150, ARCADE1_MMIO_BASE, ...)  illegal source: none
 *   gfx_sprite(150, 100, img_ram, ...)      RAM copy -> 32 yellow pixels
 *   gfx_present()                           one present, counted by the device
 */

#include "gfx.h"

#define COLOR_BLUE   0x001Fu
#define COLOR_RED    0xF800u
#define COLOR_GREEN  0x07E0u
#define COLOR_WHITE  0xFFFFu
#define COLOR_YELLOW 0xFFE0u

/* 10x6 image, 2 bytes per row, MSB = leftmost pixel:
 *   row0 1010101011  row1 1111000000  row2 0000111101
 *   row3 0011001111  row4 1111111111  row5 0000000010
 */
static const unsigned char img[12] = {
    0xAA, 0xC0,  0xF0, 0x00,  0x0F, 0x50,
    0x33, 0xCC,  0xFF, 0xFF,  0x00, 0x80,
};

static unsigned char img_ram[12];

int main(void)
{
    int i;

    gfx_clear(COLOR_BLUE);

    /* ROM source, fully on-screen. */
    gfx_sprite(20, 30, img, 10, 6, COLOR_RED);

    /* Negative origin: image cols 4..9 land at screen x 0..5,
     * image rows 2..5 land at screen y 0..3. */
    gfx_sprite(-4, -2, img, 10, 6, COLOR_GREEN);

    /* Hangs off the right and bottom edges; image cols 0..4 / rows 0..2
     * are on-screen. */
    gfx_sprite(315, 237, img, 10, 6, COLOR_WHITE);

    /* Fully off-screen: nothing drawn. */
    gfx_sprite(400, 100, img, 10, 6, COLOR_RED);
    gfx_sprite(-20, -10, img, 10, 6, COLOR_RED);

    /* Zero size: nothing drawn. */
    gfx_sprite(100, 100, img, 0, 6, COLOR_RED);
    gfx_sprite(100, 100, img, 10, 0, COLOR_RED);

    /* Source outside ROM/RAM (the MMIO window): nothing drawn, and the
     * device must not disturb the timer or anything else. */
    gfx_sprite(200, 150, (const void *)ARCADE1_MMIO_BASE, 10, 6, COLOR_RED);

    /* Same image from RAM must render identically to the ROM original. */
    for (i = 0; i < 12; i++)
        img_ram[i] = img[i];
    gfx_sprite(150, 100, img_ram, 10, 6, COLOR_YELLOW);

    gfx_present();
    return 0;
}
