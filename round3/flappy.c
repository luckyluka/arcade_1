/* ARCADE-1 game: FLAPPY — a one-button timing game.
 *
 * A bird of fixed horizontal position is pulled down by gravity; pressing A
 * flaps it upward. Pipe pairs scroll right to left, each with a gap; passing
 * one scores +1. Hitting a pipe, the ground or the ceiling ends the run:
 * "GAME OVER" is shown and A (or START) restarts with the score reset.
 *
 * Everything observable is a file-scope global, so the tools can read it out
 * of guest RAM by name:
 *     arcade1 run flappy.arc1 --frames 480 --input input.txt \
 *         --vars score,state,bird_y,bird_vy --var-every 30
 *
 * Determinism: integer maths only, no wall clock; the only randomness is
 * arcade1_rand() (the seeded LCG), consumed in program order on pipe respawn.
 * The same input script always replays bit-identically.
 *
 * Controls: A = flap (edge-triggered) / restart. START also restarts.
 */

#include "arcade1.h"          /* pad, draw commands, rng */
#include "text.h"             /* opt-in 5x7 font: text_draw / text_u32 */

/* --- screen / world geometry -------------------------------------------- */
#define SCREEN_W 480          /* the screen is 480x270, RGB565 */
#define SCREEN_H 270
#define GROUND_H 20
#define GROUND_Y (SCREEN_H - GROUND_H)   /* 250: first ground row */

#define BIRD_X  96            /* the bird never moves horizontally */
#define BIRD_W  10
#define BIRD_H  8

#define PIPE_W        26
#define PIPE_GAP      64
#define PIPE_SPACING  150     /* distance between pipe pairs */
#define PIPE_SPEED    2       /* pixels scrolled per frame */
#define PIPE_N        4       /* enough pairs to cover a screen and a bit */
#define PIPE_FIRST_X  260     /* first pipe's spawn x (off the bird) */

/* --- physics (fixed point: y and vy in quarter-pixels) ------------------- */
#define GRAV     1            /* +0.25 px/frame^2 */
#define FLAP     (-8)         /* -2 px/frame impulse */
#define MAX_FALL 14           /* 3.5 px/frame terminal velocity */

#define GAP_MIN_TOP 24        /* gap top row range: 24 .. 162 */

/* --- colours (RGB565) ---------------------------------------------------- */
#define COL_SKY    0x7E5Cu    /* #7EC8E3 */
#define COL_GROUND 0x8AE7u    /* #8B5E3C */
#define COL_PIPE   0x4D6Au    /* #4CAF50 */
#define COL_BIRD   0xFE69u    /* #FFD34D */
#define COL_TEXT   0xFFFFu
#define COL_DEAD   0xF800u    /* red */

/* --- observable state (file-scope globals: the tools read these) --------- */
static int state     = 0;     /* 0 = play, 1 = game over */
static int score     = 0;     /* pipes passed */
static int bird_y    = 110;   /* bird top row, pixels */
static int bird_y4   = 440;   /* bird_y in quarter-pixels */
static int bird_vy   = 0;     /* quarter-pixels per frame */
static int pad_prev  = 0;     /* last frame's pad (for edge detection) */

static int pipe_x[PIPE_N];       /* each pair's left column, pixels */
static int pipe_gap[PIPE_N];     /* each pair's gap top row, pixels */
static int pipe_scored[PIPE_N];  /* 1 once this pair has been passed */

/* Put the world back to frame-0 conditions (score included). The LCG is
 * deliberately NOT reseeded: a restarted session keeps consuming the same
 * deterministic stream the first run left off at. */
static void world_reset(void)
{
    int i;
    score   = 0;
    bird_y  = 110;
    bird_y4 = 110 * 4;
    bird_vy = 0;
    for (i = 0; i < PIPE_N; i++) {
        pipe_x[i]      = PIPE_FIRST_X + i * PIPE_SPACING;
        /* deterministic, friendly opening pattern: first gap centred on the
         * bird's starting altitude, the rest varied by a fixed formula */
        pipe_gap[i]    = 80 + ((i * 41) % 110);
        pipe_scored[i] = 0;
    }
}

