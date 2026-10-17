/* ARCADE-1 — M27 step 3 test harness (TILEMAP4 paletted tile grid).
 *
 * Proves docs.md "Color decisions, pinned at M27": the TILEMAP4 command draws a
 * COLSxROWS grid of 4-bit tiles, each cell carrying a per-cell palette bank and
 * X/Y flips ([15:0] index, [19:16] bank, [20] flipX, [21] flipY), with the same
 * windowing/camera/clipping rules as TILEMAP. The whole framebuffer is verified
 * against an independent model for: per-cell bank + flips, an empty cell,
 * windowing with the camera, cells at the screen edges, and the even-width
 * rule. PAL_BANK/FLAGS registers are proven irrelevant (the cell supplies them).
 */

#include "machine.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>

/* Absolute MMIO addresses of the registers used here. */
#define ADDR_X         (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_X)
#define ADDR_Y         (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_Y)
#define ADDR_COLOR     (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_COLOR)
#define ADDR_CMD       (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_CMD)
#define ADDR_W         (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_W)
#define ADDR_H         (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_H)
#define ADDR_MAP_ADDR  (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_MAP_ADDR)
#define ADDR_TILE_ADDR (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_TILE_ADDR)
#define ADDR_COLS      (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_COLS)
#define ADDR_ROWS      (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_ROWS)
#define ADDR_CAM_X     (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_CAM_X)
#define ADDR_CAM_Y     (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_CAM_Y)
#define ADDR_PAL_INDEX (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_PAL_INDEX)
#define ADDR_PAL_DATA  (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_PAL_DATA)
#define ADDR_PAL_BANK  (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_PAL_BANK)
#define ADDR_FLAGS     (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_FLAGS)

#define CMD_CLEAR    1u
#define CMD_TILEMAP4 8u

/* Power-on default palette entries 0..15 (in sync with video.c). */
static const uint16_t def[16] = {
    0x0000u, 0x001Fu, 0x03E0u, 0x03FFu, 0xF800u, 0xF81Fu, 0xAAA0u, 0xAD55u,
    0x52AAu, 0x52BFu, 0x57EAu, 0x57FFu, 0xFAAAu, 0xFABFu, 0xFFE0u, 0xFFFFu,
};

/* Expected colour of palette value v under bank b. Bank 0 = power-on default;
 * bank 1 = entries 16+v = 0x1000+v; bank 2 = entries 32+v = 0x2000+v. */
static uint16_t pal_of(int bank, int v)
{
    return (bank == 0) ? def[v] : (uint16_t)(bank * 0x1000u + (unsigned)v);
}

#define TILE_W 4
#define TILE_H 4
#define TILE_BYTES ((TILE_W / 2) * TILE_H)   /* 8 */
#define N_TILES 3
#define COLS 3
#define ROWS 2

/* Three 4x4 4-bit tiles (HIGH nibble = left pixel); tile 1 has a transparent
 * bottom row, tiles 2/3 have interleaved transparent pixels. */
static const uint8_t tiles[N_TILES][TILE_BYTES] = {
    { 0x11, 0x11, 0x22, 0x22, 0x33, 0x33, 0x00, 0x00 },
    { 0x40, 0x04, 0x05, 0x50, 0x06, 0x60, 0x70, 0x07 },
    { 0x88, 0x88, 0x99, 0x99, 0xAA, 0xAA, 0xBB, 0xBB },
};

/* The 3x2 cell grid: index | bank<<16 | flipX<<20 | flipY<<21. Cell (0,1) is
 * empty (index 0); others mix banks 0/1/2 and flips. */
static const uint32_t cells[COLS * ROWS] = {
    0x00000001u, 0x00110002u, 0x00200003u,
    0x00000000u, 0x00320001u, 0x00010002u,
};

/* Tile pixel value at source (x, y) of tile `t` (HIGH nibble first). */
static int tile_nib(int t, int x, int y)
{
    const uint8_t b = tiles[t][y * (TILE_W / 2) + x / 2];
    return (x & 1) ? (b & 0x0F) : (b >> 4);
}

