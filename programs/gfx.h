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

#include "arcade1_map.h"   /* the shared guest memory map (MMIO base, etc.) */

/* Video device registers (window base + M13 offsets). */
#define GFX_MMIO_BASE ARCADE1_MMIO_BASE

#define GFX_REG_X     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x100))
#define GFX_REG_Y     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x104))
#define GFX_REG_COLOR (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x108))
#define GFX_REG_CMD   (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x10C))
#define GFX_REG_ADDR  (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x114))
#define GFX_REG_W     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x118))
#define GFX_REG_H     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x11C))
/* M20 tilemap registers (docs.md "Tilemap decisions, pinned at M20"). */
#define GFX_REG_MAP_ADDR  (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x120))
#define GFX_REG_TILE_ADDR (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x124))
#define GFX_REG_COLS      (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x128))
#define GFX_REG_ROWS      (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x12C))
/* M21 camera registers (docs.md "Scrolling decisions, pinned at M21"). */
#define GFX_REG_CAM_X     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x130))
#define GFX_REG_CAM_Y     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x134))
/* M27 palette + 4-bit registers (docs.md "Color decisions, pinned at M27"). */
#define GFX_REG_PAL_INDEX (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x138))
#define GFX_REG_PAL_DATA  (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x13C))
#define GFX_REG_PAL_BANK  (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x140))
#define GFX_REG_FLAGS     (*(volatile unsigned int *)(GFX_MMIO_BASE + 0x144))
/* 0x110 (STATUS, PRESENT count) exists but the API never needs to read it. */

/* VIDEO_CMD values (docs.md register table). */
#define GFX_CMD_CLEAR     0x1u
#define GFX_CMD_SET_PIXEL 0x2u
#define GFX_CMD_PRESENT   0x3u
#define GFX_CMD_FILL      0x4u
#define GFX_CMD_SPRITE    0x5u
#define GFX_CMD_TILEMAP   0x6u
#define GFX_CMD_SPRITE4   0x7u
#define GFX_CMD_TILEMAP4  0x8u

/* --- Compiler barrier (M29: the device reads RAM) --------------------------
 * The video device reads guest memory through the INTEGER addresses in its
 * registers (image at ADDR, cells at MAP_ADDR, tiles at TILE_ADDR). That
 * access is invisible to the compiler, so without help GCC is free to sink
 * or delete the stores that fill those tables — and the device then reads
 * stale/zero bytes and silently draws nothing (sdk docs: M29_LOG.md probe9).
 *
 * THE RULE: SDK helpers are barriers. Every command goes out through
 * gfx_cmd() below, which brackets the CMD write with GFX_BARRIER(), so any
 * store made before a draw is complete before the device can see the command
 * (and no later store can be hoisted in front of it). When you poke
 * GFX_REG_CMD yourself, add the barrier yourself too:
 *
 *      GFX_BARRIER();
 *      GFX_REG_X = ...;            // hand the address to the device
 *      GFX_REG_CMD = GFX_CMD_SPRITE;
 *      GFX_BARRIER();
 *
 * The barrier is pure compile-time: it emits no instruction, so it cannot
 * change timing, hashes or behaviour — only the order the compiler is allowed
 * to assume. */
#define GFX_BARRIER() __asm__ volatile ("" ::: "memory")

/* The single place the SDK writes the video CMD register. always_inline so
 * the barrier costs no call: without it GCC keeps an out-of-line copy at -Os
 * and every command grows a jal/ret (same behaviour, needlessly fatter
 * guest code). */
static inline __attribute__((always_inline)) void gfx_cmd(unsigned int cmd)
{
    GFX_BARRIER();
    GFX_REG_CMD = cmd;
    GFX_BARRIER();
}

/* Fill the whole screen with color (RGB565). */
static inline void gfx_clear(unsigned int color)
{
    GFX_REG_COLOR = color;
    gfx_cmd(GFX_CMD_CLEAR);
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
    gfx_cmd(GFX_CMD_FILL);
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
    gfx_cmd(GFX_CMD_SPRITE);
}

/* Draw a cols x rows tilemap of cells at top-left (x, y).
 *
 * `cells` points at cols*rows 32-bit little-endian words. Each cell's low
 * 16 bits are a tile index (0 = empty cell, drawn as nothing so the
 * background shows through); its high 16 bits are an RGB565 colour that
 * tints that tile's set bits. Tile N (>= 1) has its 1-bpp image — the same
 * format gfx_sprite() takes — stored at `tiles + (N-1)*tile_bytes`, where
 * tile_bytes = tile_h * ((tile_w + 7) / 8). Cell (col, row) lands at
 * (x + col*tile_w, y + row*tile_h); each tile clips to the screen like a
 * sprite. `cells` and `tiles` must live in ROM or RAM; anything else draws
 * nothing. tile_w/tile_h <= 0 or cols/rows <= 0 draws nothing. */
