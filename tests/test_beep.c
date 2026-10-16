/* ARCADE-1 — Milestone 05 test harness.
 *
 * Proves the audio beep by capturing the batch audio callback from the real
 * arcade1_libretro.so and verifying:
 *   - the exact number of stereo frames per video frame (48000/60 = 800);
 *   - the signal is a square wave (only two distinct levels, +AMP / -AMP);
 *   - left and right channels are identical (mono beep);
 *   - the tone frequency is 440 Hz (measured from polarity transitions);
 *   - it is deterministic: reset() restarts the identical waveform.
 */

#include "libretro.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define W 320
#define H 240
#define RATE 48000
#define FPS  60
#define FRAMES_PER_RUN (RATE / FPS)          /* 800 */
#define BEEP_AMP 8000
#define BEEP_HZ  440

/* Captured audio from the most recent retro_run(). */
static int16_t acap[FRAMES_PER_RUN * 2];
static size_t  aframes;
static int     aruns;
static int     got_rgb565;

static bool env_cb(unsigned cmd, void *data)
{
    if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) {
        enum retro_pixel_format *fmt = (enum retro_pixel_format *)data;
        got_rgb565 = (*fmt == RETRO_PIXEL_FORMAT_RGB565);
        return got_rgb565;
    }
    if (cmd == RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME ||
        cmd == RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS)
        return true;
    return false;
}

static void video_cb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }

/* Batch audio sink: record the frames the core produced this run. */
static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
    assert(frames <= FRAMES_PER_RUN && "core must not overflow per-frame budget");
    memcpy(acap, data, frames * 2 * sizeof(int16_t));
    aframes = frames;
    aruns++;
    return frames;
}

static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned port, unsigned device,
                              unsigned index, unsigned id)
{
    (void)port; (void)device; (void)index; (void)id;
    return 0;
}

int main(void)
{
    retro_set_environment(env_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);

    retro_init();
    assert(retro_load_game(NULL) && "load must succeed");
    assert(got_rgb565 && "must negotiate RGB565");

    /* --- One frame: sample count + square-wave shape --------------------- */
    aruns = 0; aframes = 0;
    retro_run();
    assert(aruns == 1 && "core must call batch audio once per frame");
    assert(aframes == FRAMES_PER_RUN &&
           "core must emit exactly 800 stereo frames per video frame");

    /* Every sample must be either +AMP or -AMP (square wave), L == R. */
    int pos = 0, neg = 0;
    for (size_t i = 0; i < aframes; i++) {
        int16_t l = acap[i * 2], r = acap[i * 2 + 1];
        assert(l == r && "left and right must match (mono beep)");
        assert((l == (int16_t)BEEP_AMP || l == (int16_t)-BEEP_AMP) &&
               "sample must be +AMP or -AMP (square wave)");
        if (l > 0) pos++; else neg++;
    }
    assert(pos > 0 && neg > 0 && "square wave must have both polarities");

    /* --- Frequency: count polarity transitions across many frames -------- */
    /* Run 60 frames (= 1 second) and count how many times the wave flips
     * sign. A 440 Hz square wave flips twice per cycle => ~880 flips/sec.
     * We count only within-frame transitions plus frame-boundary continuity
     * by tracking the last sample across calls. */
    long flips = 0;
    int16_t last;
    int have_last = 0;

    /* Re-capture a full second worth of frames, stitching boundaries. */
    for (int f = 0; f < FPS; f++) {
        retro_run();
        for (size_t i = 0; i < aframes; i++) {
            int16_t s = acap[i * 2];
            if (have_last && ((last < 0 && s >= 0) || (last >= 0 && s < 0)))
                flips++;
            last = s;
            have_last = 1;
        }
    }
    /* 440 Hz * 2 flips = 880 expected. Allow small tolerance for the fixed
     * -point step rounding. */
    assert(flips >= 860 && flips <= 900 &&
           "measured square-wave flips must match 440 Hz (880/s)");

    /* --- Determinism: reset() restarts the identical waveform ----------- */
    uint32_t snap[FRAMES_PER_RUN];
    retro_reset();
    retro_run();
    for (size_t i = 0; i < aframes; i++)
        snap[i] = (uint16_t)acap[i * 2];
    retro_reset();
    retro_run();
    for (size_t i = 0; i < aframes; i++)
        assert(snap[i] == (uint16_t)acap[i * 2] &&
               "reset must restart an identical beep waveform");

    printf("milestone05: all assertions passed\n");
    printf("  frames/run  : %zu (expect %d)\n", aframes, FRAMES_PER_RUN);
    printf("  waveform    : square, L==R, +/-%d\n", BEEP_AMP);
    printf("  frequency   : %ld flips/sec (expect ~880 = %d Hz)\n",
           flips, BEEP_HZ);
    printf("  deterministic: yes (reset reproduces waveform)\n");

    retro_unload_game();
    retro_deinit();
    return 0;
}
