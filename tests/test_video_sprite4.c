/* ARCADE-1 — M27 step 2 test harness (SPRITE4 paletted 4-bit blit).
 *
 * Proves docs.md "Color decisions, pinned at M27": the SPRITE4 command draws a
 * 4-bit image (two pixels per byte, HIGH nibble first, even width) from a
 * palette bank with optional X/Y flips, world-space + camera, clipped. This
 * harness drives MMIO directly and re-verifies the ENTIRE framebuffer against
 * an independent model of the documented semantics for each case:
 *
 *   - transparency (nibble 0 leaves the background);
 *   - palette bank (PAL_BANK 0 = power-on default, bank 1 = custom);
 *   - flip X, flip Y, and flip X+Y (via FLAGS);
 *   - clipping at all four screen edges and with the camera;
 *   - the even-width rule (odd W / zero size draw nothing);
 *   - an illegal source (the MMIO window) draws nothing and leaves no strays;
 *   - reset restores the power-on palette/flags/bank and a black screen.
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
#define ADDR_ADDR      (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_ADDR)
#define ADDR_W         (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_W)
#define ADDR_H         (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_H)
#define ADDR_CAM_X     (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_CAM_X)
#define ADDR_CAM_Y     (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_CAM_Y)
#define ADDR_PAL_INDEX (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_PAL_INDEX)
#define ADDR_PAL_DATA  (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_PAL_DATA)
#define ADDR_PAL_BANK  (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_PAL_BANK)
#define ADDR_FLAGS     (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_FLAGS)

#define CMD_CLEAR   1u
#define CMD_SPRITE4 7u

/* Power-on default palette entries 0..15 (in sync with video.c). */
static const uint16_t def[16] = {
    0x0000u, 0x001Fu, 0x03E0u, 0x03FFu, 0xF800u, 0xF81Fu, 0xAAA0u, 0xAD55u,
    0x52AAu, 0x52BFu, 0x57EAu, 0x57FFu, 0xFAAAu, 0xFABFu, 0xFFE0u, 0xFFFFu,
};

/* Expected colour of palette value v under bank b. Bank 0 is the power-on
 * default; bank 1 was programmed in this test to entry 16+v = 0x1000+v. */
static uint16_t pal_of(int bank, int v)
{
    return (bank == 0) ? def[v] : (uint16_t)(0x1000u + (unsigned)v);
}

/* A 6x4 4-bit image (even width 6 -> 3 bytes/row). HIGH nibble = left pixel.
 * Values include 0s (transparent) interleaved so flips are meaningful. */
static const uint8_t img[12] = {
    0x01, 0x23, 0x45,
    0x67, 0x89, 0xAB,
    0x01, 0x02, 0x03,
    0x40, 0x50, 0x60,
};
#define IMG_W 6
#define IMG_H 4

/* Image pixel value at source column x, row y (HIGH nibble first). */
static int img_nib(int x, int y)
{
    const uint8_t b = img[y * (IMG_W / 2) + x / 2];
    return (x & 1) ? (b & 0x0F) : (b >> 4);
}

#define PIX(x, y) ((unsigned)(y) * (unsigned)ARCADE1_WIDTH + (unsigned)(x))

/* Copy the image into guest RAM word-by-word (little-endian). */
static void put_image(arcade1_machine_t *m)
{
    for (unsigned i = 0; i < sizeof img; i += 4) {
        uint32_t w = 0;
        for (unsigned j = 0; j < 4 && i + j < sizeof img; j++)
            w |= (uint32_t)img[i + j] << (8 * j);
        arcade1_write32(&m->bus, ARCADE1_RAM_BASE + i, w);
    }
}

/* Clear the screen to `bg`, then issue one SPRITE4 with the given world
 * origin, size, palette bank, flags and camera. */
static void draw(arcade1_machine_t *m, int X, int Y, int W, int H,
                 int bank, int flags, int camx, int camy, uint16_t bg)
{
    arcade1_write32(&m->bus, ADDR_COLOR, bg);
    arcade1_write32(&m->bus, ADDR_CMD, CMD_CLEAR);
    arcade1_write32(&m->bus, ADDR_CAM_X, (uint32_t)camx);
    arcade1_write32(&m->bus, ADDR_CAM_Y, (uint32_t)camy);
    arcade1_write32(&m->bus, ADDR_ADDR, ARCADE1_RAM_BASE);
    arcade1_write32(&m->bus, ADDR_X, (uint32_t)X);
    arcade1_write32(&m->bus, ADDR_Y, (uint32_t)Y);
    arcade1_write32(&m->bus, ADDR_W, (uint32_t)W);
    arcade1_write32(&m->bus, ADDR_H, (uint32_t)H);
    arcade1_write32(&m->bus, ADDR_PAL_BANK, (uint32_t)bank);
    arcade1_write32(&m->bus, ADDR_FLAGS, (uint32_t)flags);
    arcade1_write32(&m->bus, ADDR_CMD, CMD_SPRITE4);
}

