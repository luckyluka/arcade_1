/* ARCADE-1 — Milestone 04 test harness.
 *
 * Proves the input -> state -> rendering vertical slice by driving the real
 * arcade1_libretro.so. The square is drawn in a unique orange (RGB565 0xFD00)
 * that appears nowhere in the test pattern, so we can find its exact bounding
 * box by scanning the framebuffer and assert its position after simulated
 * D-pad input and after reset.
 */

#include "libretro.h"
#include "machine.h"     /* ARCADE1_WIDTH / ARCADE1_HEIGHT (M25) */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define W ARCADE1_WIDTH
#define H ARCADE1_HEIGHT
#define SQ_SIZE  32
#define SQ_SPEED 2

/* Unique square colour: pack_rgb565(31,40,0). */
#define SQ_COLOR ((uint16_t)((31u << 11) | (40u << 5) | 0u))

/* Simulated held-state for the 8 buttons, in pad[] order. */
static int sim_held[8];
static const unsigned sim_btn_id[8] = {
    RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN,
    RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
    RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B,
    RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT,
};

/* Button indices matching the above. */
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_START, B_SELECT };

static uint16_t cap[W * H];
static unsigned cap_pitch;
static int got_rgb565;

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

/* Find the bounding box of the orange square. Returns 0 if not found. */
static int find_square(int *minx, int *maxx, int *miny, int *maxy)
{
    int found = 0;
    *minx = W; *maxx = -1; *miny = H; *maxy = -1;
    for (int y = 0; y < H; y++) {
        const uint16_t *row = &cap[y * (cap_pitch / 2)];
        for (int x = 0; x < W; x++) {
            if (row[x] == SQ_COLOR) {
                found = 1;
                if (x < *minx) *minx = x;
                if (x > *maxx) *maxx = x;
                if (y < *miny) *miny = y;
                if (y > *maxy) *maxy = y;
            }
        }
    }
    return found;
}

static void set_buttons(unsigned mask)
{
    for (int i = 0; i < 8; i++)
        sim_held[i] = (mask >> i) & 1u;
}

/* Run n frames with the given held-button mask. */
static void run_frames(unsigned mask, int n)
{
    set_buttons(mask);
    for (int i = 0; i < n; i++)
        retro_run();
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

    int minx, maxx, miny, maxy;
    const int cx0 = (W - SQ_SIZE) / 2;   /* (480-32)/2 = 224 */
    const int cy0 = (H - SQ_SIZE) / 2;   /* (270-32)/2 = 119 */

    /* --- No input: square sits centred ---------------------------------- */
    run_frames(0, 1);
    assert(find_square(&minx, &maxx, &miny, &maxy) && "square must be present");
    assert(minx == cx0 && maxx == cx0 + SQ_SIZE - 1 &&
           miny == cy0 && maxy == cy0 + SQ_SIZE - 1 &&
           "square must start centred");

    /* --- Hold RIGHT 10 frames: x += 10*2 = 20 --------------------------- */
    run_frames(1u << B_RIGHT, 10);
    find_square(&minx, &maxx, &miny, &maxy);
    assert(minx == cx0 + 20 && maxx == cx0 + 20 + SQ_SIZE - 1 &&
           "square must move right by speed*frames");
    assert(miny == cy0 && maxy == cy0 + SQ_SIZE - 1 &&
           "square y must not change when only RIGHT held");

    /* --- Hold DOWN 5 frames: y += 5*2 = 10 ------------------------------ */
    run_frames(1u << B_DOWN, 5);
    find_square(&minx, &maxx, &miny, &maxy);
    assert(miny == cy0 + 10 && maxy == cy0 + 10 + SQ_SIZE - 1 &&
           "square must move down by speed*frames");
    assert(minx == cx0 + 20 && "square x must not change when only DOWN held");

    /* --- Hold LEFT 3 frames: x -= 3*2 = 6 -------------------------------- */
    run_frames(1u << B_LEFT, 3);
    find_square(&minx, &maxx, &miny, &maxy);
    assert(minx == cx0 + 20 - 6 && "square must move left by speed*frames");

    /* --- Diagonal: RIGHT+UP together ------------------------------------ */
    run_frames((1u << B_RIGHT) | (1u << B_UP), 4);
    find_square(&minx, &maxx, &miny, &maxy);
    assert(minx == cx0 + 14 + 8 && "diagonal must move x right");
    assert(miny == cy0 + 10 - 8 && "diagonal must move y up");

    /* --- Clamp left/top: hold LEFT+UP a lot -> clamps at 0,0 ------------- */
    run_frames((1u << B_LEFT) | (1u << B_UP), 500);
    find_square(&minx, &maxx, &miny, &maxy);
    assert(minx == 0 && maxx == SQ_SIZE - 1 && "clamp: square must stop at x=0");
    assert(miny == 0 && maxy == SQ_SIZE - 1 && "clamp: square must stop at y=0");

    /* --- Clamp right/bottom: hold RIGHT+DOWN a lot -> clamps at max ------ */
    run_frames((1u << B_RIGHT) | (1u << B_DOWN), 1000);
    find_square(&minx, &maxx, &miny, &maxy);
    assert(maxx == W - 1 && "clamp: square must stop at right edge");
    assert(maxy == H - 1 && "clamp: square must stop at bottom edge");

    /* --- retro_reset recenters ------------------------------------------ */
    retro_reset();
    run_frames(0, 1);
    find_square(&minx, &maxx, &miny, &maxy);
    assert(minx == cx0 && maxx == cx0 + SQ_SIZE - 1 &&
           miny == cy0 && maxy == cy0 + SQ_SIZE - 1 &&
           "reset must re-centre the square");

    printf("milestone04: all assertions passed\n");
    printf("  square colour : 0x%04X (unique in pattern)\n", SQ_COLOR);
    printf("  verified      : centred start, D-pad move (R/D/L), diagonal,\n");
    printf("                  edge clamping, reset re-centre\n");

    retro_unload_game();
    retro_deinit();
    return 0;
}