/* One simulation step. `edged_a` / `edged_start` are 1 when that button went
 * from up to down this frame (flap and restart are press events, not holds). */
static void game_update(unsigned edged_a, unsigned edged_start)
{
    int i, crash = 0;

    if (state != 0) {                       /* game over: only restart matters */
        if (edged_a || edged_start) {
            world_reset();
            state = 0;
        }
        return;
    }

    if (edged_a) bird_vy = FLAP;
    bird_vy += GRAV;
    if (bird_vy > MAX_FALL) bird_vy = MAX_FALL;
    bird_y4 += bird_vy;

    /* ceiling and ground both end the run */
    if (bird_y4 < 0) {
        bird_y4 = 0;
        crash = 1;
    }
    if (bird_y4 >= (GROUND_Y - BIRD_H) * 4) {
        bird_y4 = (GROUND_Y - BIRD_H) * 4;
        crash = 1;
    }
    bird_y = bird_y4 >> 2;

    for (i = 0; i < PIPE_N; i++) {
        pipe_x[i] -= PIPE_SPEED;
        if (pipe_x[i] <= -PIPE_W) {         /* off the left edge: recycle */
            pipe_x[i] += PIPE_SPACING * PIPE_N;
            pipe_gap[i] = GAP_MIN_TOP +
                arcade1_rand(GROUND_Y - 2 * GAP_MIN_TOP - PIPE_GAP + 1);
            pipe_scored[i] = 0;
        }
        /* scoring: the pair's right edge cleared the bird's left edge */
        if (!pipe_scored[i] && pipe_x[i] + PIPE_W < BIRD_X) {
            pipe_scored[i] = 1;
            score++;
        }
        /* collision: horizontal overlap + vertical miss of the gap */
        if (BIRD_X < pipe_x[i] + PIPE_W && BIRD_X + BIRD_W > pipe_x[i]) {
            if (bird_y < pipe_gap[i] ||
                bird_y + BIRD_H > pipe_gap[i] + PIPE_GAP)
                crash = 1;
        }
    }
    if (crash) state = 1;
}

static void draw_world(void)
{
    int i, gy;
    arcade1_clear(COL_SKY);
    arcade1_rect(0, GROUND_Y, SCREEN_W, GROUND_H, COL_GROUND);
    for (i = 0; i < PIPE_N; i++) {
        gy = pipe_gap[i];
        if (pipe_x[i] > SCREEN_W || pipe_x[i] + PIPE_W < 0)
            continue;                       /* fully off screen, skip */
        arcade1_rect(pipe_x[i], 0, PIPE_W, gy, COL_PIPE);
        arcade1_rect(pipe_x[i], gy + PIPE_GAP, PIPE_W,
                     GROUND_Y - gy - PIPE_GAP, COL_PIPE);
    }
    arcade1_rect(BIRD_X, bird_y, BIRD_W, BIRD_H, COL_BIRD);
}

static void draw_hud(void)
{
    arcade1_cam(0, 0);      /* text is a world-space sprite: pin it for HUD */
    text_u32(SCREEN_W / 2 - 8, 8, (unsigned)score, 2, COL_TEXT);
    if (state != 0) {
        text_draw(SCREEN_W / 2 - 36, 100, "GAME OVER", COL_DEAD);
        text_draw(SCREEN_W / 2 - 28, 116, "PRESS A", COL_TEXT);
    }
}

int main(void)
{
    world_reset();
    for (;;) {                             /* one iteration == one 60 fps frame */
        unsigned p     = arcade1_pad();
        unsigned edged = p & ~pad_prev;
        pad_prev = p;
        game_update(edged & BTN_A, edged & BTN_START);
        draw_world();
        draw_hud();
        arcade1_present();                 /* this write ENDS the frame */
    }
    return 0;
}
