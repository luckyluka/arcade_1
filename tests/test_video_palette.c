/* ARCADE-1 — M27 step 1 test harness (video palette registers).
 *
 * Proves docs.md "Color decisions, pinned at M27": the video device gained a
 * 256-entry RGB565 palette addressed through PAL_INDEX/PAL_DATA. A PAL_DATA
 * write stores value&0xFFFF at the current index and auto-increments it
 * (wrapping 255 -> 0); a PAL_DATA read returns the current entry without
 * incrementing. The power-on palette is entry 0 = black, entries 1..15 = a
 * fixed 16-colour default, and entries 16..255 = black.
 *
 * This harness drives the machine's MMIO window directly (no guest program):
 * step 1 only concerns the palette registers, which no 1-bit command uses, so
 * the existing draw path is checked to be unaffected. SPRITE4/TILEMAP4 (which
 * consume the palette) are exercised in steps 2 and 3.
 */

#include "machine.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>

/* Absolute MMIO addresses of the registers used here. */
#define ADDR_PAL_INDEX (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_PAL_INDEX)
#define ADDR_PAL_DATA  (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_PAL_DATA)
#define ADDR_X         (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_X)
#define ADDR_Y         (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_Y)
#define ADDR_COLOR     (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_COLOR)
#define ADDR_CMD       (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_CMD)

/* The colour commands used for the "drawing still works" sanity check. */
#define CMD_CLEAR 1u
#define CMD_FILL  4u

#define COLOR_BLUE ((uint16_t)0x001Fu)
#define COLOR_RED  ((uint16_t)0xF800u)

/* The power-on default palette entries 0..15 (kept in sync by hand with
 * video.c; the harness must catch any drift). */
static const uint16_t def[16] = {
    0x0000u, 0x001Fu, 0x03E0u, 0x03FFu, 0xF800u, 0xF81Fu, 0xAAA0u, 0xAD55u,
    0x52AAu, 0x52BFu, 0x57EAu, 0x57FFu, 0xFAAAu, 0xFABFu, 0xFFE0u, 0xFFFFu,
};

/* Read palette entry i by setting the index then reading PAL_DATA. */
static uint16_t pal_read(arcade1_machine_t *m, uint32_t i)
{
    arcade1_write32(&m->bus, ADDR_PAL_INDEX, i);
    return (uint16_t)(arcade1_read32(&m->bus, ADDR_PAL_DATA) & 0xFFFFu);
}

/* Framebuffer index of pixel (x, y). */
#define PIX(x, y) ((unsigned)(y) * (unsigned)ARCADE1_WIDTH + (unsigned)(x))

