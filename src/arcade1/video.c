/* ARCADE-1 video device (milestone 13) implementation.
 *
 * See video.h. Register state plus synchronous commands that draw into the
 * machine's scanout framebuffer; no clock, no host input, no buffering
 * policy. M19 (docs.md "Sprite decisions, pinned at M19") added FILL and
 * SPRITE: device-side rectangle fills and 1-bpp image blits with transparent
 * bits, so the per-pixel loop no longer runs in the guest.
 */

#include "video.h"

#include "machine.h"   /* for the screen geometry only (ARCADE1_WIDTH/HEIGHT) */

#include <string.h>    /* memset: the per-frame stats reset (task C counters) */

/* --- M27: power-on palette ------------------------------------------------ *
 * A fixed 16-colour default (an EGA-like set) occupies entries 0..15; entry 0
 * is black (the transparent/empty colour of a 4-bit image), and entries 16..255
 * are black too. The guest rewrites any entry through PAL_INDEX/PAL_DATA. The
 * exact RGB565 values are pinned in docs.md "Color decisions, pinned at M27". */
static const uint16_t k_default_palette[16] = {
    0x0000u, /*  0 black         */
    0x001Fu, /*  1 blue          */
    0x03E0u, /*  2 green         */
    0x03FFu, /*  3 cyan          */
    0xF800u, /*  4 red           */
    0xF81Fu, /*  5 magenta       */
    0xAAA0u, /*  6 brown         */
    0xAD55u, /*  7 light gray    */
    0x52AAu, /*  8 dark gray     */
    0x52BFu, /*  9 light blue    */
    0x57EAu, /* 10 light green   */
    0x57FFu, /* 11 light cyan    */
    0xFAAAu, /* 12 light red     */
    0xFABFu, /* 13 light magenta */
    0xFFE0u, /* 14 yellow        */
    0xFFFFu, /* 15 white         */
};

/* --- Lifecycle ------------------------------------------------------------ */

void arcade1_video_reset(arcade1_video_t *v)
{
    v->x        = 0u;
    v->y        = 0u;
    v->color    = 0u;
    v->addr     = 0u;
    v->w        = 0u;
    v->h        = 0u;
    v->map_addr  = 0u;
    v->tile_addr = 0u;
    v->cols      = 0u;
    v->rows      = 0u;
    v->cam_x     = 0u;
    v->cam_y     = 0u;
    /* Power-on palette: entry 0 = black, 1..15 = the fixed default set,
     * 16..255 = black. Deterministic on every reset. */
    memcpy(v->palette, k_default_palette, sizeof k_default_palette);
    memset(v->palette + 16, 0,
           sizeof v->palette - sizeof k_default_palette);
    v->pal_index = 0u;
    v->presents = 0u;
    arcade1_video_stats_begin_frame(v);   /* counters start clean too */
    v->frame_totals = v->stats;           /* and read as zero until a frame */
    /* v->framebuffer / v->rom / v->ram are deliberately kept: they are
     * assigned once at machine init and survive resets (like the bus's MMIO
     * attachment). */
}

/* --- Host-side work counters (DEVICE_BUDGET.md, BEFORE_M25 task C) -------- *
 * Measurement only: these fields only ever increment, are never exposed over
 * MMIO, and are not part of any state hash, so they cannot affect behaviour.
 * See video.h for what each metric means. */

void arcade1_video_stats_begin_frame(arcade1_video_t *v)
{
    memset(&v->stats, 0, sizeof v->stats);
}

void arcade1_video_stats_end_frame(arcade1_video_t *v)
{
    v->frame_totals = v->stats;
}

const arcade1_video_stats_t *arcade1_video_stats(const arcade1_video_t *v)
{
    return &v->stats;
}

const arcade1_video_stats_t *arcade1_video_frame_totals(const arcade1_video_t *v)
{
    return &v->frame_totals;
}

/* --- Commands (run synchronously at the VIDEO_CMD write) ------------------ */

static void video_clear(arcade1_video_t *v)
{
    const uint16_t c = (uint16_t)v->color;

    v->stats.cmds_clear++;
    v->stats.commands++;
    if (v->framebuffer == 0)
        return;
    for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
        v->framebuffer[i] = c;
    v->stats.pixels_written += (uint32_t)ARCADE1_FB_PIXELS;
}

