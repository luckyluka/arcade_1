/* ARCADE-1 audio device (milestone 15).
 *
 * Registers for guest-driven tone generation — "basic audio" for Breakout
 * (docs.md §19). Decisions pinned in docs.md ("Firmware & frame protocol,
 * pinned at M15"):
 *
 *   - Powers ON at the historical demo tone: 440 Hz, enabled. That keeps the
 *     M05 beep behaviour identical for the content-less core (test_beep).
 *   - The guest reprograms AUDIO_FREQ per event (brick/paddle/death) and
 *     gates it with AUDIO_CTRL bit0 for short beeps; CTRL off = silence.
 *   - The actual waveform is rendered by the machine each frame AFTER the
 *     guest has run (audio_render in machine.c), so a register write at frame
 *     N already shapes frame N's samples.
 *   - Amplitude is fixed at +/-8000 (square wave) for now; no PCM, no mixing,
 *     no channels.
 *
 * No dependency on libretro or any host frontend.
 */

#ifndef ARCADE1_AUDIO_H
#define ARCADE1_AUDIO_H

#include <stdint.h>

/* --- Registers (byte offsets within the 0x4000 MMIO window) --------------- */

#define ARCADE1_AUDIO_REG_BASE 0x300u  /* first audio offset; mux split       */
#define ARCADE1_AUDIO_REG_FREQ 0x300u  /* R/W : tone frequency in Hz          */
#define ARCADE1_AUDIO_REG_CTRL 0x304u  /* R/W : bit0 = tone enable            */

/* AUDIO_CTRL bits. Any other bit is ignored on write and reads as 0. */
#define ARCADE1_AUDIO_CTRL_ENABLE 0x1u

/* Power-on tone: the demo beep (M05) survives as the default state. */
#define ARCADE1_AUDIO_DEFAULT_HZ 440u

/* --- Device state --------------------------------------------------------- */

typedef struct arcade1_audio_dev {
    uint32_t freq;   /* tone frequency in Hz (0 = constant level while on) */
    uint32_t ctrl;   /* only ARCADE1_AUDIO_CTRL_ENABLE is defined           */
} arcade1_audio_dev_t;

/* Power-on: 440 Hz, enabled — the waveform test_beep.c expects. */
void arcade1_audio_reset(arcade1_audio_dev_t *a);

/* MMIO handler pair for the machine's muxed window handler (docs.md M11
 * contract); ctx is the arcade1_audio_dev_t*. Unknown offsets read 0 /
 * ignore writes. */
uint32_t arcade1_audio_mmio_read(void *ctx, uint32_t offset);
void arcade1_audio_mmio_write(void *ctx, uint32_t offset, uint32_t value);

#endif /* ARCADE1_AUDIO_H */
