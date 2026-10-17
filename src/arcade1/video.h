/* ARCADE-1 video device (milestone 13).
 *
 * The second real MMIO peripheral: it lets a guest RV32 program control the
 * screen. Decisions are pinned in docs.md ("Video decisions, pinned at M13"
 * and "Sprite decisions, pinned at M19"):
 *
 *   - Extremely simple, per docs.md §17: CLEAR, SET_PIXEL and PRESENT, driven
 *     through parameter registers + one command register. M19 added two more
 *     synchronous commands on top: FILL (solid rectangle) and SPRITE
 *     (1-bpp image blit), so the per-pixel loop lives in the device.
 *   - The device draws DIRECTLY into the machine's scanout framebuffer (the
 *     same RGB565 buffer Libretro reads), so the chain is:
 *         RV32 program -> MMIO -> video device -> software renderer -> Libretro
 *   - Single buffer. PRESENT only counts executions: buffering / frame
 *     flipping is deliberately NOT decided here (docs.md: do not finalize the
 *     graphics architecture yet).
 *   - Registers live in the upper half of the one MMIO window (the M11 single
 *     handler contract): 0x000..0x0FF is the timer, 0x100.. is this device.
 *
 * No dependency on libretro or any host frontend.
 */

#ifndef ARCADE1_VIDEO_H
#define ARCADE1_VIDEO_H

#include <stdint.h>

/* --- Registers (byte offsets within the MMIO window) ---------------------- */

#define ARCADE1_VIDEO_REG_BASE   0x100u  /* first video offset; mux split     */
#define ARCADE1_VIDEO_REG_X      0x100u  /* R/W : draw X coordinate           */
#define ARCADE1_VIDEO_REG_Y      0x104u  /* R/W : draw Y coordinate           */
#define ARCADE1_VIDEO_REG_COLOR  0x108u  /* R/W : RGB565 colour (masked 16b)  */
#define ARCADE1_VIDEO_REG_CMD    0x10Cu  /* W   : command; see below          */
#define ARCADE1_VIDEO_REG_STATUS 0x110u  /* R   : PRESENT commands executed   */
#define ARCADE1_VIDEO_REG_ADDR   0x114u  /* R/W : 1-bpp image address         */
#define ARCADE1_VIDEO_REG_W      0x118u  /* R/W : width in pixels             */
#define ARCADE1_VIDEO_REG_H      0x11Cu  /* R/W : height in pixels            */
/* M20 (docs.md "Tilemap decisions, pinned at M20"): four more registers for
 * the TILEMAP command. The device reads a COLSxROWS grid of 32-bit cells from
 * MAP_ADDR and blits the tile set at TILE_ADDR. X/Y are the map origin on
 * screen and W/H are one tile's size (reused from FILL/SPRITE). */
#define ARCADE1_VIDEO_REG_MAP_ADDR  0x120u  /* R/W : grid of 32-bit cells     */
#define ARCADE1_VIDEO_REG_TILE_ADDR 0x124u  /* R/W : tile-set image data      */
#define ARCADE1_VIDEO_REG_COLS      0x128u  /* R/W : grid width in cells      */
#define ARCADE1_VIDEO_REG_ROWS      0x12Cu  /* R/W : grid height in cells     */
/* M21 (docs.md "Scrolling decisions, pinned at M21"): a 2D camera offset.
 * The two WORLD-space commands — SPRITE and TILEMAP — draw at
 * (X - CAM_X, Y - CAM_Y); the primitives (CLEAR/SET_PIXEL/FILL) stay in
 * screen space and are never scrolled (they are the HUD). A camera of (0,0)
 * is the power-on default and reproduces M19/M20 exactly. */
#define ARCADE1_VIDEO_REG_CAM_X    0x130u  /* R/W : camera X (signed pixels)  */
#define ARCADE1_VIDEO_REG_CAM_Y    0x134u  /* R/W : camera Y (signed pixels)  */

/* M27 (docs.md "Color decisions, pinned at M27"): a 256-entry RGB565 palette
 * inside the device, addressed through an index register. PAL_INDEX selects
 * the entry; a PAL_DATA write stores value&0xFFFF into that entry and then
 * auto-increments the index (wrapping 255 -> 0), so a guest uploads a whole
 * block with one PAL_INDEX write followed by N PAL_DATA writes. A PAL_DATA
 * read returns the entry at the current index without incrementing. The
 * palette is what the 4-bit commands (SPRITE4/TILEMAP4) colour their pixels
 * with; the 1-bit commands (FILL/SPRITE/...) ignore it entirely. */
#define ARCADE1_VIDEO_REG_PAL_INDEX 0x138u /* R/W : palette entry index 0..255 */
#define ARCADE1_VIDEO_REG_PAL_DATA  0x13Cu /* R/W : entry data (RGB565); write
                                            * auto-increments PAL_INDEX       */

/* VIDEO_CMD values. Any other value is ignored. */
#define ARCADE1_VIDEO_CMD_CLEAR     0x1u  /* fill the screen with COLOR       */
#define ARCADE1_VIDEO_CMD_SET_PIXEL 0x2u  /* plot COLOR at (X, Y), clipped    */
#define ARCADE1_VIDEO_CMD_PRESENT   0x3u  /* acknowledge the frame (counted)  */
#define ARCADE1_VIDEO_CMD_FILL      0x4u  /* solid WxH rect at (X, Y)         */
#define ARCADE1_VIDEO_CMD_SPRITE    0x5u  /* blit 1-bpp image at ADDR at (X,Y) */
#define ARCADE1_VIDEO_CMD_TILEMAP   0x6u  /* draw the COLSxROWS tile grid     */