static void video_set_pixel(arcade1_video_t *v)
{
    const uint16_t c = (uint16_t)v->color;

    v->stats.cmds_set_pixel++;
    v->stats.commands++;
    if (v->framebuffer == 0)
        return;
    /* Out-of-range coordinates are stored in the register (and readable back)
     * but the plot is clipped: nothing is written outside the screen. */
    if (v->x < (uint32_t)ARCADE1_WIDTH && v->y < (uint32_t)ARCADE1_HEIGHT) {
        v->framebuffer[v->y * (uint32_t)ARCADE1_WIDTH + v->x] = c;
        v->stats.pixels_written++;      /* clipped away: no pixel written */
    }
}

static void video_present(arcade1_video_t *v)
{
    /* Single buffer for now: PRESENT only records that the guest presented a
     * frame. Flipping/buffering is explicitly not decided at M13. */
    v->stats.cmds_present++;
    v->stats.commands++;
    v->presents++;
}

/* --- M19: FILL and SPRITE -------------------------------------------------- *
 *
 * Both draw a WxH block of pixels at (X, Y) into the scanout framebuffer,
 * row-major, clipped to the screen. Pixel-for-pixel identical to the old
 * per-pixel SET_PIXEL loop (docs.md "Sprite decisions, pinned at M19"), so
 * the framebuffer bytes do not change; only the per-pixel loop now lives in
 * the device.
 *
 * Semantics of the clip: X/Y are SIGNED coordinates and W/H are sizes; the
 * device draws the intersection of [x, x+w) x [y, y+h) with the screen,
 * computed in 64-bit. For every value the guest API can produce (int x/y,
 * positive w/h) this is exactly what the old wrapped per-pixel loop drew —
 * a negative origin shows its right/bottom part, an overhang off the
 * right/bottom edge is cut — but no coordinate can wrap around an edge. */

/* Clip the on-screen window of the block [x, x+w) x [y, y+h). X/Y are SIGNED
 * coordinates and W/H are sizes; the result is the intersection with the
 * screen, computed in 64-bit so no coordinate can wrap. */
static void video_clip(int64_t x, int64_t y, uint64_t w, uint64_t h,
                       int64_t *x0, int64_t *x1, int64_t *y0, int64_t *y1)
{
    const int64_t xw = x + (int64_t)w;   /* 64-bit: cannot overflow */
    const int64_t yh = y + (int64_t)h;

    *x0 = (x > 0) ? x : 0;
    *x1 = (xw < (int64_t)ARCADE1_WIDTH) ? xw : (int64_t)ARCADE1_WIDTH;
    *y0 = (y > 0) ? y : 0;
    *y1 = (yh < (int64_t)ARCADE1_HEIGHT) ? yh : (int64_t)ARCADE1_HEIGHT;
    if (*x0 > *x1) *x0 = *x1;   /* fully off-screen -> empty range */
    if (*y0 > *y1) *y0 = *y1;
}

static void video_fill(arcade1_video_t *v)
{
    int64_t x0, x1, y0, y1, px, py;

    v->stats.cmds_fill++;
    v->stats.commands++;
    if (v->framebuffer == 0)
        return;

    video_clip((int32_t)v->x, (int32_t)v->y, v->w, v->h, &x0, &x1, &y0, &y1);

    for (py = y0; py < y1; py++)
        for (px = x0; px < x1; px++)
            v->framebuffer[py * (int64_t)ARCADE1_WIDTH + px] =
                (uint16_t)v->color;
    /* Every pixel of the clipped window is written (a fill has no
     * transparency), so the on-screen area is exactly the work done. */
    v->stats.pixels_written += (uint32_t)((x1 - x0) * (y1 - y0));
}

/* Fetch one byte of a 1-bpp image. The device only ever reads guest ROM
 * (0x00000000..0x0003FFFF) and RAM (0x20000000..0x2000FFFF); anything else —
 * in particular the MMIO window — reads as 0 bits, so a sprite/tile can never
 * have side effects on other devices and these fetches are not CPU accesses
 * (the stray-access counter stays untouched). */
