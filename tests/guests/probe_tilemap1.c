/* M29 regression probe: a 1-bit TILEMAP whose cells and tiles the guest
 * fills in RAM — the same defect as probe_tm4, on the 1-bit path.
 *
 * The device walks the cell words in MAP_ADDR and fetches each tile from
 * TILE_ADDR; neither read is visible to the compiler, so without the M29
 * draw barrier in programs/gfx.h the fills can be deleted and the map
 * draws nothing at all.
 *
 * 2x2 cells of 8x8 pixels = 256 drawn pixels. Cell words carry the tile
 * index in the low half and the drawing colour in the high half (the 1-bit
 * path takes no colour argument; see programs/gfx.h gfx_tilemap).
 */
#include "arcade1.h"

#define RGB(r, g, b) (unsigned)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31))

#define TCOLS 2
#define TROWS 2
#define TW    8
#define TH    8
#define NTILE 2                              /* tile 0 unused, tile 1 used */

static unsigned      tcells[TCOLS * TROWS]; /* filled at run time (RAM)   */
static unsigned char ttile[NTILE * TH * ((TW + 7) / 8)];

int main(void)
{
    int r, c, t;

    for (t = 0; t < (int)(sizeof ttile); t++)
        ttile[t] = 0xFF;                    /* tile 1 = every bit set     */

    for (r = 0; r < TROWS; r++)
        for (c = 0; c < TCOLS; c++)
            tcells[r * TCOLS + c] = 1u | (RGB(31, 31, 31) << 16);

    for (;;) {
        arcade1_clear(RGB(0, 0, 0));
        arcade1_tilemap(100, 40, tcells, ttile, TCOLS, TROWS, TW, TH);
        arcade1_present();
    }
}
