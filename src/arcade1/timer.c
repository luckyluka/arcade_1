/* ARCADE-1 timer — deterministic device (milestone 12) implementation.
 *
 * See timer.h. Pure register state plus a per-frame tick; no clock, no
 * interrupts, no host input.
 */

#include "timer.h"

/* --- Lifecycle ------------------------------------------------------------ */

void arcade1_timer_reset(arcade1_timer_t *t)
{
    t->count = 0u;
    t->ctrl  = ARCADE1_TIMER_CTRL_ENABLE;
}

void arcade1_timer_tick(arcade1_timer_t *t)
{
    if (t->ctrl & ARCADE1_TIMER_CTRL_ENABLE)
        t->count++;
}

/* --- MMIO handlers (ctx is the arcade1_timer_t*) -------------------------- */

uint32_t arcade1_timer_mmio_read(void *ctx, uint32_t offset)
{
    const arcade1_timer_t *t = ctx;

    if (offset == ARCADE1_TIMER_REG_COUNT)
        return t->count;
    if (offset == ARCADE1_TIMER_REG_CTRL)
        return t->ctrl;
    return 0u;   /* Unknown register: deterministic zero. */
}

void arcade1_timer_mmio_write(void *ctx, uint32_t offset, uint32_t value)
{
    arcade1_timer_t *t = ctx;

    if (offset == ARCADE1_TIMER_REG_CTRL) {
        t->ctrl = value & ARCADE1_TIMER_CTRL_ENABLE;
        return;
    }
    /* TIMER_COUNT is read-only; unknown registers are ignored. */
}
