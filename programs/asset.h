#ifndef ARCADE1_ASSET_H
#define ARCADE1_ASSET_H
#include "arcade1.h"

/* asset.h — typed accessors over arc1-assetc output (ROADMAP_CREATION_FLOW
 * Wave 2). tools/arc1_assetc.py emits, per sprite, a per-frame ref array
 *
 *     static const sprite4_ref hero_frames[HERO_FRAMES] = {…};
 *
 * so game code addresses art by frame, never by stride arithmetic. A ref
 * carries the frame's pixels, geometry and palette bank, plus the gameplay
 * metadata declared in assets.json ("origin" and "collision"). The emitted
 * header names sprite4_ref, so include THIS header before it:
 *
 *     #include "asset.h"            // sprite4_ref + asset_sprite4
 *     #include "assets.h"           // the generated header
 *
 * Names resolve at build time only (AssetEcosystem.md §13): the manifest's
 * "name" becomes a C identifier; nothing on the target looks art up by
 * string. */

/* One drawn frame. Field order is the emitter's initializer order:
 *   data         this frame's 4-bit pixels (sheet + frame * STRIDE)
 *   w, h         frame size in pixels (w even)
 *   bank         palette bank the indices address (0..15)
 *   ox, oy       origin: the anchor INSIDE the frame ([0,0] = top-left;
 *                [w/2, h] is the classic bottom-centre feet anchor — the
 *                edge itself is allowed)
 *   cx, cy, cw, ch
 *                collision box inside the frame; cw = ch = 0 = no box */
typedef struct sprite4_ref {
    const unsigned char *data;
    int w, h;
    int bank;
    int ox, oy;
    int cx, cy, cw, ch;
} sprite4_ref;

/* Draw one ref at top-left (x, y): the ref carries pixels, size and bank,
 * so the call site repeats nothing. flags bit0 = flip X, bit1 = flip Y —
 * the same bits arcade1_sprite4() takes; the device mirrors within the
 * frame, so one sheet serves both directions. Placement by a ref's own
 * metadata reads:
 *     asset_sprite4(&hero_frames[f], x - hero_frames[f].ox,
 *                                       y - hero_frames[f].oy, flip); */
static inline void asset_sprite4(const sprite4_ref *ref, int x, int y,
                                 unsigned flags)
{
    arcade1_sprite4(x, y, ref->data, ref->w, ref->h,
                    (unsigned)ref->bank, flags);
}

/* --- Animation (Wave 3) ---------------------------------------------------
 * assets.json "animations" emits, per animation, a frame table
 *
 *     static const sprite4_ref *const run_frames[RUN_STEPS] = {…};
 *
 * (refs into the sprite's frames, in play order). An anim_state is how a
 * game PLAYS such a table — loop, ping-pong or once — and it is timed by
 * the frame timer (arcade1_timer_count(), +1 per machine frame; never host
 * time), so playback is a pure function of frames and identical across
 * hosts. Call anim_tick() once per frame with the timer's current reading;
 * it returns the current step's ref, ready for asset_sprite4(). */

#define ANIM_LOOP      0   /* wrap from the last step back to the first */
#define ANIM_PINGPONG  1   /* forward to the end, then back, no repeats */
#define ANIM_ONCE      2   /* advance to the last step, then hold (done) */

typedef struct anim_state {
    const sprite4_ref *const *frames;   /* the emitted NAME_frames table */
    int steps;                          /* entries in it */
    int ms;                             /* ms per step; <= 0 holds the step */
    int mode;                           /* ANIM_LOOP / _PINGPONG / _ONCE */
    unsigned last, sub;                 /* timer reading + 1/60 ms carry */
    int step;                           /* current table index */
    int dir;                            /* ping-pong travel: +1 / -1 */
    int done;                           /* ANIM_ONCE reached its last step */
} anim_state;

/* Initialiser for a file-scope state:
 *     static anim_state run = ANIM(run_frames, RUN_MS, ANIM_LOOP);      */
#define ANIM(frames, ms, mode) \
    { (frames), (int)(sizeof(frames) / sizeof((frames)[0])), (ms), (mode), \
      0u, 0u, 0, 1, 0 }

/* Restart from the state's first step, anchored at `timer` — call on state
 * changes so the new animation doesn't inherit the old carry (walkdemo
 * resets on the step-off so the walk always begins in the contact pose). */
static inline void anim_reset(anim_state *a, unsigned timer)
{
    a->last = timer;
    a->sub = 0u;
    a->step = 0;
    a->dir = 1;
    a->done = 0;
}

/* Advance by the frame time elapsed since a's last call and return the
 * current step's ref. Time is carried in units of 1/60 ms and one frame is
 * exactly 1000 of them, so steps land without drift (80 ms = 4.8 frames =
 * 4800 units). A stall catches up at most 10 s (dt clamp), and the unsigned
 * subtraction keeps the timer's wrap-around safe. A step length of 0 (or
 * less) ms holds the current step and drops the carry — a state that was
 * never given a duration used to spin the catch-up loop forever. */
static inline const sprite4_ref *anim_tick(anim_state *a, unsigned timer)
{
    unsigned dt = timer - a->last;
    unsigned period;

    a->last = timer;
    if (a->ms <= 0) {                   /* no step length: hold, never hang */
        a->sub = 0u;
        return a->frames[a->step];
    }
    if (dt > 600u)
        dt = 600u;                       /* 10 s: a stall, not fast-forward */
    a->sub += dt * 1000u;
    period = (unsigned)a->ms * 60u;
    while (a->sub >= period) {
        a->sub -= period;
        if (a->done)
            break;                       /* ANIM_ONCE holds its last step */
        if (a->mode == ANIM_PINGPONG) {
            if (a->steps > 1) {
                a->step += a->dir;
                if (a->step >= a->steps) { a->step = a->steps - 2; a->dir = -1; }
                else if (a->step < 0)    { a->step = 1;               a->dir = 1; }
            }
        } else if (a->mode == ANIM_ONCE) {
            if (a->step + 1 < a->steps)
                a->step++;
            else
                a->done = 1;
        } else {                         /* ANIM_LOOP */
            a->step++;
            if (a->step >= a->steps)
                a->step = 0;
        }
    }
    return a->frames[a->step];
}

#endif
