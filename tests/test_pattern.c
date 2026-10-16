/* ARCADE-1 — Milestone 02 test harness.
 *
 * Loads the built arcade1_libretro.so through the real Libretro frontend
 * callbacks, runs a few frames, and deterministically verifies the test
 * pattern: pixel format negotiation, frame geometry, the solid bars /
 * gradient bands, and that the bottom bars animate. Also dumps PPM frames
 * for manual inspection.
 */

#include "libretro.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 240

/* Captured most-recent frame from the core. */
static uint16_t cap[W * H];
static unsigned cap_w, cap_h, cap_pitch;
static int      frames_seen;
static int      got_rgb565;

static bool env_cb(unsigned cmd, void *data)
{
    if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) {
        enum retro_pixel_format *fmt = (enum retro_pixel_format *)data;
        got_rgb565 = (*fmt == RETRO_PIXEL_FORMAT_RGB565);
        return got_rgb565;   /* Only accept RGB565; core must match us. */
    }
    if (cmd == RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME) {
        return true;
    }
    return false;
}

static void video_cb(const void *data, unsigned width, unsigned height,
                     size_t pitch)
{
    cap_w = width;
    cap_h = height;
    cap_pitch = (unsigned)pitch;
    if (data)
        memcpy(cap, data, (size_t)height * pitch);
    frames_seen++;
}

static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
    (void)data;
    return frames;
}
static void   input_poll_cb(void) {}
static int16_t input_state_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{
    (void)a; (void)b; (void)c; (void)d;
    return 0;
}

/* RGB565 -> 8-bit channel helpers for assertions / PPM. */
static unsigned r5(uint16_t p) { return ((p >> 11) & 0x1Fu) * 255 / 31; }
static unsigned g6(uint16_t p) { return ((p >>  5) & 0x3Fu) * 255 / 63; }
static unsigned b5(uint16_t p) { return ( p        & 0x1Fu) * 255 / 31; }

static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%u %u\n255\n", cap_w, cap_h);
    for (unsigned y = 0; y < cap_h; y++)
        for (unsigned x = 0; x < cap_w; x++) {
            uint16_t p = cap[y * (cap_pitch / 2) + x];
            unsigned char rgb[3] = { (unsigned char)r5(p),
                                     (unsigned char)g6(p),
                                     (unsigned char)b5(p) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

int main(void)
{
    /* Frontend callbacks the core will hold. */
    retro_set_environment(env_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);

    retro_init();
    assert(retro_load_game(NULL) && "retro_load_game must succeed");
    assert(got_rgb565 && "core must negotiate RGB565 pixel format");

    /* --- Frame 0: check bands ------------------------------------------- */
    retro_run();
    assert(cap_w == W && cap_h == H && "frame geometry must be 320x240");
    assert(cap_pitch == W * 2 && "pitch must be width * 2 bytes");

    /* Top band, y=10: left pixel is bar 0 (white), right pixel is bar 7 (black). */
    assert(r5(cap[10 * W + 0]) == 255 && g6(cap[10 * W + 0]) == 255 &&
           b5(cap[10 * W + 0]) == 255 && "top-left must be white");
    assert(r5(cap[10 * W + (W - 1)]) == 0 && g6(cap[10 * W + (W - 1)]) == 0 &&
           b5(cap[10 * W + (W - 1)]) == 0 && "top-right must be black");

    /* Middle band, y=120: left is black (level 0), right is white (level 31). */
    assert(r5(cap[120 * W + 0]) == 0 && "gradient left must be black");
    assert(r5(cap[120 * W + (W - 1)]) == 255 &&
           b5(cap[120 * W + (W - 1)]) == 255 && "gradient right must be white");

    /* Bottom band, y=200: at x=19 on frame 0 -> black (see sx/20 parity). */
    uint16_t bottom_f0 = cap[200 * W + 19];
    assert(r5(bottom_f0) == 0 && "frame0 bottom x=19 must be black");

    dump_ppm("/tmp/opencode/frame0.ppm");

    /* --- Frame 1: bottom band must scroll by 1px ------------------------ */
    retro_run();
    uint16_t bottom_f1 = cap[200 * W + 19];
    assert(r5(bottom_f1) == 255 && "frame1 bottom x=19 must be white (scroll+1)");
    dump_ppm("/tmp/opencode/frame1.ppm");

    /* --- Determinism: reset returns us to the same frame 0 image -------- */
    uint16_t snapshot[W * H];
    retro_reset();
    retro_run();
    memcpy(snapshot, cap, sizeof(snapshot));
    retro_reset();
    retro_run();
    assert(memcmp(snapshot, cap, sizeof(snapshot)) == 0 &&
           "reset+run must reproduce identical frame (deterministic)");

    assert(frames_seen >= 3 && "core must have pushed every frame");

    printf("milestone02: all assertions passed\n");
    printf("  geometry   : %ux%u pitch=%u\n", cap_w, cap_h, cap_pitch);
    printf("  frames     : %d\n", frames_seen);
    printf("  rgb565     : %s\n", got_rgb565 ? "yes" : "no");
    printf("  dumped     : /tmp/opencode/frame0.ppm, frame1.ppm\n");

    retro_unload_game();
    retro_deinit();
    return 0;
}
