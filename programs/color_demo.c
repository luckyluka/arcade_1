/* ARCADE-1 milestone 27 guest program — paletted 4-bit colour.
 *
 * M26 delivered the v0.2 memory map; M27 adds paletted colour graphics: a
 * 256-entry RGB565 palette, plus SPRITE4 and TILEMAP4 that draw 4-bit images
 * (two pixels per byte, HIGH nibble first, even width) coloured from a palette
 * bank with per-sprite/per-cell flips (docs.md "Color decisions, pinned at
 * M27"). The existing 1-bit commands are unchanged and still form the HUD.
 *
 * This demo exercises every new feature together (the harness
 * tests/test_color.c checks the result):
 *
 *   - a scrolling TILEMAP4 world (40x28 cells of 16px = 640x448, larger than
 *     the 480x270 screen) of four 16x16 4-bit tiles, with a per-cell palette
 *     bank (0 = power-on default, 1 = a custom bank) and per-cell X/Y flips;
 *   - three 4-bit SPRITE4 sprites: one on bank 2, one flipped in X, and one
 *     drawn from a different palette bank (bank 1);
 *   - a HUD rectangle using the OLD 1-bit gfx_rect (screen-space, not
 *     scrolled), proving the 1-bit and 4-bit paths coexist.
 *
 * Frame protocol: build palette/tiles/map once, then loop forever — set the
 * camera, draw, present. The machine runs one iteration per host frame, so the
 * camera advances deterministically one step per frame.
 */

#include "gfx.h"

#define TILE_W 16
#define TILE_H 16
#define MAP_COLS 40
#define MAP_ROWS 28

#define WORLD_W (MAP_COLS * TILE_W)   /* 640 */
#define WORLD_H (MAP_ROWS * TILE_H)   /* 448 */
#define VIEW_W  480
#define VIEW_H  270
#define MAXCX   (WORLD_W - VIEW_W)    /* 160 */
#define MAXCY   (WORLD_H - VIEW_H)    /* 178 */

/* Background the empty cells and the clear leave showing. */
#define COLOR_BG 0x0000u

/* HUD rectangle (screen-space, never scrolled). A unique teal that appears in
 * no palette, so the harness can isolate it exactly. */
#define HUD_X 8
#define HUD_Y 8
#define HUD_W 60
#define HUD_H 12
#define COLOR_HUD 0x5642u

/* World-space sprites (pan with the camera). All are 16x16 4-bit discs. */
#define SPR_A_X 120
#define SPR_A_Y 90
#define SPR_B_X 300
#define SPR_B_Y 180
#define SPR_C_X 430
#define SPR_C_Y 250

/* Custom palette bank 1 (entries 16..31) and bank 2 (entries 32..47). Bank 0
 * is left at the power-on default. The harness checks that bank 0's blue,
 * bank 1's orange and bank 2's sprite colour all appear, so these tables are
 * kept in sync with tests/test_color.c. */
static const unsigned short pal1[16] = {
    0x0000u, 0xFD20u, 0x39E7u, 0x9E66u, 0xFC18u, 0xA6FFu, 0xC618u, 0xFABFu,
    0x52AAu, 0x57EAu, 0x57FFu, 0xFAAAu, 0xFFE0u, 0x5AD9u, 0xB596u, 0xC638u,
};
static const unsigned short pal2[16] = {
    0x0000u, 0x8400u, 0x8540u, 0x8680u, 0x8010u, 0x87C0u, 0x8180u, 0x8280u,
    0x8340u, 0x8480u, 0x8500u, 0x8600u, 0x8700u, 0x8011u, 0x8012u, 0x8013u,
};

/* Buffers built once into guest RAM (the world is larger than ROM). */
static unsigned char tiles_ram[4][128];            /* 4 tiles, 8 bytes/row  */
static unsigned char sprite_ram[128];              /* one 16x16 4-bit disc  */
static unsigned int  cells[MAP_ROWS * MAP_COLS];   /* 4-byte TILEMAP4 cells  */

