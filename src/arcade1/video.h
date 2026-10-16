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

/* --- Registers (byte offsets within the 0x4000 MMIO window) --------------- */

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

/* VIDEO_CMD values. Any other value is ignored. */
#define ARCADE1_VIDEO_CMD_CLEAR     0x1u  /* fill the screen with COLOR       */
#define ARCADE1_VIDEO_CMD_SET_PIXEL 0x2u  /* plot COLOR at (X, Y), clipped    */
#define ARCADE1_VIDEO_CMD_PRESENT   0x3u  /* acknowledge the frame (counted)  */
#define ARCADE1_VIDEO_CMD_FILL      0x4u  /* solid WxH rect at (X, Y)         */
#define ARCADE1_VIDEO_CMD_SPRITE    0x5u  /* blit 1-bpp image at ADDR at (X,Y) */
#define ARCADE1_VIDEO_CMD_TILEMAP   0x6u  /* draw the COLSxROWS tile grid     */

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
    uint32_t presents;       /* PRESENT commands executed (STATUS)        */
} arcade1_video_t;

/* Registers/power-on: parameters zeroed, nothing presented. Keeps the
 * framebuffer/ROM/RAM pointers (set once by the machine at init). */
void arcade1_video_reset(arcade1_video_t *v);

/* MMIO handler pair for the machine's muxed window handler (docs.md M11
 * contract); ctx is the arcade1_video_t*. Unknown offsets read 0 / ignore
 * writes. Commands execute synchronously at the write. */
uint32_t arcade1_video_mmio_read(void *ctx, uint32_t offset);
void arcade1_video_mmio_write(void *ctx, uint32_t offset, uint32_t value);

#endif /* ARCADE1_VIDEO_H */
