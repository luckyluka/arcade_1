/* edge probe (ROADMAP_V4 card 4.3): the smallest guest that shows the
 * edge-trigger trap.
 *
 * A press is an EDGE, not a hold. This guest counts what the A button did:
 *
 *   presses   every A edge it saw (a press, whether or not it did anything)
 *   flaps     edges spent on a flap (the game was playing)
 *   restarts  edges spent on a restart (the game was over)
 *   state     0 = playing, 1 = over
 *   dead_frame the frame the run ended on (-1 = still alive)
 *
 * The model is a bird with a small amount of life: `alt` frames. Every frame
 * it loses one; at zero the run ends. A flap buys FLAP_ALT frames, a restart
 * (only possible while over) buys GROUND_ALT and clears the score.
 *
 * The trap this pins: a script that HOLDS A across the restart produces ONE
 * edge, and that edge is spent on the restart — so the flaps the author
 * meant never happen, the bird falls, and the session diverges from the
 * plan. The fix is to release A for one frame, so the next press is a new
 * edge (see SDK.md and GAME_DEV_GUIDE.md, "Edge-triggered buttons and
 * input scripts"; tests/test_pad_trace.sh runs both scripts).
 *
 * Nothing here is timing-dependent: the frame counter is the loop itself
 * (one iteration = one frame, because every iteration presents).
 */
#include "arcade1.h"

#define GROUND_ALT 4     /* frames of life a fresh run buys                */
#define FLAP_ALT   12    /* frames of life one flap buys                   */

/* The observable globals are NOT static: the tools read them out of guest
 * RAM by name (an `nm` dump), and a static that nothing reads is deleted
 * by the compiler — it would vanish from the dump and from the test. */
int state;        /* 0 = playing, 1 = over                          */
int alt = GROUND_ALT;  /* frames of life left (a fresh run)            */
int presses;      /* A edges seen                                   */
int flaps;        /* edges spent on a flap                           */
int restarts;     /* edges spent on a restart                        */
int dead_frame = -1;  /* frame the run ended on (-1 = still alive)       */
int frames;       /* frames executed                                */
static int prev_a;

int main(void)
{
    for (;;) {
        unsigned pad = arcade1_pad();
        unsigned a = (pad & BTN_A) != 0u;
        unsigned edge = a && !prev_a;

        prev_a = a;
        frames++;

        if (edge) {
            presses++;
            if (state) {                  /* over: the edge is the restart */
                restarts++;
                state = 0;
                alt = GROUND_ALT;
                dead_frame = -1;
            } else {                      /* playing: the edge is a flap   */
                flaps++;
                alt = FLAP_ALT;
            }
        }

        if (!state) {
            alt--;
            if (alt <= 0) {
                state = 1;
                dead_frame = frames;
            }
        }

        /* Draw the state: a bar whose height is the life left, red when
         * the run is over. The pixels are not the point — the globals are. */
        arcade1_clear(state ? 0x0800u : 0x0018u);
        arcade1_rect(20, 240 - (alt > 0 ? alt * 8 : 0), 24,
                     alt > 0 ? alt * 8 : 0, state ? 0xF800u : 0x07E0u);
        arcade1_present();
    }
}
