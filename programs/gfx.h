/* ARCADE-1 guest-facing graphics API (milestone 14).
 *
 * The primitive drawing API every guest game uses: clear, rectangle, present.
 * It is a thin layer over the M13 video device's MMIO registers (docs.md
 * register table), so the drawing path is guest code -> MMIO -> video device ->
 * framebuffer.
 *
 * GUEST CODE ONLY. The functions dereference fixed MMIO addresses
 * (0x4000 + register), which only makes sense when executed by the RV32 CPU;
 * host-side tests must talk to the machine through the arcade1_* API instead.
 *
 * Deliberately primitive, per docs.md milestone 14: a rectangle is a loop of
 * SET_PIXEL writes (one sw per pixel, colour set once per call). Rectangles
 * may extend past the screen edges — including negative coordinates — and the
 * device clips each plot. No sprites, tilemaps, textures, alpha blending,
 * rotation or shaders yet.
 */

#ifndef ARCADE_GFX_H
#define ARCADE_GFX_H

/* Video device registers: MMIO window base + window-relative offsets from
 * docs.md (must match src/arcade1/video.h). */
#define GFX_MMIO_BASE 0x4000u
#define GFX_REG_X     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x100))
#define GFX_REG_Y     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x104))
#define GFX_REG_COLOR (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x108))
#define GFX_REG_CMD   (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x10C))
/* 0x110 (STATUS, PRESENT count) exists but the API never needs to read it. */

/* VIDEO_CMD values (docs.md register table). */
#define GFX_CMD_CLEAR     0x1u
#define GFX_CMD_SET_PIXEL 0x2u
#define GFX_CMD_PRESENT   0x3u

/* Fill the whole screen with color (RGB565). */
static inline void gfx_clear(unsigned int color)
{
    GFX_REG_COLOR = color;
    GFX_REG_CMD   = GFX_CMD_CLEAR;
}

/* Draw a filled, axis-aligned rectangle of `color` with its top-left corner
 * at (x, y). w/h <= 0 draws nothing. Parts outside the screen (including
 * negative x/y) are clipped per pixel by the device. */
static inline void gfx_rect(int x, int y, int w, int h, unsigned int color)
{
    GFX_REG_COLOR = color;
    for (int j = 0; j < h; j++) {
        GFX_REG_Y = (unsigned int)(y + j);
        for (int i = 0; i < w; i++) {
            GFX_REG_X = (unsigned int)(x + i);
            GFX_REG_CMD = GFX_CMD_SET_PIXEL;
        }
    }
}

/* Mark a frame as presented (counted by the device's STATUS register). */
static inline void gfx_present(void)
{
    GFX_REG_CMD = GFX_CMD_PRESENT;
}

#endif /* ARCADE_GFX_H */