static uint8_t video_fetch_byte(const arcade1_video_t *v, uint64_t addr)
{
    if (addr < (uint32_t)ARCADE1_ROM_SIZE)
        return v->rom[addr];
    if (addr >= (uint32_t)ARCADE1_RAM_BASE &&
        addr < (uint32_t)(ARCADE1_RAM_BASE + ARCADE1_RAM_SIZE))
        return v->ram[addr - ARCADE1_RAM_BASE];
    return 0u;
}

/* Blit one WxH 1-bpp image at (src_addr) onto the screen at the SIGNED
 * origin (x, y), tinting set bits with `color` and leaving clear bits
 * untouched (transparent). This is the single pixel pipeline shared by the
 * SPRITE command and by each cell of the TILEMAP command. Rows are padded to
 * whole bytes; MSB is the leftmost pixel. When the image hangs off the
 * left/top of the screen the image column/row is recovered from the clipped
 * screen coordinate (64-bit subtraction, so it cannot wrap). */
static void video_blit_1bpp(arcade1_video_t *v, uint64_t src_addr,
                            int64_t x, int64_t y, uint64_t w, uint64_t h,
                            uint16_t color)
{
    int64_t x0, x1, y0, y1, sx, sy;

    if (v->framebuffer == 0)
        return;

    video_clip(x, y, w, h, &x0, &x1, &y0, &y1);

    for (sy = y0; sy < y1; sy++) {
        const uint64_t row = (uint64_t)(sy - y);
        for (sx = x0; sx < x1; sx++) {
            const uint64_t col = (uint64_t)(sx - x);
            const uint64_t byte_addr = src_addr +
                row * ((w + 7u) / 8u) + col / 8u;
            const uint8_t bits = video_fetch_byte(v, byte_addr);

            /* The reference reads one byte per pixel EXAMINED (no caching),
             * so that is the fetch work this blit really cost. */
            v->stats.bytes_fetched++;

            /* Set bits are drawn in color; clear bits are transparent. */
            if ((bits & (uint8_t)(0x80u >> (col & 7u))) != 0u) {
                v->framebuffer[sy * (int64_t)ARCADE1_WIDTH + sx] = color;
                v->stats.pixels_written++;   /* transparent: writes nothing */
            }
        }
    }
}

/* M21: SPRITE is a WORLD-space command, so the camera is subtracted from its
 * origin — the guest keeps the sprite at a fixed world position and only the
 * camera moves. Primitives (CLEAR/SET_PIXEL/FILL) stay screen-space. */
static void video_sprite(arcade1_video_t *v)
{
    v->stats.cmds_sprite++;
    v->stats.commands++;
    video_blit_1bpp(v, v->addr,
                    (int32_t)v->x - (int32_t)v->cam_x,
                    (int32_t)v->y - (int32_t)v->cam_y,
                    v->w, v->h, (uint16_t)v->color);
}

/* --- M20: TILEMAP --------------------------------------------------------- *
 * Draw a COLSxROWS grid of cells in one command. Each cell is one 32-bit
 * little-endian word: bits [15:0] = tile index (0 = empty, skipped), bits
 * [31:16] = RGB565 tint for that tile. Tile N (>= 1) has its 1-bpp image at
 * tile_addr + (N-1) * tile_bytes, tile_bytes = H * ceil(W/8); the image
 * format is exactly the SPRITE one. Cell (col, row) lands at
 * (X + col*W, Y + row*H); each tile clips to the screen like a sprite.
 * Per-cell colour is the one "tile attribute" both shipped games need
 * (Tetris' 7 piece colours, Breakout's per-row brick colours).
 *
 * M21: TILEMAP is WORLD-space, so the camera is subtracted from the origin.
 * It also WINDOWs: only the columns/rows even partially on screen are
 * visited, so a map far larger than the screen costs only its visible cells
 * in fetches/bandwidth (the M23 question). Skipping a fully off-screen cell
 * cannot change a pixel — the per-tile clip would have drawn nothing for it
 * anyway — so the framebuffer is identical to visiting every cell. */
