/* ARCADE-1 timer — deterministic device (milestone 12).
 *
 * The machine's first real MMIO peripheral: a tick counter that exposes frame
 * time to the guest. Design is pinned in docs.md ("Timer decisions, pinned at
 * M12"):
 *
 *   - Tick source: ONE tick per frame. arcade1_timer_tick() is called once per
 *     completed arcade1_run_frame(); there is no host wall-clock anywhere, so
 *     the count is a pure function of frames executed and is identical across
 *     hosts. Frames themselves are paced by the host/frontend.
 *   - Registers: TIMER_COUNT (read-only) and TIMER_CTRL (bit0 = enable), at
 *     byte offsets within the single MMIO window (docs.md register table).
 *   - Relationship: the CPU counts instructions (instret), the machine counts
 *     frames, and this device hands frame time to the guest over MMIO.
 *
 * The handlers below are registered on the bus with arcade1_set_mmio(), so
 * every guest/host access reaches the device (never a stray access).
 *
 * No dependency on libretro or any host frontend.
 */

#ifndef ARCADE1_TIMER_H
#define ARCADE1_TIMER_H

#include <stdint.h>

/* --- Registers (byte offsets within the MMIO window) ---------------------- */

#define ARCADE1_TIMER_REG_COUNT 0x00u  /* R   : tick count                */
#define ARCADE1_TIMER_REG_CTRL  0x04u  /* R/W : bit0 = enable             */

/* TIMER_CTRL bits. Any other bit is ignored on write and reads back as 0. */
#define ARCADE1_TIMER_CTRL_ENABLE 0x1u

/* --- Device state --------------------------------------------------------- */

typedef struct arcade1_timer {
    uint32_t count;   /* ticks since reset, while enabled */
    uint32_t ctrl;    /* only ARCADE1_TIMER_CTRL_ENABLE is defined */
} arcade1_timer_t;

/* Power-on: count = 0 and the timer is enabled. */
void arcade1_timer_reset(arcade1_timer_t *t);

/* Advance one tick if enabled. Called exactly once per completed machine
 * frame — never from wall-clock time. */
void arcade1_timer_tick(arcade1_timer_t *t);

/* MMIO handler pair for arcade1_set_mmio(); ctx is the arcade1_timer_t*.
 * TIMER_COUNT is read-only (writes ignored) and unknown offsets read 0 /
 * are ignored — deterministic, and not stray accesses because the handler is
 * attached. */
uint32_t arcade1_timer_mmio_read(void *ctx, uint32_t offset);
void arcade1_timer_mmio_write(void *ctx, uint32_t offset, uint32_t value);

#endif /* ARCADE1_TIMER_H */