#define PIX(x, y) ((unsigned)(y) * (unsigned)ARCADE1_WIDTH + (unsigned)(x))

/* Copy a byte array into guest RAM word-by-word (little-endian). */
static void put_bytes(arcade1_machine_t *m, uint32_t addr,
                      const uint8_t *b, unsigned n)
{
    for (unsigned i = 0; i < n; i += 4) {
        uint32_t w = 0;
        for (unsigned j = 0; j < 4 && i + j < n; j++)
            w |= (uint32_t)b[i + j] << (8 * j);
        arcade1_write32(&m->bus, addr + i, w);
    }
}

/* Clear to bg, then issue one TILEMAP4 at world origin (X,Y) with the given
 * camera and grid size. MAP/TILE sources point into guest RAM. PAL_BANK/FLAGS
 * are deliberately set to junk: TILEMAP4 must ignore them (cells supply them). */
static void draw(arcade1_machine_t *m, int X, int Y, int camx, int camy,
                 int w, int h, uint16_t bg)
{
    arcade1_write32(&m->bus, ADDR_COLOR, bg);
    arcade1_write32(&m->bus, ADDR_CMD, CMD_CLEAR);
    arcade1_write32(&m->bus, ADDR_PAL_BANK, 7u);   /* must be ignored */
    arcade1_write32(&m->bus, ADDR_FLAGS, 3u);      /* must be ignored */
    arcade1_write32(&m->bus, ADDR_CAM_X, (uint32_t)camx);
    arcade1_write32(&m->bus, ADDR_CAM_Y, (uint32_t)camy);
    arcade1_write32(&m->bus, ADDR_MAP_ADDR, ARCADE1_RAM_BASE + 0u);
    arcade1_write32(&m->bus, ADDR_TILE_ADDR, ARCADE1_RAM_BASE + 0x100u);
    arcade1_write32(&m->bus, ADDR_W, (uint32_t)w);
    arcade1_write32(&m->bus, ADDR_H, (uint32_t)h);
    arcade1_write32(&m->bus, ADDR_COLS, COLS);
    arcade1_write32(&m->bus, ADDR_ROWS, ROWS);
    arcade1_write32(&m->bus, ADDR_X, (uint32_t)X);
    arcade1_write32(&m->bus, ADDR_Y, (uint32_t)Y);
    arcade1_write32(&m->bus, ADDR_CMD, CMD_TILEMAP4);
}

/* Verify the whole framebuffer against the documented TILEMAP4 semantics for
 * the last draw() call (map origin = world minus camera). */
static void verify(arcade1_machine_t *m, int X, int Y, int camx, int camy,
                   uint16_t bg)
{
    const uint16_t *fb = arcade1_framebuffer(m);
    const int ox = X - camx, oy = Y - camy;

    for (int py = 0; py < (int)ARCADE1_HEIGHT; py++) {
        for (int px = 0; px < (int)ARCADE1_WIDTH; px++) {
            const int relx = px - ox, rely = py - oy;
            uint16_t want = bg;
            if (relx >= 0 && relx < COLS * TILE_W &&
                rely >= 0 && rely < ROWS * TILE_H) {
                const int cc = relx / TILE_W, cr = rely / TILE_H;
                const int c  = relx % TILE_W, r  = rely % TILE_H;
                const uint32_t cell = cells[cr * COLS + cc];
                const uint32_t index = cell & 0xFFFFu;
                if (index != 0u) {
                    const int bank = (int)((cell >> 16) & 0x0Fu);
                    const int fx = (int)((cell >> 20) & 1u);
                    const int fy = (int)((cell >> 21) & 1u);
                    const int sc = fx ? (TILE_W - 1 - c) : c;
                    const int sr = fy ? (TILE_H - 1 - r) : r;
                    const int nib = tile_nib((int)index - 1, sc, sr);
                    want = nib ? pal_of(bank, nib) : bg;
                }
            }
            assert(fb[PIX(px, py)] == want && "TILEMAP4 pixel mismatch");
        }
    }
}

