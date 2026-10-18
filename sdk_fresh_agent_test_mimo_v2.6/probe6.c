/* probe6: how many cells of a TILEMAP4 actually get drawn? */
#include "include/arcade1.h"

#define RGB(r, g, b) (unsigned)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31))

#define COLS 10
#define ROWS 3
#define TW   16
#define TH   8

static unsigned cells[COLS * ROWS];
static const unsigned char tile[TH * (TW / 2)] = {
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
};

int main(void)
{
    int r, c;
    static const unsigned short pal[64] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, (unsigned short)RGB(31, 0, 0), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, (unsigned short)RGB(0, 63, 0), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, (unsigned short)RGB(0, 0, 31), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, (unsigned short)RGB(31, 63, 0), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, (unsigned short)RGB(31, 0, 31), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };

    gfx_pal_upload(0, pal, 64);
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++)
            cells[r * COLS + c] = 1u | ((unsigned)((c % 5) + 1) << 16);

    for (;;) {
        arcade1_clear(RGB(0, 0, 0));
        arcade1_tilemap4(0, 20, cells, tile, COLS, ROWS, TW, TH);
        arcade1_present();
    }
}
