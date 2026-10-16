/* ARCADE-1 — Milestone 06 test harness.
 *
 * Proves the machine is usable WITHOUT RetroArch. This links directly against
 * src/arcade1/machine.c (no libretro, no .so) and drives it purely through the
 * arcade1_* API. It verifies the same behaviours the libretro tests do, so a
 * passing run shows the extraction preserved machine semantics:
 *   - reset puts the square at screen centre;
 *   - input moves the square (input -> state -> rendering);
 *   - the framebuffer contains the expected RGB565 square colour;
 *   - audio produces the deterministic 440 Hz beep;
 *   - reset restarts the identical frame (determinism).
 */

#include "machine.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Orange square colour, matching machine.c. */
#define SQ_COLOR ((uint16_t)0xFD00)
#define SQ_SIZE  32
#define BEEP_AMP 8000

static arcade1_machine_t m;   /* Static: large struct (holds framebuffer). */

/* Find the bounding box of the orange square in the framebuffer. */
static int find_square(int *minx, int *maxx, int *miny, int *maxy)
{
    const unsigned pitch = arcade1_framebuffer_pitch(&m) / 2; /* px/row */
    const uint16_t *fb   = arcade1_framebuffer(&m);
    int found = 0;
    *minx = ARCADE1_WIDTH; *maxx = -1;
    *miny = ARCADE1_HEIGHT; *maxy = -1;

    for (int y = 0; y < ARCADE1_HEIGHT; y++)
        for (int x = 0; x < ARCADE1_WIDTH; x++)
            if (fb[y * pitch + x] == SQ_COLOR) {
                found = 1;
                if (x < *minx) *minx = x;
                if (x > *maxx) *maxx = x;
                if (y < *miny) *miny = y;
                if (y > *maxy) *maxy = y;
            }
    return found;
}

int main(void)
{
    const int cx0 = (ARCADE1_WIDTH  - SQ_SIZE) / 2;   /* 144 */
    const int cy0 = (ARCADE1_HEIGHT - SQ_SIZE) / 2;   /* 104 */
    int minx, maxx, miny, maxy;

    /* --- init + reset: square centred ----------------------------------- */
    arcade1_init(&m);
    arcade1_reset(&m);

    arcade1_input_t none; memset(&none, 0, sizeof(none));
    arcade1_set_input(&m, &none);
    arcade1_run_frame(&m);

    assert(find_square(&minx, &maxx, &miny, &maxy) && "square must be drawn");
    assert(minx == cx0 && maxx == cx0 + SQ_SIZE - 1 &&
           miny == cy0 && maxy == cy0 + SQ_SIZE - 1 &&
           "reset must centre the square");

    /* --- geometry / accessors ------------------------------------------- */
    assert(arcade1_framebuffer_pitch(&m) == ARCADE1_WIDTH * 2 &&
           "pitch must be width * 2 bytes");
    assert(arcade1_audio_frames(&m) == ARCADE1_AUDIO_FRAMES &&
           "audio frames must be 48000/60 = 800");

    /* --- input -> state: hold RIGHT, square moves right ----------------- */
    arcade1_input_t right; memset(&right, 0, sizeof(right));
    right.right = true;
    arcade1_set_input(&m, &right);
    arcade1_run_frame(&m);

    find_square(&minx, &maxx, &miny, &maxy);
    assert(minx == cx0 + 2 && "RIGHT must move square right by 2 px/frame");
    assert(miny == cy0 && "RIGHT must not change y");

    /* --- audio: deterministic square-wave beep -------------------------- */
    const int16_t *a   = arcade1_audio(&m);
    unsigned       n   = arcade1_audio_frames(&m);
    int pos = 0, neg = 0;
    for (unsigned i = 0; i < n; i++) {
        assert(a[i*2] == a[i*2+1] && "left must equal right (mono)");
        assert((a[i*2] == (int16_t)BEEP_AMP || a[i*2] == (int16_t)-BEEP_AMP) &&
               "sample must be +/-AMP (square wave)");
        if (a[i*2] > 0) pos++; else neg++;
    }
    assert(pos > 0 && neg > 0 && "beep must have both polarities");

    /* --- determinism: reset reproduces the identical frame --------------- */
    /* Establish a known frame, snapshot it, then reset and replay the exact
     * same input sequence; the two frames must be byte-identical. */
    arcade1_reset(&m);
    arcade1_set_input(&m, &none);
    arcade1_run_frame(&m);

    uint16_t snap[ARCADE1_FB_PIXELS];
    memcpy(snap, arcade1_framebuffer(&m), sizeof(snap));

    arcade1_reset(&m);
    arcade1_set_input(&m, &none);
    arcade1_run_frame(&m);
    assert(memcmp(snap, arcade1_framebuffer(&m), sizeof(snap)) == 0 &&
           "reset + same input must reproduce an identical frame");

    /* Also: a moved frame must be reproducible by replaying the same moves. */
    arcade1_reset(&m);
    arcade1_set_input(&m, &right);
    arcade1_run_frame(&m);
    arcade1_run_frame(&m);           /* two RIGHT frames */
    uint16_t snap2[ARCADE1_FB_PIXELS];
    memcpy(snap2, arcade1_framebuffer(&m), sizeof(snap2));

    arcade1_reset(&m);
    arcade1_set_input(&m, &right);
    arcade1_run_frame(&m);
    arcade1_run_frame(&m);
    assert(memcmp(snap2, arcade1_framebuffer(&m), sizeof(snap2)) == 0 &&
           "replaying the same input sequence must be deterministic");

    printf("milestone06: all assertions passed\n");
    printf("  machine driven standalone (no libretro, no .so)\n");
    printf("  verified: reset/centre, input->state->render, audio beep,\n");
    printf("            determinism via reset\n");

    return 0;
}