/* Pack a 4-bit nibble into a 4-bit image row (HIGH nibble = even column). */
static void put_nib(unsigned char *rowbytes, int col, int nib)
{
    int bi = col / 2;
    if (col & 1)
        rowbytes[bi] = (unsigned char)((rowbytes[bi] & 0xF0) | (nib & 0x0F));
    else
        rowbytes[bi] = (unsigned char)((rowbytes[bi] & 0x0F) | ((nib & 0x0F) << 4));
}

/* Four 16x16 tiles: a 1px border of nibble 15, interior of nibble t+1. */
static void build_tiles(void)
{
    int t, r, c, i;
    for (i = 0; i < 4 * 128; i++)
        ((unsigned char *)tiles_ram)[i] = 0u;
    for (t = 0; t < 4; t++)
        for (r = 0; r < 16; r++)
            for (c = 0; c < 16; c++)
                put_nib(&tiles_ram[t][r * 8], c,
                        (r == 0 || r == 15 || c == 0 || c == 15) ? 15 : (t + 1));
}

/* One 16x16 4-bit disc: nibble 4 inside a radius-6 circle, transparent (0)
 * outside. The colour depends on the bank the sprite is drawn from. */
static void build_sprites(void)
{
    int r, c, i;
    for (i = 0; i < 128; i++)
        sprite_ram[i] = 0u;
    for (r = 0; r < 16; r++)
        for (c = 0; c < 16; c++) {
            int dx = c - 8, dy = r - 8;
            if (dx * dx + dy * dy <= 36)
                put_nib(&sprite_ram[r * 8], c, 4);
        }
}

/* The world map. Border ring = tile 2 on bank 0; interior cells tile 1..4 on
 * alternating banks 0/1 with occasional flips; every 6th row/col is empty. */
static void build_map(void)
{
    int col, row;
    for (row = 0; row < MAP_ROWS; row++) {
        for (col = 0; col < MAP_COLS; col++) {
            unsigned int cell;
            if (col == 0 || row == 0 || col == MAP_COLS - 1 || row == MAP_ROWS - 1) {
                cell = 2u;                                  /* border, bank 0 */
            } else if (col % 6 == 0 && row % 6 == 0) {
                cell = 0u;                                  /* empty -> bg    */
            } else {
                unsigned int tile = 1u + (unsigned int)((col + row) % 4);
                unsigned int bank = (unsigned int)((col / 2 + row / 2) % 2);
                unsigned int fx   = (col % 5 == 0) ? 1u : 0u;
                unsigned int fy   = (row % 5 == 0) ? 1u : 0u;
                cell = tile | (bank << 16) | (fx << 20) | (fy << 21);
            }
            cells[row * MAP_COLS + col] = cell;
        }
    }
}

/* Triangular-wave bounce of v within [0, maxv] (deterministic full sweep). */
static int bounce(int v, int maxv)
{
    int period = maxv * 2;
    if (period <= 0)
        return 0;
    v %= period;
    if (v < 0)
        v += period;
    return (v <= maxv) ? v : (period - v);
}

static void draw(void)
{
    gfx_clear(COLOR_BG);
    /* World-space 4-bit map (camera pans it; per-cell bank + flips). */
    gfx_tilemap4(0, 0, cells, tiles_ram, MAP_COLS, MAP_ROWS, TILE_W, TILE_H);
    /* World-space 4-bit sprites: A on bank 2, B flipped in X on bank 2,
     * C on a different bank (bank 1). */
    gfx_sprite4(SPR_A_X, SPR_A_Y, sprite_ram, 16, 16, 2, 0);
    gfx_sprite4(SPR_B_X, SPR_B_Y, sprite_ram, 16, 16, 2, 1);
    gfx_sprite4(SPR_C_X, SPR_C_Y, sprite_ram, 16, 16, 1, 0);
    /* Screen-space HUD using the old 1-bit command (never scrolled). */
    gfx_rect(HUD_X, HUD_Y, HUD_W, HUD_H, COLOR_HUD);
    gfx_present();
}

int main(void)
{
    int frame = 0;

    gfx_pal_upload(16, pal1, 16);   /* bank 1 */
    gfx_pal_upload(32, pal2, 16);   /* bank 2 */
    build_tiles();
    build_sprites();
    build_map();
    for (;;) {
        gfx_camera(bounce(frame * 5, MAXCX), bounce(frame * 3, MAXCY));
        draw();
        frame++;
    }
    return 0;
}
