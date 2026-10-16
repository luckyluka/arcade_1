/* ARCADE-1 audio device (milestone 15) implementation.
 *
 * See audio.h. Register state only; the machine renders the waveform from it
 * every frame (audio_render in machine.c).
 */

#include "audio.h"

/* --- Lifecycle ------------------------------------------------------------ */

void arcade1_audio_reset(arcade1_audio_dev_t *a)
{
    a->freq = ARCADE1_AUDIO_DEFAULT_HZ;   /* 440 Hz: the M05 demo beep */
    a->ctrl = ARCADE1_AUDIO_CTRL_ENABLE;
}

/* --- MMIO handlers (ctx is the arcade1_audio_dev_t*) ---------------------- */

uint32_t arcade1_audio_mmio_read(void *ctx, uint32_t offset)
{
    const arcade1_audio_dev_t *a = ctx;

    if (offset == ARCADE1_AUDIO_REG_FREQ)
        return a->freq;
    if (offset == ARCADE1_AUDIO_REG_CTRL)
        return a->ctrl;
    return 0u;
}

void arcade1_audio_mmio_write(void *ctx, uint32_t offset, uint32_t value)
{
    arcade1_audio_dev_t *a = ctx;

    if (offset == ARCADE1_AUDIO_REG_FREQ) {
        a->freq = value;   /* Hz; 0 produces a constant level while enabled */
        return;
    }
    if (offset == ARCADE1_AUDIO_REG_CTRL) {
        a->ctrl = value & ARCADE1_AUDIO_CTRL_ENABLE;
        return;
    }
    /* Unknown register: ignored. */
}