/* Verify the WHOLE framebuffer against the documented SPRITE4 semantics for
 * the last draw() call. Every screen pixel must equal the palette colour of
 * its source nibble (or the background if transparent / outside the sprite). */
static void verify(arcade1_machine_t *m, int X, int Y, int W, int H,
                   int bank, int flags, int camx, int camy, uint16_t bg)
{
    const uint16_t *fb = arcade1_framebuffer(m);
    const int flip_x = flags & 1, flip_y = flags & 2;
    const int sx0 = X - camx, sy0 = Y - camy;   /* screen origin */

    for (int py = 0; py < (int)ARCADE1_HEIGHT; py++) {
        for (int px = 0; px < (int)ARCADE1_WIDTH; px++) {
            const int c = px - sx0, r = py - sy0;
            uint16_t want = bg;
            if (c >= 0 && c < W && r >= 0 && r < H) {
                const int sc = flip_x ? (W - 1 - c) : c;
                const int sr = flip_y ? (H - 1 - r) : r;
                const int nib = img_nib(sc, sr);   /* W,H match the image */
                want = nib ? pal_of(bank, nib) : bg;
            }
            assert(fb[PIX(px, py)] == want && "SPRITE4 pixel mismatch");
        }
    }
}

/* Full-screen scan asserting every pixel equals `c` (for the "drew nothing"
 * cases where the image size is invalid). */
static void assert_all(arcade1_machine_t *m, uint16_t c, const char *what)
{
    const uint16_t *fb = arcade1_framebuffer(m);
    for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
        assert(fb[i] == c && what);
}

