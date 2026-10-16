/* ARCADE-1 milestone 21 guest program — camera scrolling.
 *
 * M20 drew a tilemap at a fixed on-screen origin. Both future games need a
 * CAMERA: a big world where only a 320x240 window is visible, and the guest
 * pans that window (docs.md M21). The two WORLD-space commands — SPRITE and
 * TILEMAP — are drawn by the device at (x - cam_x, y - cam_y), so a guest
 * keeps objects at fixed world coordinates and only the camera moves. The
 * primitives (gfx_clear / gfx_rect / gfx_set_pixel) stay screen-space and are
 * never scrolled: they are the HUD.
 *
 * The harness (tests/test_scroll.c) re-renders EVERY pixel from an independent
 * model that reads the demo's real map out of guest RAM and its real tiles out
 * of guest ROM, so this demo pins the semantics deliberately:
 *
 *   - a map LARGER than the screen (32x22 cells of 16px = 512x352 world), so
 *     at any camera only a window of it is visible — the device WINDOWs the
 *     grid (only visible columns/rows are visited) and the model visits every
 *     cell then clips, proving windowing changes no pixel;
 *   - horizontal AND vertical scrolling: the camera sweeps a triangular wave
 *     over both axes at different rates;
 *   - a WORLD-space sprite at a fixed world position that pans with the
 *     camera;
 *   - a SCREEN-space HUD rectangle that does NOT move with the camera;
 *   - three distinct tiles (solid / box / diagonal) and per-cell colour, so
 *     any scroll offset visibly shifts the pattern.
 *
 * Frame protocol: build the map once in RAM, then loop forever — set the
 * camera for this frame, draw, present. The machine runs one loop iteration
 * per host frame (docs.md M15 present-yield), so the camera advances
 * deterministically one step per frame.
 */

#include "gfx.h"

#define TILE_W 16
#define TILE_H 16
#define MAP_COLS 32
#define MAP_ROWS 22

/* World size in pixels and the camera's scroll range (world minus view). */
#define WORLD_W (MAP_COLS * TILE_W)   /* 512 */
#define WORLD_H (MAP_ROWS * TILE_H)   /* 352 */
#define MAXCX   (WORLD_W - 320)       /* 192 */
#define MAXCY   (WORLD_H - 240)       /* 112 */

/* Background the empty cells and the clear leave showing. */
#define COLOR_BG 0x0000u

/* HUD rectangle (screen-space, never scrolled). Orange, distinct from the
 * map palette so its fixed position is easy to check. */
#define HUD_X 8
#define HUD_Y 8
#define HUD_W 48
#define HUD_H 10
#define COLOR_HUD 0xFD20u

/* World-space sprite (pans with the camera): a 16x16 plus sign, white. */
#define SPRITE_X 200
#define SPRITE_Y 150
#define COLOR_SPR 0xFFFFu

/* Three 16x16 1-bpp tiles: 2 bytes per row, 32 bytes per tile, MSB = leftmost.
 *   tile 1  solid
 *   tile 2  box outline
 *   tile 3  top-left -> bottom-right diagonal
 */
static const unsigned char tiles[3][32] = {
    {   /* 1: solid */
        0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
        0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
        0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
        0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
    },
    {   /* 2: box outline */
        0xFF,0xFF, 0x80,0x01, 0x80,0x01, 0x80,0x01,
        0x80,0x01, 0x80,0x01, 0x80,0x01, 0x80,0x01,
        0x80,0x01, 0x80,0x01, 0x80,0x01, 0x80,0x01,
        0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
    },
    {   /* 3: diagonal */
        0x80,0x00, 0x40,0x00, 0x20,0x00, 0x10,0x00,
        0x08,0x00, 0x04,0x00, 0x02,0x00, 0x01,0x00,
        0x00,0x80, 0x00,0x40, 0x00,0x20, 0x00,0x10,
        0x00,0x08, 0x00,0x04, 0x00,0x02, 0x00,0x01,
    },
};

/* The 16x16 world-space sprite: a plus sign (col 7/8 all rows, row 7/8 all
 * cols), MSB = leftmost. */
static const unsigned char sprite_img[32] = {
    0x01,0x80, 0x01,0x80, 0x01,0x80, 0x01,0x80,
    0x01,0x80, 0x01,0x80, 0x01,0x80, 0xFF,0xFF,
    0xFF,0xFF, 0x01,0x80, 0x01,0x80, 0x01,0x80,
    0x01,0x80, 0x01,0x80, 0x01,0x80, 0x01,0x80,
};

/* The map, built once into RAM so the world is bigger than ROM. Each cell is
 * a 32-bit tilemap word: tile index | (RGB565 << 16). The harness reads these
 * cells straight out of guest RAM and feeds them to its own model. */
static unsigned int cells[MAP_ROWS * MAP_COLS];

static void build_map(void)
{
    static const unsigned short pal[6] = {
        0xF800u, 0x07E0u, 0x001Fu, 0xFFE0u, 0xF81Fu, 0x07FFu
    };   /* red green blue yellow magenta cyan */
    int col, row;

    for (row = 0; row < MAP_ROWS; row++) {
        for (col = 0; col < MAP_COLS; col++) {
            unsigned int cell;
            if (col == 0 || row == 0 ||
                col == MAP_COLS - 1 || row == MAP_ROWS - 1) {
                cell = 2u | (0xFFFFu << 16);        /* white box border      */
            } else if (col % 4 == 1 && row % 4 == 1) {
                cell = 0u;                          /* hole -> background    */
            } else {
                unsigned int tile = 1u + (unsigned int)((col + row) % 3);
                unsigned int colr = pal[(col / 2 + row / 2) % 6];
                cell = tile | (colr << 16);         /* tiling + colour block */
            }
            cells[row * MAP_COLS + col] = cell;
        }
    }
}

/* Triangular-wave bounce of v (already scaled) within [0, maxv]. Deterministic
 * and covers the full scroll range on each axis. */
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
    /* World-space map at the world origin (0,0); the camera pans it. Only the
     * visible window is drawn by the device. */
    gfx_tilemap(0, 0, cells, tiles, MAP_COLS, MAP_ROWS, TILE_W, TILE_H);
    /* World-space sprite: fixed world position, pans with the camera. */
    gfx_sprite(SPRITE_X, SPRITE_Y, sprite_img, TILE_W, TILE_H, COLOR_SPR);
    /* Screen-space HUD: NOT scrolled. */
    gfx_rect(HUD_X, HUD_Y, HUD_W, HUD_H, COLOR_HUD);
    gfx_present();
}

int main(void)
{
    int frame = 0;

    build_map();
    for (;;) {
        /* X sweeps at one rate, Y at another, so both axes scroll. */
        gfx_camera(bounce(frame * 5, MAXCX), bounce(frame * 3, MAXCY));
        draw();
        frame++;
    }
    return 0;
}
