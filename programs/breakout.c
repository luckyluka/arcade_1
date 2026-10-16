/* ARCADE-1 Breakout — milestone 15's game, as a GUEST program.
 *
 * Runs entirely on the RV32I interpreter inside the machine: it reads the
 * M12 timer and M15 input/audio registers, draws rectangles through gfx.h
 * (M14 -> M13 video), and presents every frame with gfx_present, which is the
 * frame boundary the machine runs it to (docs.md M15 "Firmware and frame
 * protocol"). One loop iteration = one 60 fps host frame.
 *
 * Demonstrates: input (INPUT_PAD), timing (TIMER_COUNT drives beep duration
 * and the death pause), a game loop, rectangles, collision, score, lives,
 * game state, and basic audio (guest-programmed tone events).
 *
 * Game states (the test reads the `state` global):
 *   0 ST_SERVE  ball sits on the paddle; press A to launch
 *   1 ST_PLAY   ball in flight
 *   2 ST_DEAD   life lost, pause DEAD_PAUSE timer ticks, then ST_SERVE
 *   3 ST_OVER   all lives gone; press START to restart
 *   4 ST_WIN    all bricks cleared; press START to restart
 *
 * Controls: left/right steer the paddle, A launches, START restarts.
 *
 * Guest constraint: built -march=rv32i without libgcc, so no variable
 * multiplication or division anywhere (constant folding to shifts/adds is
 * fine); score digits are produced by subtraction.
 */

#include "gfx.h"

/* --- MMIO registers (fixed map, see docs.md table) ----------------------- */

#define MMIO_BASE      0x4000u
#define REG_TIMER_COUNT (MMIO_BASE + 0x00u)  /* M12: ticks once per frame   */
#define REG_INPUT_PAD   (MMIO_BASE + 0x200u) /* M15: read-only pad          */
#define REG_AUDIO_FREQ  (MMIO_BASE + 0x300u) /* M15: tone in Hz             */
#define REG_AUDIO_CTRL  (MMIO_BASE + 0x304u) /* M15: bit0 = enable          */

#define PAD_LEFT   0x04u
#define PAD_RIGHT  0x08u
#define PAD_A      0x10u
#define PAD_START  0x40u

/* --- Geometry (the test probes pixels at these places) ------------------- */

#define W 320
#define H 240

#define SCORE_X     4        /* top-left: 4 digits, 3x5 font at scale 2     */
#define SCORE_Y     4
#define DIGIT_ADV   10       /* digit pitch (6 wide + 4 gap)                 */
#define SCORE_DIGITS 4

#define LIFE_X      312      /* top-right: one 8x6 rect per remaining life  */
#define LIFE_Y      4
#define LIFE_W      8
#define LIFE_H      6
#define LIFE_ADV    10

#define BR_ROWS     5
#define BR_COLS     10
#define BR_X0       10       /* brick field: x0 + col*30, y0 + row*12        */
#define BR_Y0       24
#define BR_BW       28
#define BR_BH       10
#define BR_PITCH_X  30
#define BR_PITCH_Y  12

#define PADDLE_Y    224
#define PADDLE_W    48
#define PADDLE_H    8
#define PADDLE_SPEED 2

#define BALL_S      6
#define BALL_OFF_X  21       /* ball sits paddle_x + 21 while serving        */

/* --- Palette (RGB565, flat fills) ---------------------------------------- */

#define COL_BG      0x0000u  /* black                                        */
#define COL_BALL    0xFFE0u  /* yellow                                       */
#define COL_PADDLE  0xFFFFu  /* white                                        */
#define COL_HUD     0x07E0u  /* green: score digits                          */
#define COL_LIFE    0x07FFu  /* aqua: remaining lives                        */

static const unsigned short rowcol[BR_ROWS] = {
    0xF800u, 0xFD20u, 0xFFE0u, 0x07E0u, 0x001Fu   /* red .. blue, top first  */
};

/* Points per brick row (top row worth the most). */
static const unsigned char pts[BR_ROWS] = { 50, 40, 30, 20, 10 };

/* --- Game states --------------------------------------------------------- */

#define ST_SERVE 0
#define ST_PLAY  1
#define ST_DEAD  2
#define ST_OVER  3
#define ST_WIN   4

#define DEAD_PAUSE 40        /* timer ticks paused after losing a life      */
#define BE_TICKS   6         /* timer ticks a beep lasts                    */

/* Tone events (the test reads AUDIO_FREQ after each). */
#define BE_BRICK   880u
#define BE_PADDLE  660u
#define BE_DEATH   220u
#define BE_OVER    110u
#define BE_START   440u