/* --- Host-side work counters (DEVICE_BUDGET.md, BEFORE_M25 task C) --------- *
 *
 * MEASUREMENT ONLY. These count what the device actually did during the
 * current frame, so a per-frame work budget can be sized from data rather
 * than guessed. They are deliberately NOT guest-visible (no MMIO register
 * exposes them), NOT part of any state hash (state_hash covers CPU+RAM;
 * fb_hash covers the framebuffer), and they never affect behaviour — the
 * fields only ever increment.
 *
 *   commands, by type   how many of each command the guest issued
 *   pixels_written      framebuffer writes AFTER clipping; a transparent
 *                       1-bpp bit writes nothing and is not counted
 *   bytes_fetched       guest-memory reads the device performed: TILEMAP
 *                       cell bytes + 1-bpp bitmap bytes. The reference
 *                       blitter reads one byte per pixel EXAMINED, so this
 *                       is a fetch count, not a distinct-byte count (a
 *                       caching implementation would read fewer; see
 *                       DEVICE_BUDGET.md).
 *   cells_visited       TILEMAP cells after windowing (a windowed-out cell
 *                       is never visited and costs nothing)
 */
typedef struct arcade1_video_stats {
    uint32_t cmds_clear;      /* CLEAR commands executed                  */
    uint32_t cmds_set_pixel;  /* SET_PIXEL commands executed              */
    uint32_t cmds_fill;       /* FILL commands executed                   */
    uint32_t cmds_sprite;     /* SPRITE commands executed                 */
    uint32_t cmds_tilemap;    /* TILEMAP commands executed                */
    uint32_t cmds_present;    /* PRESENT commands executed                */
    uint32_t commands;        /* total commands executed (all types)      */
    uint32_t pixels_written;  /* framebuffer writes after clipping        */
    uint32_t bytes_fetched;   /* cell + bitmap byte reads from guest mem  */
    uint32_t cells_visited;   /* TILEMAP cells after windowing            */
} arcade1_video_stats_t;

/* --- Device state --------------------------------------------------------- */

typedef struct arcade1_video {
    uint16_t *framebuffer;   /* machine scanout buffer; set once at init
                              * (the device borrows it, does not own it) */
    const uint8_t *rom;      /* guest ROM/RAM, borrowed the same way; the
                              * SPRITE/TILEMAP commands fetch bytes here    */
    uint8_t *ram;
    uint32_t x, y;           /* parameter registers                       */
    uint32_t color;          /* RGB565, kept masked to 16 bits            */
    uint32_t addr;           /* image data address (SPRITE source)        */
    uint32_t w, h;           /* width/height in pixels (FILL/SPRITE)      */
    uint32_t map_addr;       /* cell array address (TILEMAP source, M20)  */
    uint32_t tile_addr;      /* tile-set image address (TILEMAP, M20)     */
    uint32_t cols, rows;     /* grid size in cells (TILEMAP, M20)         */
    uint32_t cam_x, cam_y;   /* camera offset (SPRITE/TILEMAP origin, M21)*/
    uint16_t palette[256];   /* RGB565 palette (M27), 256 entries         */
    uint8_t  pal_index;      /* PAL_INDEX register (auto-increment target)*/
    uint32_t presents;       /* PRESENT commands executed (STATUS)        */
    arcade1_video_stats_t stats;        /* work done so far this frame    */
    arcade1_video_stats_t frame_totals; /* snapshot at end of last frame  */
} arcade1_video_t;

/* Registers/power-on: parameters zeroed, nothing presented. Keeps the
 * framebuffer/ROM/RAM pointers (set once by the machine at init). */
void arcade1_video_reset(arcade1_video_t *v);

/* MMIO handler pair for the machine's muxed window handler (docs.md M11
 * contract); ctx is the arcade1_video_t*. Unknown offsets read 0 / ignore
 * writes. Commands execute synchronously at the write. */
uint32_t arcade1_video_mmio_read(void *ctx, uint32_t offset);
void arcade1_video_mmio_write(void *ctx, uint32_t offset, uint32_t value);

/* --- Host-side work counters (DEVICE_BUDGET.md, BEFORE_M25 task C) --------- *
 *
 * Measurement only; never guest-visible and never affects behaviour.
 * The machine calls _begin_frame at the start of every arcade1_run_frame and
 * _end_frame just before it returns, so `frame_totals` always holds the work
 * done by the frame that just finished, and `stats` accumulates the current
 * one from zero. */
void arcade1_video_stats_begin_frame(arcade1_video_t *v);
void arcade1_video_stats_end_frame(arcade1_video_t *v);

/* Read-only views for tests/budgets. `stats` is live (this frame, growing);
 * `frame_totals` is the completed last frame. */
const arcade1_video_stats_t *arcade1_video_stats(const arcade1_video_t *v);
const arcade1_video_stats_t *arcade1_video_frame_totals(const arcade1_video_t *v);

#endif /* ARCADE1_VIDEO_H */
