/* probe7: is the TILEMAP4 cols/rows geometry what the docs say? */
#include "include/arcade1.h"

#define RGB(r, g, b) (unsigned)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31))

#ifndef COLS
#define COLS 5
#endif
#ifndef ROWS
#define ROWS 2
#endif
#define TW 16
#define TH 8

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
    /* 6 banks: distinct reds/greens so every bank is visible */
    static const unsigned short pal[96] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0xF800, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0x07E0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0x001F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0xFFE0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0xF81F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };

    gfx_pal_upload(0, pal, 96);
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++)
            cells[r * COLS + c] = 1u | ((unsigned)((c % 5) + 1) << 16);

    for (;;) {
        arcade1_clear(RGB(0, 0, 0));
        arcade1_tilemap4(0, 20, cells, tile, COLS, ROWS, TW, TH);
        arcade1_present();
    }
}
