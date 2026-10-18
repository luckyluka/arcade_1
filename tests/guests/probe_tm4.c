/* M29 regression probe: a TILEMAP4 map whose cells and tiles the guest fills
 * in RAM at run time — the exact shape the M29 fresh-agent build got wrong.
 *
 * The video device reads those tables through the integer addresses the guest
 * hands it in MAP_ADDR/TILE_ADDR. That read is invisible to the compiler, so
 * without the M29 draw barrier in programs/gfx.h GCC deletes the fills (the
 * address only escapes via an integer cast into a volatile MMIO register) and
 * the device draws from zeroed RAM: a blank screen, no error anywhere.
 *
 * tests/test_barrier.c runs one geometry per build (-DCOLS/-DROWS) and counts
 * the drawn pixels; each geometry that miscompiles draws 0. Geometry is
 * deliberately varied: some loop shapes survive, some don't, so the test
 * covers both.
 */
#include "arcade1.h"

#define RGB(r, g, b) (unsigned)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31))

#ifndef COLS
#define COLS 4
#endif
#ifndef ROWS
#define ROWS 3
#endif

#define TW 16                  /* tile width  in pixels */
#define TH 8                   /* tile height in pixels */
#define MAPN 64                /* >= COLS * ROWS for every built geometry  */

static unsigned       cells[MAPN];            /* filled at run time (RAM)  */
static unsigned char  tile[TH * (TW / 2)];    /* 4-bit: two pixels/byte    */

int main(void)
{
    static const unsigned short pal[32] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, RGB(31, 0, 0), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    int r, c, t;

    gfx_pal_upload(0, pal, (int)(sizeof pal / sizeof pal[0]));

    for (t = 0; t < (int)(sizeof tile); t++)
        tile[t] = 0x11;                     /* every nibble = colour 1    */

    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++)
            cells[r * COLS + c] = 1u | (1u << 16);  /* tile 1, bank 0      */

    for (;;) {
        arcade1_clear(RGB(0, 0, 0));
        arcade1_tilemap4(0, 20, cells, tile, COLS, ROWS, TW, TH);
        arcade1_present();
    }
}