/* --- Guest state --------------------------------------------------------- *
 * score/lives/state/paddle_x are globals the test reads out of RAM. */

int score;
int lives;
int state;
int paddle_x;

static int ball_x, ball_y;
static int vx, vy;
static unsigned short bricks[BR_ROWS];   /* bit c set = brick col c alive    */
static unsigned timer_now_v;             /* last read TIMER_COUNT            */
static unsigned dead_until;              /* timer count when the pause ends  */
static unsigned beep_until;              /* timer count when the beep ends   */
static int      beep_on;

/* --- MMIO helpers -------------------------------------------------------- */

static unsigned mmio_read(unsigned addr)
{
    return *(volatile unsigned *)addr;
}

static void mmio_write(unsigned addr, unsigned v)
{
    *(volatile unsigned *)addr = v;
}

static unsigned timer_now(void)
{
    timer_now_v = mmio_read(REG_TIMER_COUNT);
    return timer_now_v;
}

static unsigned input_pad(void)
{
    return mmio_read(REG_INPUT_PAD);
}

/* Play a short beep of the given frequency (ends BE_TICKS ticks later). */
static void event_beep(unsigned hz)
{
    mmio_write(REG_AUDIO_FREQ, hz);
    mmio_write(REG_AUDIO_CTRL, 1u);
    beep_until = timer_now() + BE_TICKS;
    beep_on = 1;
}

/* --- Brick field --------------------------------------------------------- */

static void reset_bricks(void)
{
    int r;
    for (r = 0; r < BR_ROWS; r++)
        bricks[r] = (unsigned short)((1u << BR_COLS) - 1u);
}

static int all_clear(void)
{
    int r;
    for (r = 0; r < BR_ROWS; r++)
        if (bricks[r] != 0)
            return 0;
    return 1;
}

/* --- Ball ---------------------------------------------------------------- */

static int ball_hits(int rx, int ry, int rw, int rh)
{
    return ball_x < rx + rw && ball_x + BALL_S > rx &&
           ball_y < ry + rh && ball_y + BALL_S > ry;
}

static void step_ball(void)
{
    int r, c;

    ball_x += vx;
    ball_y += vy;

    /* Screen walls. */
    if (ball_x <= 0)               { ball_x = 0;               vx =  1; }
    if (ball_x >= W - BALL_S)      { ball_x = W - BALL_S;      vx = -1; }
    if (ball_y <= 0)               { ball_y = 0;               vy =  2; }

    /* Bricks: first overlapping alive brick is cleared and reflects. */
    for (r = 0; r < BR_ROWS; r++) {
        for (c = 0; c < BR_COLS; c++) {
            if (!(bricks[r] & (1u << c)))
                continue;
            if (ball_hits(BR_X0 + c * BR_PITCH_X, BR_Y0 + r * BR_PITCH_Y,
                          BR_BW, BR_BH)) {
                bricks[r] &= (unsigned short)~(1u << c);
                score += pts[r];
                vy = -vy;
                event_beep(BE_BRICK);
                if (all_clear()) {
                    state = ST_WIN;
                    event_beep(BE_OVER);
                }
                return;
            }
        }
    }

    /* Paddle: reflect upward; the hit offset picks the new angle. */
    if (vy > 0 && ball_hits(paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H)) {
        int centre = ball_x + BALL_S / 2;
        ball_y = PADDLE_Y - BALL_S;
        vy = -2;
        if      (centre < paddle_x + PADDLE_W / 4)     vx = -2;
        else if (centre < paddle_x + PADDLE_W / 2)     vx = -1;
        else if (centre < paddle_x + PADDLE_W * 3 / 4) vx =  1;
        else                                           vx =  2;
        event_beep(BE_PADDLE);
    }

    /* Past the bottom: lose a life. */
    if (ball_y >= H) {
        lives--;
        event_beep(BE_DEATH);
        if (lives <= 0) {
            lives = 0;
            state = ST_OVER;
            event_beep(BE_OVER);
        } else {
            state = ST_DEAD;
            dead_until = timer_now() + DEAD_PAUSE;
        }
    }
}

/* --- Frame update -------------------------------------------------------- */

static void serve_ball(void)
{
    ball_x = paddle_x + BALL_OFF_X;
    ball_y = PADDLE_Y - BALL_S;
}

static void restart(void)
{
    score = 0;
    lives = 3;
    paddle_x = (W - PADDLE_W) / 2;
    reset_bricks();
    state = ST_SERVE;
    event_beep(BE_START);
}

