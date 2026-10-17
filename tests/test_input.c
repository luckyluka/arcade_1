/* ARCADE-1 — Milestone 03 test harness.
 *
 * Proves RetroPad input reaches the core by driving the input_state_cb the way
 * a frontend would (simulating button presses), then verifying that the
 * matching indicator box lights up in the framebuffer. Also checks the input
 * descriptors were registered and that releasing a button dims it again.
 *
 * This exercises the real arcade1_libretro.so through its exported API.
 */

#include "libretro.h"
#include "machine.h"     /* ARCADE1_WIDTH / ARCADE1_HEIGHT (M25) */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define W ARCADE1_WIDTH
#define H ARCADE1_HEIGHT

/* Simulated held-state for the 8 buttons, indexed in pad[] order:
 * UP, DOWN, LEFT, RIGHT, A, B, START, SELECT. */
static int sim_held[8];

/* Must match pad_btn_id[] order in the core. */
static const unsigned sim_btn_id[8] = {
    RETRO_DEVICE_ID_JOYPAD_UP,
    RETRO_DEVICE_ID_JOYPAD_DOWN,
    RETRO_DEVICE_ID_JOYPAD_LEFT,
    RETRO_DEVICE_ID_JOYPAD_RIGHT,
    RETRO_DEVICE_ID_JOYPAD_A,
    RETRO_DEVICE_ID_JOYPAD_B,
    RETRO_DEVICE_ID_JOYPAD_START,
    RETRO_DEVICE_ID_JOYPAD_SELECT,
};

/* Captured frame. */
static uint16_t cap[W * H];
static unsigned cap_pitch;

/* Environment observation. */
static int got_rgb565;
static int got_descriptors;

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format *fmt = (enum retro_pixel_format *)data;
        got_rgb565 = (*fmt == RETRO_PIXEL_FORMAT_RGB565);
        return got_rgb565;
    }
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: {
        const struct retro_input_descriptor *d =
            (const struct retro_input_descriptor *)data;
        /* Count non-terminator entries; expect at least our 8 buttons. */
        int n = 0;
        while (d[n].description)
            n++;
        got_descriptors = (n >= 8);
        return true;
    }
    default:
        return false;
    }
}

static void video_cb(const void *data, unsigned width, unsigned height,
                     size_t pitch)
{
    (void)width; (void)height;
    cap_pitch = (unsigned)pitch;
    if (data)
        memcpy(cap, data, (size_t)H * pitch);
}

static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void   input_poll_cb(void) {}

/* Frontend-side input: report the simulated held-state for port 0. */
static int16_t input_state_cb(unsigned port, unsigned device,
                              unsigned index, unsigned id)
{
    if (port != 0 || device != RETRO_DEVICE_JOYPAD || index != 0)
        return 0;
    for (int i = 0; i < 8; i++)
        if (sim_btn_id[i] == id)
            return (int16_t)(sim_held[i] ? 1 : 0);
    return 0;
}

/* RGB565 -> 8-bit channel helpers. */
static unsigned r5(uint16_t p) { return ((p >> 11) & 0x1Fu) * 255 / 31; }
static unsigned g6(uint16_t p) { return ((p >>  5) & 0x3Fu) * 255 / 63; }
static unsigned b5(uint16_t p) { return ( p        & 0x1Fu) * 255 / 31; }

/* Mirror of the core's overlay layout (machine.c draw_pad_overlay): box=24,
 * gap=8, the whole row centred horizontally, y0 = 2*(H/3)+16. The lit inner
 * square's centre for button i is (x0 + i*(box+gap) + box/2, y0 + box/2). */
static uint16_t box_center(int i)
{
    const int box = 24, gap = 8;
    const int total = 8 * box + 7 * gap;
    const int x0 = (W - total) / 2;
    const int y0 = 2 * (H / 3) + 16;
    int x = x0 + i * (box + gap) + box / 2;
    int y = y0 + box / 2;
    return cap[y * (cap_pitch / 2) + x];
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
    assert(got_descriptors && "must register >=8 input descriptors");

    /* --- All released: every indicator dim ------------------------------ */
    memset(sim_held, 0, sizeof(sim_held));
    retro_run();
    for (int i = 0; i < 8; i++) {
        uint16_t p = box_center(i);
        assert(r5(p) < 64 && "all indicators must be dim when nothing held");
    }

    /* --- Press A (index 4, red) ----------------------------------------- */
    memset(sim_held, 0, sizeof(sim_held));
    sim_held[4] = 1;                     /* A */
    retro_run();
    {
        uint16_t pa = box_center(4);
        assert(r5(pa) == 255 && g6(pa) == 0 && b5(pa) == 0 &&
               "A indicator must be pure red when held");
        /* Others still dim. */
        for (int i = 0; i < 8; i++)
            if (i != 4)
                assert(r5(box_center(i)) < 64 && "others must stay dim");
    }

    /* --- Press UP (index 0) + LEFT (index 2) simultaneously ------------- */
    memset(sim_held, 0, sizeof(sim_held));
    sim_held[0] = 1;                     /* UP    (white) */
    sim_held[2] = 1;                     /* LEFT  (cyan)  */
    retro_run();
    {
        uint16_t pup  = box_center(0);
        uint16_t pled = box_center(2);
        assert(r5(pup) == 255 && g6(pup) == 255 && b5(pup) == 255 &&
               "UP indicator must be white when held");
        assert(r5(pled) == 0 && g6(pled) == 255 && b5(pled) == 255 &&
               "LEFT indicator must be cyan when held");
    }

    /* --- Release everything: back to dim -------------------------------- */
    memset(sim_held, 0, sizeof(sim_held));
    retro_run();
    for (int i = 0; i < 8; i++)
        assert(r5(box_center(i)) < 64 && "indicators must dim on release");

    printf("milestone03: all assertions passed\n");
    printf("  descriptors : %s\n", got_descriptors ? "registered" : "MISSING");
    printf("  rgb565      : %s\n", got_rgb565 ? "yes" : "no");
    printf("  input->overlay verified for A, UP, LEFT + release\n");

    retro_unload_game();
    retro_deinit();
    return 0;
}
