/* ARCADE-1 video device (milestone 13) implementation.
 *
 * See video.h. Register state plus three synchronous commands that draw into
 * the machine's scanout framebuffer; no clock, no host input, no buffering
 * policy.
 */

#include "video.h"

#include "machine.h"   /* for the screen geometry only (ARCADE1_WIDTH/HEIGHT) */

/* --- Lifecycle ------------------------------------------------------------ */

void arcade1_video_reset(arcade1_video_t *v)
{
    v->x        = 0u;
    v->y        = 0u;
    v->color    = 0u;
    v->presents = 0u;
    /* v->framebuffer is deliberately kept: it is assigned once at machine
     * init and survives resets (like the bus's MMIO attachment). */
}

/* --- Commands (run synchronously at the VIDEO_CMD write) ------------------ */

static void video_clear(arcade1_video_t *v)
{
    const uint16_t c = (uint16_t)v->color;

    if (v->framebuffer == 0)
        return;
    for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
        v->framebuffer[i] = c;
}

static void video_set_pixel(arcade1_video_t *v)
{
    const uint16_t c = (uint16_t)v->color;

    if (v->framebuffer == 0)
        return;
    /* Out-of-range coordinates are stored in the register (and readable back)
     * but the plot is clipped: nothing is written outside the screen. */
    if (v->x < (uint32_t)ARCADE1_WIDTH && v->y < (uint32_t)ARCADE1_HEIGHT)
        v->framebuffer[v->y * (uint32_t)ARCADE1_WIDTH + v->x] = c;
}

static void video_present(arcade1_video_t *v)
{
    /* Single buffer for now: PRESENT only records that the guest presented a
     * frame. Flipping/buffering is explicitly not decided at M13. */
    v->presents++;
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
    case ARCADE1_VIDEO_REG_CMD:
        switch (value) {
        case ARCADE1_VIDEO_CMD_CLEAR:     video_clear(v);     return;
        case ARCADE1_VIDEO_CMD_SET_PIXEL: video_set_pixel(v); return;
        case ARCADE1_VIDEO_CMD_PRESENT:   video_present(v);   return;
        default:                          return;             /* ignored */
        }
    default:
        return;   /* Unknown register: ignored. */
    }
}