int main(void)
{
    static arcade1_machine_t m;   /* Static: embeds the buffers. */

    arcade1_init(&m);
    arcade1_reset(&m);

    /* --- Power-on palette ------------------------------------------------- */
    assert(arcade1_read32(&m.bus, ADDR_PAL_INDEX) == 0u &&
           "PAL_INDEX starts at 0");
    for (uint32_t i = 0; i < 16u; i++)
        assert(pal_read(&m, i) == def[i] && "default palette entry 0..15");
    for (uint32_t i = 16u; i < 256u; i++)
        assert(pal_read(&m, i) == 0u && "palette entries 16..255 start black");

    /* --- Write sequence: auto-increment, readback, no-read-increment ------ */
    arcade1_write32(&m.bus, ADDR_PAL_INDEX, 0u);
    arcade1_write32(&m.bus, ADDR_PAL_DATA, 0x1111u);
    arcade1_write32(&m.bus, ADDR_PAL_DATA, 0x2222u);
    arcade1_write32(&m.bus, ADDR_PAL_DATA, 0x3333u);
    assert(arcade1_read32(&m.bus, ADDR_PAL_INDEX) == 3u &&
           "PAL_DATA writes must auto-increment PAL_INDEX");
    assert(pal_read(&m, 0u) == 0x1111u && "entry 0 written");
    assert(pal_read(&m, 1u) == 0x2222u && "entry 1 written");
    assert(pal_read(&m, 2u) == 0x3333u && "entry 2 written");

    /* A PAL_DATA read must NOT advance the index. */
    arcade1_write32(&m.bus, ADDR_PAL_INDEX, 5u);
    (void)arcade1_read32(&m.bus, ADDR_PAL_DATA);
    (void)arcade1_read32(&m.bus, ADDR_PAL_DATA);
    assert(arcade1_read32(&m.bus, ADDR_PAL_INDEX) == 5u &&
           "PAL_DATA reads must not auto-increment");

    /* PAL_INDEX is masked to 8 bits (256 entries). */
    arcade1_write32(&m.bus, ADDR_PAL_INDEX, 0x1FFu);
    assert(arcade1_read32(&m.bus, ADDR_PAL_INDEX) == 0xFFu &&
           "PAL_INDEX masks to 8 bits");

    /* --- Auto-increment wrap at 256 --------------------------------------- */
    arcade1_write32(&m.bus, ADDR_PAL_INDEX, 254u);
    arcade1_write32(&m.bus, ADDR_PAL_DATA, 0xAAAAu);   /* entry 254, idx->255 */
    arcade1_write32(&m.bus, ADDR_PAL_DATA, 0xBBBBu);   /* entry 255, idx->0   */
    arcade1_write32(&m.bus, ADDR_PAL_DATA, 0xCCCCu);   /* entry 0,   idx->1   */
    assert(arcade1_read32(&m.bus, ADDR_PAL_INDEX) == 1u &&
           "PAL_INDEX must wrap 255 -> 0");
    assert(pal_read(&m, 254u) == 0xAAAAu && "entry 254 written");
    assert(pal_read(&m, 255u) == 0xBBBBu && "entry 255 written");
    assert(pal_read(&m, 0u)   == 0xCCCCu && "entry 0 written after wrap");

    /* --- The palette does not disturb the existing 1-bit draw path -------- */
    arcade1_write32(&m.bus, ADDR_COLOR, COLOR_BLUE);
    arcade1_write32(&m.bus, ADDR_CMD, CMD_CLEAR);
    {
        const uint16_t *fb = arcade1_framebuffer(&m);
        for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
            assert(fb[i] == COLOR_BLUE && "CLEAR fills the screen blue");
    }
    arcade1_write32(&m.bus, ADDR_X, 10u);
    arcade1_write32(&m.bus, ADDR_Y, 20u);
    arcade1_write32(&m.bus, ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_W, 4u);
    arcade1_write32(&m.bus, ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_H, 3u);
    arcade1_write32(&m.bus, ADDR_COLOR, COLOR_RED);
    arcade1_write32(&m.bus, ADDR_CMD, CMD_FILL);
    {
        const uint16_t *fb = arcade1_framebuffer(&m);
        assert(fb[PIX(10, 20)] == COLOR_RED && "FILL writes its top-left pixel");
        assert(fb[PIX(13, 22)] == COLOR_RED && "FILL writes its bottom-right");
        assert(fb[PIX(14, 20)] == COLOR_BLUE && "FILL stays inside WxH");
        assert(fb[PIX(10, 23)] == COLOR_BLUE && "FILL stays inside WxH");
    }

    /* --- Reset restores the power-on palette deterministically ------------ */
    arcade1_reset(&m);
    assert(arcade1_read32(&m.bus, ADDR_PAL_INDEX) == 0u &&
           "reset clears PAL_INDEX");
    for (uint32_t i = 0; i < 16u; i++)
        assert(pal_read(&m, i) == def[i] && "reset restores default entry");
    assert(pal_read(&m, 128u) == 0u && "reset restores black entries");
    {
        const uint16_t *fb = arcade1_framebuffer(&m);
        for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
            assert(fb[i] == 0u && "reset restores a black screen");
    }

    printf("milestone27-step1: all assertions passed\n");
    printf("  verified: 256-entry palette, PAL_INDEX/PAL_DATA auto-increment "
           "+ wrap,\n            default palette (0=black, 1-15 fixed, 16-255 "
           "black), read-no-inc,\n            1-bit draw path unaffected, "
           "deterministic under reset\n");
    return 0;
}