static void video_tilemap(arcade1_video_t *v)
{
    uint32_t col, row;
    uint64_t tile_bytes;
    int64_t ox, oy, w, h;
    int64_t col_start, col_end, row_start, row_end;

    v->stats.cmds_tilemap++;
    v->stats.commands++;
    if (v->framebuffer == 0)
        return;
    /* A zero size or an empty grid draws nothing (also avoids /0 below). */
    if (v->w == 0 || v->h == 0 || v->cols == 0 || v->rows == 0)
        return;

    tile_bytes = (uint64_t)v->h * (((uint64_t)v->w + 7u) / 8u);

    /* Map origin on screen after the camera (world minus camera). */
    ox = (int32_t)v->x - (int32_t)v->cam_x;
    oy = (int32_t)v->y - (int32_t)v->cam_y;
    w  = (int64_t)v->w;
    h  = (int64_t)v->h;

    /* Visible column window: first col whose right edge > 0, last col (excl)
     * whose left edge < WIDTH. floor/ceil in 64-bit, clamped to the grid. */
    col_start = (ox < 0) ? ((-ox) / w) : 0;           /* floor(-ox / w)     */
    if (col_start < 0) col_start = 0;
    if (col_start > (int64_t)v->cols) col_start = (int64_t)v->cols;
    {
        const int64_t num = (int64_t)ARCADE1_WIDTH - ox;
        col_end = (num <= 0) ? 0 : ((num + w - 1) / w);   /* ceil(num / w)  */
    }
    if (col_end < 0) col_end = 0;
    if (col_end > (int64_t)v->cols) col_end = (int64_t)v->cols;

    row_start = (oy < 0) ? ((-oy) / h) : 0;           /* floor(-oy / h)     */
    if (row_start < 0) row_start = 0;
    if (row_start > (int64_t)v->rows) row_start = (int64_t)v->rows;
    {
        const int64_t num = (int64_t)ARCADE1_HEIGHT - oy;
        row_end = (num <= 0) ? 0 : ((num + h - 1) / h);   /* ceil(num / h)  */
    }
    if (row_end < 0) row_end = 0;
    if (row_end > (int64_t)v->rows) row_end = (int64_t)v->rows;

    /* Windowed cell count: what the guest asked for, minus the cells the
     * camera pushed off screen (those are never visited, never fetched). */
    v->stats.cells_visited +=
        (uint32_t)((col_end - col_start) * (row_end - row_start));

    for (row = (uint32_t)row_start; row < (uint32_t)row_end; row++) {
        for (col = (uint32_t)col_start; col < (uint32_t)col_end; col++) {
            const uint64_t cell_addr = (uint64_t)v->map_addr +
                ((uint64_t)row * v->cols + col) * 4u;
            /* One little-endian 32-bit cell, assembled from byte fetches. */
            const uint32_t cell =
                (uint32_t)video_fetch_byte(v, cell_addr) |
                ((uint32_t)video_fetch_byte(v, cell_addr + 1u) << 8) |
                ((uint32_t)video_fetch_byte(v, cell_addr + 2u) << 16) |
                ((uint32_t)video_fetch_byte(v, cell_addr + 3u) << 24);
            const uint32_t index = cell & 0xFFFFu;

            v->stats.bytes_fetched += 4u;   /* the 32-bit cell, byte by byte */

            if (index == 0u)
                continue;   /* empty cell: background shows through */

            video_blit_1bpp(v,
                            (uint64_t)v->tile_addr +
                                (uint64_t)(index - 1u) * tile_bytes,
                            ox + (int64_t)col * w,
                            oy + (int64_t)row * h,
                            v->w, v->h, (uint16_t)(cell >> 16));
        }
    }
}

/* --- MMIO handlers (ctx is the arcade1_video_t*) -------------------------- */

