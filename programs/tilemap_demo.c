/* ARCADE-1 milestone 20 guest program — the tilemap engine.
 *
 * Milestone 19 drew 1-bpp sprites one object at a time. Both shipped games
 * still repaint a *grid* cell-by-cell (Tetris' 10x20 field, Breakout's
 * 5x10 bricks), so M20 adds a TILEMAP command that draws a whole COLSxROWS
 * grid of tiles in one shot (docs.md M20). Each cell is one 32-bit word:
 * a tile index (0 = empty) plus a per-cell RGB565 tint — the one tile
 * attribute both games need.
 *
 * The harness (tests/test_tilemap.c) re-renders every pixel from an
 * independent model, so this demo pins the semantics deliberately:
 *
 *   - multiple distinct tiles (4) at an 8x8 tile size (1 byte/row);
 *   - per-cell colour: the same tile re-tinted across the grid;
 *   - empty cells (index 0) leave the background showing;
 *   - clipping: hangs off the right/bottom edge, and a negative origin
 *     (top-left off-screen) shifts which tile pixels land on screen;
 *   - cells from ROM and cells from a RAM copy render identically;
 *   - a cells pointer outside ROM/RAM (the MMIO window) draws nothing and
 *     must not disturb any device;
 *   - guards: cols=0, rows=0 and tile size 0 all draw nothing.
 *
 * Layout (all three map draws occupy disjoint screen regions, so the
 * framebuffer is a clean composition):
 *   gfx_clear(black)                          background
 *   gfx_tilemap(20, 30, cells_rom, ...)       on-screen map
 *   gfx_tilemap(-10, -6, cells_rom, ...)      negative origin
 *   gfx_tilemap(300, 220, cells_rom, ...)     right/bottom overhang
 *   gfx_tilemap(250, 100, ARCADE1_MMIO_BASE, ...)  illegal source -> nothing
 *   gfx_tilemap(20, 100, cells_ram, ...)      RAM copy of the same map
 *   gfx_tilemap(..., 0, ...) x3               guards -> nothing
 *   gfx_present()                             one present, counted by the device
 */

#include "gfx.h"

/* Background: a flat grey, so "empty cell" and "untouched screen" are the
 * same colour and transparency is unambiguous against the bright tiles. */
#define COLOR_BG 0x8410u

#define COLOR_R 0xF800u   /* red     */
#define COLOR_G 0x07E0u   /* green   */
#define COLOR_B 0x001Fu   /* blue    */
#define COLOR_W 0xFFFFu   /* white   */
#define COLOR_Y 0xFFE0u   /* yellow  */
#define COLOR_M 0xF81Fu   /* magenta */
#define COLOR_C 0x07FFu   /* cyan    */

#define TILE_W 8
#define TILE_H 8
#define MAP_COLS 6
#define MAP_ROWS 4

/* A cell word: low 16 bits = tile index, high 16 bits = RGB565 tint. */
#define CELL(idx, rgb) ((unsigned int)(idx) | ((unsigned int)(rgb) << 16))

/* Four 1-bpp tiles, 8x8, one byte per row, MSB = leftmost pixel.
 *   tile 1  hollow box outline
 *   tile 2  solid
 *   tile 3  top-left -> bottom-right diagonal
 *   tile 4  plus / cross
 */
static const unsigned char tiles[4][8] = {
    { 0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF },  /* box    */
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },  /* solid  */
    { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 },  /* diag   */
    { 0x10, 0x10, 0x10, 0xFF, 0x10, 0x10, 0x10, 0x10 },  /* cross  */
};

/* The map: every tile, every colour, and empty cells scattered through it.
 * Kept in ROM (const -> .rodata); the harness mirrors it cell-for-cell. */
static const unsigned int cells_rom[MAP_ROWS * MAP_COLS] = {
    CELL(1, COLOR_R), CELL(2, COLOR_G), CELL(0, 0),       CELL(3, COLOR_B),
    CELL(4, COLOR_W), CELL(1, COLOR_Y),
    CELL(0, 0),       CELL(2, COLOR_M), CELL(3, COLOR_C), CELL(0, 0),
    CELL(1, COLOR_R), CELL(2, COLOR_G),
    CELL(3, COLOR_B), CELL(0, 0),       CELL(4, COLOR_W), CELL(1, COLOR_Y),
    CELL(0, 0),       CELL(2, COLOR_M),
    CELL(4, COLOR_C), CELL(1, COLOR_R), CELL(2, COLOR_G), CELL(0, 0),
    CELL(3, COLOR_B), CELL(4, COLOR_W),
};

/* A RAM copy of the same map: proves the device fetches cells from RAM
 * exactly as it does from ROM. */
static unsigned int cells_ram[MAP_ROWS * MAP_COLS];

int main(void)
{
    int i;

    for (i = 0; i < MAP_ROWS * MAP_COLS; i++)
        cells_ram[i] = cells_rom[i];

    gfx_clear(COLOR_BG);

    /* On-screen map. */
    gfx_tilemap(20, 30, cells_rom, tiles, MAP_COLS, MAP_ROWS, TILE_W, TILE_H);

    /* Negative origin: the top-left of the map is off-screen, so the first
     * tiles/columns clip and only their on-screen part lands. */
    gfx_tilemap(-10, -6, cells_rom, tiles, MAP_COLS, MAP_ROWS, TILE_W, TILE_H);

    /* Hangs off the right and bottom edges; only the on-screen tiles (and
     * their on-screen columns/rows) are drawn. */
    gfx_tilemap(300, 220, cells_rom, tiles, MAP_COLS, MAP_ROWS, TILE_W,
                TILE_H);

    /* Cells pointer outside ROM/RAM (the MMIO window): every cell fetches
     * as 0 = empty, so nothing is drawn and no device is disturbed. */
    gfx_tilemap(250, 100, (const void *)ARCADE1_MMIO_BASE, tiles, MAP_COLS, MAP_ROWS,
                TILE_W, TILE_H);

    /* Same map from RAM must render identically to the ROM original (the
     * harness compares these two screen regions pixel-for-pixel). */
    gfx_tilemap(20, 100, cells_ram, tiles, MAP_COLS, MAP_ROWS, TILE_W,
                TILE_H);

    /* Guards: a zero dimension draws nothing (and leaves the registers of
     * the last real command intact). */
    gfx_tilemap(100, 180, cells_rom, tiles, 0,          MAP_ROWS, TILE_W,
                TILE_H);
    gfx_tilemap(100, 180, cells_rom, tiles, MAP_COLS,   0,        TILE_W,
                TILE_H);
    gfx_tilemap(100, 180, cells_rom, tiles, MAP_COLS,   MAP_ROWS, 0,
                TILE_H);

    gfx_present();
    return 0;
}
