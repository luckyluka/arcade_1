/* probe10: 6x1 tilemap4 with an explicit per-column cell table.
 * B0..B5 = palette bank per column, T0..T5 = tile index per column. */
#include "include/arcade1.h"

#define RGB(r, g, b) (unsigned)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31))

#define TW 48
#define TH 12

#ifndef B0
#define B0 1
#endif
#ifndef B1
#define B1 2
#endif
#ifndef B2
#define B2 3
#endif
#ifndef B3
#define B3 4
#endif
#ifndef B4
#define B4 5
#endif
#ifndef B5
#define B5 1
#endif
#ifndef T0
#define T0 1
#endif
#ifndef T1
#define T1 1
#endif
#ifndef T2
#define T2 1
#endif
#ifndef T3
#define T3 1
#endif
#ifndef T4
#define T4 1
#endif
#ifndef T5
#define T5 1
#endif

static unsigned cells[6];
static unsigned char tile[TH * (TW / 2)];

int main(void)
{
    int i;
    static const unsigned short pal[96] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0xF800, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0x07E0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0x001F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0xFFE0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0xF81F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    static const unsigned bs[6] = { B0, B1, B2, B3, B4, B5 };
    static const unsigned ts[6] = { T0, T1, T2, T3, T4, T5 };

    gfx_pal_upload(0, pal, (int)(sizeof pal / sizeof pal[0]));
    for (i = 0; i < TH * (TW / 2); i++)
        tile[i] = 0x11;
    for (i = 0; i < 6; i++)
        cells[i] = ts[i] | (bs[i] << 16);

    for (;;) {
        arcade1_clear(RGB(0, 0, 0));
        arcade1_tilemap4(0, 20, cells, tile, 6, 1, TW, TH);
        arcade1_present();
    }
}
