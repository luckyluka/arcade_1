/* wav probe (ROADMAP_V4 card 8.1): the smallest guest that makes a tone
 * the WAV dump can measure.
 *
 * It writes AUDIO_FREQ = 440 and AUDIO_CTRL = enable once, then presents
 * frames forever. The machine renders 800 stereo samples per frame from the
 * device state (audio_render: a 2^32 phase accumulator, square wave at
 * +/-8000, left == right), so
 *
 *     arcade1 run probe_wav.bin --frames 60 --audio out.wav
 *
 * is exactly one second of 440 Hz square wave: 48000 samples, 880 zero
 * crossings (2 per cycle). tests/test_audio_wav.sh measures that through
 * the file, byte-compares two dumps, and checks --audio moves no hash.
 */
#include "arcade1.h"

#define AUDIO_FREQ (*(volatile unsigned int *)(ARCADE1_MMIO_BASE + 0x300u))
#define AUDIO_CTRL (*(volatile unsigned int *)(ARCADE1_MMIO_BASE + 0x304u))

int main(void)
{
    AUDIO_FREQ = 440;   /* Hz; the register pair is the whole device */
    AUDIO_CTRL = 1;     /* bit0: tone enable                        */
    for (;;)
        arcade1_present();
}