int main(void)
{
    static arcade1_machine_t m;   /* Static: embeds the buffers. */
    const uint16_t bg = 0x5678u;  /* a background no palette entry equals */

    arcade1_init(&m);
    arcade1_reset(&m);
    put_image(&m);

    /* Program palette bank 1 (entries 16..31) to 0x1000+v; bank 0 is the
     * power-on default. */
    arcade1_write32(&m.bus, ADDR_PAL_INDEX, 16u);
    for (unsigned v = 0; v < 16u; v++)
        arcade1_write32(&m.bus, ADDR_PAL_DATA, 0x1000u + v);

    /* --- Basic draw + transparency (bank 0, no flip) ---------------------- */
    draw(&m, 10, 20, IMG_W, IMG_H, 0, 0, 0, 0, bg);
    verify(&m, 10, 20, IMG_W, IMG_H, 0, 0, 0, 0, bg);

    /* --- Palette bank 1 --------------------------------------------------- */
    draw(&m, 10, 20, IMG_W, IMG_H, 1, 0, 0, 0, bg);
    verify(&m, 10, 20, IMG_W, IMG_H, 1, 0, 0, 0, bg);

    /* --- Flip X, flip Y, flip X+Y ----------------------------------------- */
    draw(&m, 10, 20, IMG_W, IMG_H, 0, 1, 0, 0, bg);
    verify(&m, 10, 20, IMG_W, IMG_H, 0, 1, 0, 0, bg);
    draw(&m, 10, 20, IMG_W, IMG_H, 0, 2, 0, 0, bg);
    verify(&m, 10, 20, IMG_W, IMG_H, 0, 2, 0, 0, bg);
    draw(&m, 10, 20, IMG_W, IMG_H, 0, 3, 0, 0, bg);
    verify(&m, 10, 20, IMG_W, IMG_H, 0, 3, 0, 0, bg);

    /* --- Clipping at all four screen edges -------------------------------- */
    draw(&m, -2, -1, IMG_W, IMG_H, 0, 0, 0, 0, bg);         /* top-left    */
    verify(&m, -2, -1, IMG_W, IMG_H, 0, 0, 0, 0, bg);
    draw(&m, (int)ARCADE1_WIDTH - 3, -1, IMG_W, IMG_H, 0, 0, 0, 0, bg); /* TR */
    verify(&m, (int)ARCADE1_WIDTH - 3, -1, IMG_W, IMG_H, 0, 0, 0, 0, bg);
    draw(&m, -2, (int)ARCADE1_HEIGHT - 2, IMG_W, IMG_H, 0, 0, 0, 0, bg); /* BL*/
    verify(&m, -2, (int)ARCADE1_HEIGHT - 2, IMG_W, IMG_H, 0, 0, 0, 0, bg);
    draw(&m, (int)ARCADE1_WIDTH - 3, (int)ARCADE1_HEIGHT - 2,
         IMG_W, IMG_H, 0, 0, 0, 0, bg);                      /* bottom-right */
    verify(&m, (int)ARCADE1_WIDTH - 3, (int)ARCADE1_HEIGHT - 2,
           IMG_W, IMG_H, 0, 0, 0, 0, bg);
    /* Fully off-screen: nothing drawn. */
    draw(&m, (int)ARCADE1_WIDTH + 5, 10, IMG_W, IMG_H, 0, 0, 0, 0, bg);
    assert_all(&m, bg, "fully off-screen SPRITE4 must draw nothing");

    /* --- Flipped and clipped together ------------------------------------- */
    draw(&m, -2, -1, IMG_W, IMG_H, 0, 3, 0, 0, bg);
    verify(&m, -2, -1, IMG_W, IMG_H, 0, 3, 0, 0, bg);

    /* --- Camera shift ------------------------------------------------------ */
    draw(&m, 20, 30, IMG_W, IMG_H, 0, 0, 5, 3, bg);   /* screen origin (15,27) */
    verify(&m, 20, 30, IMG_W, IMG_H, 0, 0, 5, 3, bg);

    /* --- Even-width rule: odd W / zero size draw nothing ------------------ */
    draw(&m, 10, 20, 3, IMG_H, 0, 0, 0, 0, bg);       /* odd width */
    assert_all(&m, bg, "odd-width SPRITE4 must draw nothing");
    draw(&m, 10, 20, 0, IMG_H, 0, 0, 0, 0, bg);       /* zero width */
    assert_all(&m, bg, "zero-width SPRITE4 must draw nothing");
    draw(&m, 10, 20, IMG_W, 0, 0, 0, 0, 0, bg);          /* zero height */
    assert_all(&m, bg, "zero-height SPRITE4 must draw nothing");

    /* --- Illegal source (the MMIO window) draws nothing, no side effects --- */
    arcade1_write32(&m.bus, ADDR_COLOR, bg);
    arcade1_write32(&m.bus, ADDR_CMD, CMD_CLEAR);
    arcade1_write32(&m.bus, ADDR_ADDR, ARCADE1_MMIO_BASE);
    arcade1_write32(&m.bus, ADDR_X, 10u);
    arcade1_write32(&m.bus, ADDR_Y, 20u);
    arcade1_write32(&m.bus, ADDR_W, IMG_W);
    arcade1_write32(&m.bus, ADDR_H, IMG_H);
    arcade1_write32(&m.bus, ADDR_PAL_BANK, 0u);
    arcade1_write32(&m.bus, ADDR_FLAGS, 0u);
    arcade1_write32(&m.bus, ADDR_CMD, CMD_SPRITE4);
    assert_all(&m, bg, "illegal source must draw nothing");
    assert(m.bus.stray_accesses == 0 &&
           "device fetches must not reach the bus (strays stay 0)");

    /* --- Register readbacks ------------------------------------------------ */
    arcade1_write32(&m.bus, ADDR_PAL_BANK, 5u);
    arcade1_write32(&m.bus, ADDR_FLAGS, 3u);
    assert(arcade1_read32(&m.bus, ADDR_PAL_BANK) == 5u && "PAL_BANK readback");
    assert(arcade1_read32(&m.bus, ADDR_FLAGS) == 3u && "FLAGS readback");
    /* PAL_BANK/FLAGS are masked. */
    arcade1_write32(&m.bus, ADDR_PAL_BANK, 0xFFu);
    arcade1_write32(&m.bus, ADDR_FLAGS, 0xFFu);
    assert(arcade1_read32(&m.bus, ADDR_PAL_BANK) == 0xFu && "PAL_BANK masked");
    assert(arcade1_read32(&m.bus, ADDR_FLAGS) == 0x3u && "FLAGS masked");

    /* --- Reset restores power-on palette/bank/flags and a black screen ----- */
    arcade1_reset(&m);
    assert(arcade1_read32(&m.bus, ADDR_PAL_BANK) == 0u && "reset clears bank");
    assert(arcade1_read32(&m.bus, ADDR_FLAGS) == 0u && "reset clears flags");
    {
        const uint16_t *fb = arcade1_framebuffer(&m);
        for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
            assert(fb[i] == 0u && "reset restores a black screen");
    }

    printf("milestone27-step2: all assertions passed\n");
    printf("  verified: SPRITE4 transparency, palette bank, flip X/Y/XY,\n"
           "            clipping (4 edges + camera), even-width rule, illegal\n"
           "            source, register masking, reset determinism\n");
    return 0;
}