static inline void gfx_tilemap(int x, int y, const void *cells,
                               const void *tiles, int cols, int rows,
                               int tile_w, int tile_h)
{
    if (cols <= 0 || rows <= 0 || tile_w <= 0 || tile_h <= 0)
        return;
    GFX_REG_X         = (unsigned int)x;
    GFX_REG_Y         = (unsigned int)y;
    GFX_REG_MAP_ADDR  = (unsigned int)cells;
    GFX_REG_TILE_ADDR = (unsigned int)tiles;
    GFX_REG_COLS      = (unsigned int)cols;
    GFX_REG_ROWS      = (unsigned int)rows;
    GFX_REG_W         = (unsigned int)tile_w;
    GFX_REG_H         = (unsigned int)tile_h;
    gfx_cmd(GFX_CMD_TILEMAP);
}

/* Set the camera to the world pixel (cam_x, cam_y), which becomes the
 * top-left of the view. gfx_sprite() and gfx_tilemap() are WORLD-space: the
 * device draws each at (x - cam_x, y - cam_y), so a guest keeps its objects
 * at fixed world coordinates and only the camera moves each frame. The
 * primitives (gfx_clear/gfx_rect/gfx_set_pixel) are screen-space and are
 * never scrolled — use them for the HUD. A camera of (0,0) (the power-on
 * default) draws everything exactly where its x/y say, so sprites and maps
 * drawn before any camera call are unaffected. */
static inline void gfx_camera(int cam_x, int cam_y)
{
    GFX_REG_CAM_X = (unsigned int)cam_x;
    GFX_REG_CAM_Y = (unsigned int)cam_y;
}

/* --- M27 paletted 4-bit drawing ------------------------------------------- */

/* Upload `n` RGB565 entries to the palette starting at entry `start`: set the
 * index once, then the device auto-increments after each data write. */
static inline void gfx_pal_upload(unsigned int start,
                                  const unsigned short *data, int n)
{
    GFX_REG_PAL_INDEX = start;
    for (int i = 0; i < n; i++)
        GFX_REG_PAL_DATA = data[i];
}

/* Set the palette bank (0..15) that gfx_sprite4() draws from. */
static inline void gfx_pal_bank(unsigned int bank)
{
    GFX_REG_PAL_BANK = bank & 0xFu;
}

/* Set the sprite flip flags: bit0 = flip X, bit1 = flip Y. */
static inline void gfx_flip(unsigned int flags)
{
    GFX_REG_FLAGS = flags & 0x3u;
}

/* Draw a 4-bit image (two pixels per byte, HIGH nibble = left pixel, width
 * even) at top-left (x, y). Pixel value 0 is transparent; value v (1..15)
 * draws PALETTE[bank*16 + v]. World-space (camera-subtracted) like
 * gfx_sprite(); clipped to the screen. flips: bit0 = flip X, bit1 = flip Y. */
static inline void gfx_sprite4(int x, int y, const void *img, int w, int h,
                               unsigned int bank, unsigned int flags)
{
    if (w <= 0 || h <= 0)
        return;
    GFX_REG_X        = (unsigned int)x;
    GFX_REG_Y        = (unsigned int)y;
    GFX_REG_ADDR     = (unsigned int)img;
    GFX_REG_W        = (unsigned int)w;
    GFX_REG_H        = (unsigned int)h;
    GFX_REG_PAL_BANK = bank & 0xFu;
    GFX_REG_FLAGS    = flags & 0x3u;
    gfx_cmd(GFX_CMD_SPRITE4);
}

/* Draw a cols x rows tilemap of 4-bit tiles at top-left (x, y). `cells` points
 * at cols*rows 32-bit little-endian words: [15:0] tile index (0 = empty),
 * [19:16] palette bank, [20] flip X, [21] flip Y. Tile N (>= 1) has its 4-bit
 * image at `tiles + (N-1) * (tile_w/2 * tile_h)` (tile_w even). World-space,
 * clipped and windowed like gfx_tilemap(); PAL_BANK/FLAGS are ignored (each
 * cell supplies its own). */
static inline void gfx_tilemap4(int x, int y, const void *cells,
                                const void *tiles, int cols, int rows,
                                int tile_w, int tile_h)
{
    if (cols <= 0 || rows <= 0 || tile_w <= 0 || tile_h <= 0)
        return;
    GFX_REG_X         = (unsigned int)x;
    GFX_REG_Y         = (unsigned int)y;
    GFX_REG_MAP_ADDR  = (unsigned int)cells;
    GFX_REG_TILE_ADDR = (unsigned int)tiles;
    GFX_REG_COLS      = (unsigned int)cols;
    GFX_REG_ROWS      = (unsigned int)rows;
    GFX_REG_W         = (unsigned int)tile_w;
    GFX_REG_H         = (unsigned int)tile_h;
    gfx_cmd(GFX_CMD_TILEMAP4);
}

/* Mark a frame as presented (counted by the device's STATUS register). */
static inline void gfx_present(void)
{
    gfx_cmd(GFX_CMD_PRESENT);
}

#endif /* ARCADE_GFX_H */
