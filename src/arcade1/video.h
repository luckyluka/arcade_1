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
#define ARCADE1_VIDEO_REG_X      0x100u  /* R/W : set-pixel X coordinate      */
#define ARCADE1_VIDEO_REG_Y      0x104u  /* R/W : set-pixel Y coordinate      */
#define ARCADE1_VIDEO_REG_COLOR  0x108u  /* R/W : RGB565 colour (masked 16b)  */
#define ARCADE1_VIDEO_REG_CMD    0x10Cu  /* W   : command; see below          */
#define ARCADE1_VIDEO_REG_STATUS 0x110u  /* R   : PRESENT commands executed   */
#define ARCADE1_VIDEO_REG_ADDR   0x114u  /* R/W : 1-bpp image address (SPRITE) */
#define ARCADE1_VIDEO_REG_W      0x118u  /* R/W : width in pixels (FILL/SPRITE)*/
#define ARCADE1_VIDEO_REG_H      0x11Cu  /* R/W : height in pixels (FILL/SPRITE)*/

/* VIDEO_CMD values. Any other value is ignored. */
#define ARCADE1_VIDEO_CMD_CLEAR     0x1u  /* fill the screen with COLOR       */
#define ARCADE1_VIDEO_CMD_SET_PIXEL 0x2u  /* plot COLOR at (X, Y), clipped    */
#define ARCADE1_VIDEO_CMD_PRESENT   0x3u  /* acknowledge the frame (counted)  */
#define ARCADE1_VIDEO_CMD_FILL      0x4u  /* solid WxH rect at (X, Y)         */
#define ARCADE1_VIDEO_CMD_SPRITE    0x5u  /* blit 1-bpp image at ADDR at (X,Y) */

/* --- Device state --------------------------------------------------------- */

typedef struct arcade1_video {
    uint16_t *framebuffer;   /* machine scanout buffer; set once at init
                              * (the device borrows it, does not own it) */
    const uint8_t *rom;      /* guest ROM/RAM, borrowed the same way; the
                              * SPRITE command fetches image bytes here     */
    uint8_t *ram;
    uint32_t x, y;           /* parameter registers                       */
    uint32_t color;          /* RGB565, kept masked to 16 bits            */
    uint32_t addr;           /* image data address (SPRITE source)        */
    uint32_t w, h;           /* width/height in pixels (FILL/SPRITE)      */
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