static void update(void)
{
    unsigned pad = input_pad();

    /* Expire the event beep using the frame timer as a tick source. */
    if (beep_on && timer_now() >= beep_until) {
        mmio_write(REG_AUDIO_CTRL, 0u);
        beep_on = 0;
    }

    /* Paddle steering (frozen on game over / win). */
    if (state != ST_OVER && state != ST_WIN) {
        if (pad & PAD_LEFT)  paddle_x -= PADDLE_SPEED;
        if (pad & PAD_RIGHT) paddle_x += PADDLE_SPEED;
        if (paddle_x < 0)               paddle_x = 0;
        if (paddle_x > W - PADDLE_W)    paddle_x = W - PADDLE_W;
    }

    switch (state) {
    case ST_SERVE:
        serve_ball();
        if (pad & PAD_A) {
            vx = 1;      /* fixed launch: straight up and slightly right */
            vy = -2;
            state = ST_PLAY;
        }
        break;

    case ST_PLAY:
        step_ball();
        break;

    case ST_DEAD:
        serve_ball();
        if (timer_now() >= dead_until)
            state = ST_SERVE;
        break;

    case ST_OVER:
    case ST_WIN:
        if (pad & PAD_START)
            restart();
        break;
    }
}

/* --- Drawing ------------------------------------------------------------- */

/* 3x5 font at scale 2 (6x10 pixels), one byte per row, bit2 = left pixel. */
static const unsigned char font[10][5] = {
    { 7, 5, 5, 5, 7 },   /* 0 */
    { 2, 1, 1, 1, 7 },   /* 1 */
    { 7, 1, 7, 4, 7 },   /* 2 */
    { 7, 1, 7, 1, 7 },   /* 3 */
    { 5, 5, 7, 1, 1 },   /* 4 */
    { 7, 4, 7, 1, 7 },   /* 5 */
    { 7, 4, 7, 5, 7 },   /* 6 */
    { 7, 1, 1, 1, 1 },   /* 7 */
    { 7, 5, 7, 5, 7 },   /* 8 */
    { 7, 5, 7, 1, 7 },   /* 9 */
};

static void draw_digit(int x, int y, int d, unsigned col)
{
    int r, c;
    for (r = 0; r < 5; r++) {
        unsigned bits = font[d][r];
        for (c = 0; c < 3; c++)
            if (bits & (unsigned)(4 >> c))
                gfx_rect(x + c * 2, y + r * 2, 2, 2, col);
    }
}

/* Fixed-width decimal by subtraction (the guest has no divide). */
static const short places[SCORE_DIGITS] = { 1000, 100, 10, 1 };

static void draw_score(int value, unsigned col)
{
    int i, d;
    int v = value;
    if (v > 9999)
        v = 9999;
    for (i = 0; i < SCORE_DIGITS; i++) {
        int place = places[i];
        d = 0;
        while (v >= place) {
            v -= place;
            d++;
        }
        draw_digit(SCORE_X + i * DIGIT_ADV, SCORE_Y, d, col);
    }
}

static void draw_lives(void)
{
    int i;
    for (i = 0; i < lives; i++)
        gfx_rect(LIFE_X - i * LIFE_ADV, LIFE_Y, LIFE_W, LIFE_H, COL_LIFE);
}

static void draw_bricks(void)
{
    int r, c;
    for (r = 0; r < BR_ROWS; r++) {
        unsigned row = bricks[r];
        if (row == 0)
            continue;
        for (c = 0; c < BR_COLS; c++)
            if (row & (1u << c))
                gfx_rect(BR_X0 + c * BR_PITCH_X, BR_Y0 + r * BR_PITCH_Y,
                         BR_BW, BR_BH, rowcol[r]);
    }
}

static void draw(void)
{
    gfx_clear(COL_BG);
    draw_bricks();
    draw_lives();
    draw_score(score, COL_HUD);
    gfx_rect(paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H, COL_PADDLE);
    if (state != ST_OVER && state != ST_WIN)
        gfx_rect(ball_x, ball_y, BALL_S, BALL_S, COL_BALL);
}

/* --- Entry point --------------------------------------------------------- */

int main(void)
{
    /* Boot state: silent (the device powers on at the demo tone; the game
     * only beeps on events), serve position, full brick field. */
    mmio_write(REG_AUDIO_CTRL, 0u);
    beep_on = 0;
    score = 0;
    lives = 3;
    paddle_x = (W - PADDLE_W) / 2;
    reset_bricks();
    state = ST_SERVE;

    for (;;) {
        update();
        draw();
        gfx_present();
    }
}