int main(void)
{
    static arcade1_machine_t m;   /* Static: embeds the buffers. */
    const uint16_t bg = 0x5678u;  /* a background no palette entry equals */

    arcade1_init(&m);
    arcade1_reset(&m);

    /* Cells at RAM+0, tile set at RAM+0x100. */
    for (unsigned i = 0; i < COLS * ROWS; i++)
        arcade1_write32(&m.bus, ARCADE1_RAM_BASE + i * 4u, cells[i]);
    put_bytes(&m, ARCADE1_RAM_BASE + 0x100u,
              (const uint8_t *)tiles, sizeof tiles);

    /* Program palette bank 1 (entries 16..31 = 0x1000+v) and bank 2 (entries
     * 32..47 = 0x2000+v); bank 0 is the power-on default. */
    arcade1_write32(&m.bus, ADDR_PAL_INDEX, 16u);
    for (unsigned v = 0; v < 16u; v++)
        arcade1_write32(&m.bus, ADDR_PAL_DATA, 0x1000u + v);
    arcade1_write32(&m.bus, ADDR_PAL_INDEX, 32u);
    for (unsigned v = 0; v < 16u; v++)
        arcade1_write32(&m.bus, ADDR_PAL_DATA, 0x2000u + v);

    /* --- Basic map: per-cell bank + flips, an empty cell ------------------- */
    draw(&m, 8, 12, 0, 0, TILE_W, TILE_H, bg);
    verify(&m, 8, 12, 0, 0, bg);

    /* --- Camera shift (windowing origin moves) ---------------------------- */
    draw(&m, 20, 24, 4, 3, TILE_W, TILE_H, bg);
    verify(&m, 20, 24, 4, 3, bg);

    /* --- Negative origin: map hangs off the top-left (windowing) ---------- */
    draw(&m, -3, -2, 0, 0, TILE_W, TILE_H, bg);
    verify(&m, -3, -2, 0, 0, bg);

    /* --- Cells at the right/bottom screen edge ---------------------------- */
    draw(&m, (int)ARCADE1_WIDTH - 5, (int)ARCADE1_HEIGHT - 3, 0, 0,
         TILE_W, TILE_H, bg);
    verify(&m, (int)ARCADE1_WIDTH - 5, (int)ARCADE1_HEIGHT - 3, 0, 0, bg);

    /* --- Camera pushes most of the map off-screen (heavy windowing) ------- */
    draw(&m, 4, 6, 100, 100, TILE_W, TILE_H, bg);   /* fully off: bg only */
    {
        const uint16_t *fb = arcade1_framebuffer(&m);
        for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
            assert(fb[i] == bg && "fully windowed-out map must draw nothing");
    }
    draw(&m, 8, 12, 6, 4, TILE_W, TILE_H, bg);      /* partially visible */
    verify(&m, 8, 12, 6, 4, bg);

    /* --- Even-width rule: odd tile width draws nothing -------------------- */
    draw(&m, 8, 12, 0, 0, 5, TILE_H, bg);
    {
        const uint16_t *fb = arcade1_framebuffer(&m);
        for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
            assert(fb[i] == bg && "odd tile width must draw nothing");
    }

    /* --- Reset restores a black screen ------------------------------------ */
    arcade1_reset(&m);
    {
        const uint16_t *fb = arcade1_framebuffer(&m);
        for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
            assert(fb[i] == 0u && "reset restores a black screen");
    }

    printf("milestone27-step3: all assertions passed\n");
    printf("  verified: TILEMAP4 per-cell palette bank + flips, empty cell,\n"
           "            camera windowing (incl. fully off-screen), cells at\n"
           "            screen edges, even-width rule, PAL_BANK/FLAGS ignored\n");
    return 0;
}
