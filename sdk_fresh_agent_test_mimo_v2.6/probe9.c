/* probe9: drive the TILEMAP4 registers directly, one variable at a time.
 * Geometry is chosen with -D so each build tests one case.
 *   COLS ROWS TW TH   map geometry / tile size
 *   NTILE             how many distinct tiles the tile blob holds
 *   USEIDX            1 = cell tile index cycles 1..NTILE, 0 = every cell uses tile 1
 *   BANKCOL           1 = palette bank cycles per column, 0 = all cells bank 1
 */
#include "include/arcade1.h"

#define RGB(r, g, b) (unsigned)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31))

#ifndef COLS
#define COLS 10
#endif
#ifndef ROWS
#define ROWS 3
#endif
#ifndef TW
#define TW 16
#endif
#ifndef TH
#define TH 8
#endif
#ifndef NTILE
#define NTILE 1
#endif
#ifndef USEIDX
#define USEIDX 0
#endif
#ifndef BANKCOL
#define BANKCOL 1
#endif
#ifndef MAPN
#define MAPN 64
#endif
#ifndef X0
#define X0 0
#endif
#ifndef Y0
#define Y0 20
#endif

static unsigned cells[MAPN];
static unsigned char tile[NTILE * TH * (TW / 2)];

int main(void)
{
    int r, c, t;
    static const unsigned short pal[96] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0xF800, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0x07E0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0x001F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0xFFE0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0xF81F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };

    gfx_pal_upload(0, pal, (int)(sizeof pal / sizeof pal[0]));

    for (t = 0; t < NTILE * TH * (TW / 2); t++)
        tile[t] = 0x11;                       /* every nibble = colour 1 */

    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++)
            cells[r * COLS + c] =
                (unsigned)(USEIDX ? (c % NTILE) + 1 : 1) |
                ((unsigned)(BANKCOL ? (c % 5) + 1 : 1) << 16);

    for (;;) {
        arcade1_clear(RGB(0, 0, 0));
#ifdef USE_API
        arcade1_tilemap4(X0, Y0, cells, tile, COLS, ROWS, TW, TH);
#else
        /* poke the registers exactly as gfx_tilemap4 does, so the probe is
         * independent of any header/register mismatch */
        GFX_REG_X         = X0;
        GFX_REG_Y         = Y0;
        GFX_REG_MAP_ADDR  = (unsigned)cells;
        GFX_REG_TILE_ADDR = (unsigned)tile;
        GFX_REG_COLS      = COLS;
        GFX_REG_ROWS      = ROWS;
        GFX_REG_W         = TW;
        GFX_REG_H         = TH;
        GFX_REG_CMD       = GFX_CMD_TILEMAP4;
#endif
        arcade1_present();
    }
}
