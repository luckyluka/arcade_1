/* ARCADE-1 guest graphics API (milestone 14).
 *
 * This is a tiny convenience layer for RV32 guest programs. It is NOT part
 * of the machine's public C API — the actual machine API is in
 * src/arcade1/machine.h. This header only wraps the video device's MMIO
 * registers (docs.md M13, extended at M19).
 *
 * Coordinates are top-left based. Colors are RGB565 (5-6-5).
 * The API is synchronous: every call returns after the device has drawn.
 */
#ifndef ARCADE_GFX_H
#define ARCADE_GFX_H

/* Video device registers (window base + M13 offsets). */
#define GFX_MMIO_BASE 0x4000u

#define GFX_REG_X     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x100))
#define GFX_REG_Y     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x104))
#define GFX_REG_COLOR (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x108))
#define GFX_REG_CMD   (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x10C))
#define GFX_REG_ADDR  (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x114))
#define GFX_REG_W     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x118))
#define GFX_REG_H     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x11C))
/* 0x110 (STATUS, PRESENT count) exists but the API never needs to read it. */

/* VIDEO_CMD values (docs.md register table). */
#define GFX_CMD_CLEAR     0x1u
#define GFX_CMD_SET_PIXEL 0x2u
#define GFX_CMD_PRESENT   0x3u
#define GFX_CMD_FILL      0x4u
#define GFX_CMD_SPRITE    0x5u

/* Fill the whole screen with color (RGB565). */
static inline void gfx_clear(unsigned int color)
{
    GFX_REG_COLOR = color;
    GFX_REG_CMD   = GFX_CMD_CLEAR;
}

/* Fill a solid rectangle of `color` with its top-left corner at (x, y).
 * w/h <= 0 draws nothing. Parts outside the screen (including negative x/y)
 * are clipped by the device. The whole rectangle is drawn by one command —
 * the pixel loop lives in the video device, not in the guest. */
static inline void gfx_rect(int x, int y, int w, int h, unsigned int color)
{
    if (w <= 0 || h <= 0)
        return;
    GFX_REG_COLOR = color;
    GFX_REG_X     = (unsigned int)x;
    GFX_REG_Y     = (unsigned int)y;
    GFX_REG_W     = (unsigned int)w;
    GFX_REG_H     = (unsigned int)h;
    GFX_REG_CMD   = GFX_CMD_FILL;
}

/* Draw a 1-bits-per-pixel image with its top-left corner at (x, y).
 *
 * `img` points at `h` rows of `(w + 7) / 8` bytes (rows padded to whole
 * bytes); in each byte the MSB is the leftmost pixel. Set bits are drawn in
 * `color`, clear bits are left untouched (transparent). `img` must live in
 * the machine's ROM or RAM; anything else draws nothing. Negative x/y or
 * parts past the right/bottom edge are clipped by the device. */
static inline void gfx_sprite(int x, int y, const void *img,
                              int w, int h, unsigned int color)
{
    if (w <= 0 || h <= 0)
        return;
    GFX_REG_COLOR = color;
    GFX_REG_X     = (unsigned int)x;
    GFX_REG_Y     = (unsigned int)y;
    GFX_REG_ADDR  = (unsigned int)img;
    GFX_REG_W     = (unsigned int)w;
    GFX_REG_H     = (unsigned int)h;
    GFX_REG_CMD   = GFX_CMD_SPRITE;
}

/* Mark a frame as presented (counted by the device's STATUS register). */
static inline void gfx_present(void)
{
    GFX_REG_CMD = GFX_CMD_PRESENT;
}

#endif /* ARCADE_GFX_H */