uint32_t arcade1_video_mmio_read(void *ctx, uint32_t offset)
{
    const arcade1_video_t *v = ctx;

    switch (offset) {
    case ARCADE1_VIDEO_REG_X:      return v->x;
    case ARCADE1_VIDEO_REG_Y:      return v->y;
    case ARCADE1_VIDEO_REG_COLOR:  return v->color;
    case ARCADE1_VIDEO_REG_STATUS: return v->presents;
    case ARCADE1_VIDEO_REG_ADDR:   return v->addr;
    case ARCADE1_VIDEO_REG_W:      return v->w;
    case ARCADE1_VIDEO_REG_H:      return v->h;
    case ARCADE1_VIDEO_REG_MAP_ADDR:  return v->map_addr;
    case ARCADE1_VIDEO_REG_TILE_ADDR: return v->tile_addr;
    case ARCADE1_VIDEO_REG_COLS:      return v->cols;
    case ARCADE1_VIDEO_REG_ROWS:      return v->rows;
    case ARCADE1_VIDEO_REG_CAM_X:     return v->cam_x;
    case ARCADE1_VIDEO_REG_CAM_Y:     return v->cam_y;
    case ARCADE1_VIDEO_REG_PAL_INDEX: return v->pal_index;
    case ARCADE1_VIDEO_REG_PAL_DATA:
        return v->palette[v->pal_index];   /* read entry, no auto-increment */
    default:                       return 0u;   /* CMD is write-only, rest 0 */
    }
}

void arcade1_video_mmio_write(void *ctx, uint32_t offset, uint32_t value)
{
    arcade1_video_t *v = ctx;

    switch (offset) {
    case ARCADE1_VIDEO_REG_X:
        v->x = value;
        return;
    case ARCADE1_VIDEO_REG_Y:
        v->y = value;
        return;
    case ARCADE1_VIDEO_REG_COLOR:
        v->color = value & 0xFFFFu;   /* RGB565 is 16 bits */
        return;
    case ARCADE1_VIDEO_REG_ADDR:
        v->addr = value;              /* SPRITE image source (ROM or RAM) */
        return;
    case ARCADE1_VIDEO_REG_W:
        v->w = value;                 /* FILL/SPRITE width, TILEMAP tile w */
        return;
    case ARCADE1_VIDEO_REG_H:
        v->h = value;                 /* FILL/SPRITE height, TILEMAP tile h */
        return;
    case ARCADE1_VIDEO_REG_MAP_ADDR:
        v->map_addr = value;          /* TILEMAP cell array (ROM or RAM)   */
        return;
    case ARCADE1_VIDEO_REG_TILE_ADDR:
        v->tile_addr = value;         /* TILEMAP tile-set image (ROM/RAM)  */
        return;
    case ARCADE1_VIDEO_REG_COLS:
        v->cols = value;              /* TILEMAP grid width in cells       */
        return;
    case ARCADE1_VIDEO_REG_ROWS:
        v->rows = value;              /* TILEMAP grid height in cells      */
        return;
    case ARCADE1_VIDEO_REG_CAM_X:
        v->cam_x = value;             /* camera X; SPRITE/TILEMAP subtract */
        return;
    case ARCADE1_VIDEO_REG_CAM_Y:
        v->cam_y = value;             /* camera Y; SPRITE/TILEMAP subtract */
        return;
    case ARCADE1_VIDEO_REG_PAL_INDEX:
        v->pal_index = (uint8_t)(value & 0xFFu);   /* 256 entries */
        return;
    case ARCADE1_VIDEO_REG_PAL_DATA:
        v->palette[v->pal_index] = (uint16_t)(value & 0xFFFFu);
        v->pal_index = (uint8_t)((v->pal_index + 1u) & 0xFFu);  /* auto-inc */
        return;
    case ARCADE1_VIDEO_REG_CMD:
        switch (value) {
        case ARCADE1_VIDEO_CMD_CLEAR:     video_clear(v);     return;
        case ARCADE1_VIDEO_CMD_SET_PIXEL: video_set_pixel(v); return;
        case ARCADE1_VIDEO_CMD_PRESENT:   video_present(v);   return;
        case ARCADE1_VIDEO_CMD_FILL:      video_fill(v);      return;
        case ARCADE1_VIDEO_CMD_SPRITE:    video_sprite(v);    return;
        case ARCADE1_VIDEO_CMD_TILEMAP:   video_tilemap(v);   return;
        default:                          return;             /* ignored */
        }
    default:
        return;   /* Unknown register: ignored. */
    }
}
