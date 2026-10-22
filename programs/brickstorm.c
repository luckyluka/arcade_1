/* BRICKSTORM — a polished breakout for ARCADE-1.
 *
 * The same game as the milestone-15 Breakout, done again from nothing:
 * the machine's native 480x270 instead of the old 320x240, sprite art
 * instead of flat rectangles, and a modern arcade's worth of feel on
 * top of the classic bones — a paddle that returns the ball at an angle
 * that depends on where you hit it, bricks that crack before they break,
 * bombs that take their neighbours with them, six power-ups, twelve
 * brick layouts and a boss.
 *
 * Freestanding C for RV32IM — no libc, deterministic (the levels are a
 * table, the randomness is a seeded LCG, the sound is a register).
 *
 * Observable globals (read with --vars):
 *   state, score, hi_score, lives, level, bricks_left, ball_x, ball_y,
 *   paddle_x, paddle_w, power, boss_hp, boss_state, balls, ball_vx,
 *   ball_vy, ball_id, machine_frame
 *
 *   arcade1 run brickstorm.arc1 --vars state,score,lives,level
 *
 * The contact recorder's globals (a paddle contact, published for the
 * tools): hit_frame, hit_off, hit_side, hit_vx, hit_vx_in, hit_count.
 * `hit_frame` is the MACHINE's frame (the timer device), not the game's
 * own counter, which restarts at every level.
 *
 * States: 0 = the title, 1 = the serve (the ball waits on the paddle),
 * 2 = playing, 3 = the death pause, 4 = the level clear, 5 = the game
 * over, 6 = the win.
 *
 * Controls: left/right steer, A launches (and releases a caught ball,
 * and fires the laser), START starts and restarts.
 */
#include "asset.h"
#include "brickstorm_assets.h"
#include "text.h"

#define SCREEN_W  480
#define SCREEN_H  270
#define HUD_H     20          /* the HUD strip across the top */

/* --- the brick field ------------------------------------------------------ */
#define BRICK_COLS   10
#define BRICK_ROWS   12
#define BRICK_X0     20
#define BRICK_Y0     32
#define BRICK_PITCH_X 44
#define BRICK_PITCH_Y 14
#define BRICK_W      40
#define BRICK_H      11

/* --- the paddle and the ball --------------------------------------------- */
/* The ball's position and velocity are in 1/16 px, so it moves smoothly
 * and its speed can be tuned to a fraction of a pixel. */
#define FP           16
#define PADDLE_Y     244
#define PADDLE_H     10
#define PADDLE_W_MID 56
#define PADDLE_SPEED 7
#define BALL_S       8
/* The ball's speed is a property of the STAGE, not of the rally: it grows
 * a little with every level and never creeps up while the player is
 * returning it. (It used to climb on every paddle hit, and the ball ended
 * up both fast and flat — the rally turned into noise.) */
#define SPEED_BASE   42       /* 2.6 px per frame at stage 1 */
#define SPEED_STEP   2        /* +0.125 px per stage */
#define SPEED_MAX    58       /* 3.6 px per frame: the fastest it gets */
/* The shallowest legal angle is the table's last step: |vx| = 223,
 * |vy| = 128 (in 256ths) — 30 degrees above the horizontal. No bounce
 * may leave the ball shallower than that.
 *
 * (This was WRONG at first, and wrong in a way that cost the game its
 * whole mechanic: the limit forbade |vx| > 0.87 * |vy| — 49 degrees —
 * which is STEEPER than the shallowest legal angle, so it re-aimed
 * every single launch and the catch point decided nothing but the side.
 * The contact recorder showed it: every contact left with the same
 * |vx|.) */
#define FLAT_LIMIT   223      /* |vx| may not exceed 1.74 * |vy| */

#define SPARK_MAX 64

/* --- the power-ups -------------------------------------------------------- */
enum {
    P_EXPAND, P_MULTI, P_SLOW, P_FAST, P_LASER, P_CATCH, P_KINDS
};

/* --- the brick kinds ------------------------------------------------------ */
enum { B_NORMAL, B_TOUGH, B_STEEL, B_BOMB };

/* --- the states ----------------------------------------------------------- */
enum {
    ST_TITLE, ST_SERVE, ST_PLAY, ST_DEAD, ST_CLEAR, ST_OVER, ST_WIN
};

/* ------------------------------------------------------------------------ */
/* the observable state                                                     */
/* ------------------------------------------------------------------------ */

int state = 0;
int score = 0;
int hi_score = 0;
int lives = 3;
int level = 1;
int bricks_left = 0;
int ball_x = 240;
int ball_y = 200;
int paddle_x = 240;
int power = 0;            /* the power-up in force, or 0 */
int power_timer = 0;      /* its frames left */
int boss_hp = 0;
int boss_state = 0;       /* 0 none, 1 entering, 2 fighting, 3 dying */
int balls = 0;            /* the balls in play */
int ball_vx = 0;          /* the first ball's velocity, 1/16 px a frame */
int ball_vy = 0;

/* The last contact with the paddle, published so the tools can RECORD
 * it: which frame, where the ball landed, which side of the centre that
 * was, and which way the ball left. `arcade1 run --vars ... --var-every 1`
 * turns these into a per-frame trace; tools/record_hits.py turns the
 * trace into a contact log. */
/* volatile: the game only ever WRITES these, and at -Os a global
 * nothing reads is deleted — the tools read them, not the game. */
volatile int hit_frame = 0;   /* the frame of the last contact */
volatile int hit_off = 0;     /* where it landed, 1/16 px from the centre */
volatile int hit_side = 0;    /* -1 left of centre, 0 dead centre, +1 right */
volatile int hit_vx = 0;      /* the horizontal velocity it left with */
volatile int hit_vx_in = 0;   /* the horizontal velocity it arrived with */
volatile int hit_count = 0;   /* the contacts so far */

/* Which ball the observables (ball_x, ball_y, ball_vx, ball_vy) describe.
 * With the MULTI power-up there can be several balls in flight and the
 * tools need to know when the one they are watching has been replaced. */
volatile int ball_id = -1;

/* The MACHINE's frame counter — the timer device, not the game's own
 * `frames`, which restarts at every level load. The contact recorder
 * has to name the frame the contact happened on the way the tools count
 * frames (the trace's own numbering), or every contact is off by the
 * frames that ran before the level began. */
volatile int machine_frame = 0;

/* ------------------------------------------------------------------------ */
/* the entities                                                             */
/* ------------------------------------------------------------------------ */

typedef struct {
    int kind;              /* the brick kind */
    int hp;
    int flash;             /* the frames of the hit-flash left */
    int alive;
} Brick;

typedef struct {
    int x, y;              /* 1/16 px */
    int vx, vy;            /* 1/16 px per frame */
    int alive;
    int stuck;             /* the catch power-up holds it on the paddle */
} Ball;

typedef struct { int x, y, kind, alive; } Drop;
typedef struct { int x, y, vx, vy, life, alive; } Spark;
typedef struct { int x, y, vy, alive; } Shot;

static Brick bricks[BRICK_ROWS * BRICK_COLS];
static Ball  ball_list[8];
static Drop  drops[8];
static Spark sparks[64];
static Shot  paddle_shots[8];     /* the paddle's laser */
static Shot  boss_shots[8];        /* the boss's guns */

/* the run's private state */
static int paddle_w = PADDLE_W_MID;
static int ball_speed = SPEED_BASE;
static int paddle_vx;          /* the paddle's speed this frame (english) */
static int paddle_squash;      /* the frames of the paddle's squash left */
static int drop_timer;
static int laser_timer;        /* the frames of the laser left */
static int speed_timer;        /* the frames of the SLOW/FAST left */
static int catch_timer;        /* the frames of the catch left */
static int laser_cool;         /* the frames until the next laser shot */
static int dead_timer;
static int clear_timer;
static int shake;
static int boss_x, boss_y;
static int boss_cleared;      /* the boss is down: the stage is over */
static int hits;               /* the paddle contacts so far */
static int boss_timer;
static int title_t;
static int title_level = 1;     /* the level the title screen starts at */
static unsigned frames;
static unsigned prev_pad;

/* A press is an edge, not a hold: the title's level picker steps one
 * stage per press, so holding the button must not run to the last. */
static int pressed(unsigned pad, unsigned bit)
{
    return (pad & bit) != 0u && (prev_pad & bit) == 0u;
}

/* the ball's trail: the last positions, for the streak behind it */
static int trail_x[6], trail_y[6], trail_n;

/* ------------------------------------------------------------------------ */
/* the audio: one tone at a time, with a pitch sweep                        */
/* ------------------------------------------------------------------------ */

#define AUDIO_FREQ (*(volatile unsigned int *)(ARCADE1_MMIO_BASE + 0x300u))
#define AUDIO_CTRL (*(volatile unsigned int *)(ARCADE1_MMIO_BASE + 0x304u))
static int snd_timer, snd_len, snd_f0, snd_f1;

static void sfx(int f0, int f1, int len)
{
    snd_f0 = f0;
    snd_f1 = f1;
    snd_len = len;
    snd_timer = len;
}

static void sfx_update(void)
{
    int f, t;
    if (snd_timer <= 0)
        return;
    snd_timer--;
    if (snd_timer <= 0) {
        __asm__ volatile ("" ::: "memory");
        AUDIO_CTRL = 0;
        return;
    }
    t = snd_len - snd_timer;
    f = snd_f0 + ((snd_f1 - snd_f0) * t) / snd_len;
    if (f < 30)
        f = 30;
    __asm__ volatile ("" ::: "memory");
    AUDIO_FREQ = (unsigned int)f;
    AUDIO_CTRL = 1;
}

/* The ball's speed for a stage: a little faster every level, capped. */
static int speed_for_level(int n)
{
    int v = SPEED_BASE + (n - 1) * SPEED_STEP;
    if (v > SPEED_MAX)
        v = SPEED_MAX;
    return v;
}

/* Keep the ball out of the flat band: no bounce may leave it shallower
 * than 60 degrees from the vertical.
 *
 * The ball is put back ON the 60 degree line at the same speed, rather
 * than having its horizontal part trimmed: trimming it made the ball
 * crawl along the ceiling instead of flying (a flat ball whose speed
 * keeps shrinking is a slower flat ball, and it never gets out). */
static void limit_flat(Ball *b)
{
    int ax = b->vx < 0 ? -b->vx : b->vx;
    int ay = b->vy < 0 ? -b->vy : b->vy;
    /* the shallowest legal line: |vx| / |vy| = 223 / 128 */
    if (ay == 0 || ax * 128 <= ay * FLAT_LIMIT)
        return;
    /* too flat: put it back on that line, at the same speed */
    b->vx = (b->vx < 0 ? -1 : 1) * ((ball_speed * FLAT_LIMIT) >> 8);
    b->vy = (b->vy < 0 ? -1 : 1) * ((ball_speed * 128) >> 8);
}

/* ------------------------------------------------------------------------ */
/* the levels: twelve layouts and the boss                                  */
/* ------------------------------------------------------------------------ */

/* The layout characters:
 *   '.'  empty
 *   'a'..'e'  a normal brick in that row's colour
 *   'T'  a tough brick (two hits — it cracks first)
 *   'S'  a steel brick (the ball bounces off it)
 *   'X'  a bomb brick (it takes its neighbours with it)
 */
static const char *const levels[] = {
    /* 1 — FIRST LIGHT: five solid rows, the classic opening */
    "aaaaaaaaaa"
    "aaaaaaaaaa"
    "aaaaaaaaaa"
    "aaaaaaaaaa"
    "aaaaaaaaaa"
    ".........."
    ".........."
    ".........."
    ".........."
    ".........."
    ".........."
    "..........",
    /* 2 — PYRAMID */
    "....aa...."
    "...aaaa..."
    "..aaaaaa.."
    ".aaaaaaaa."
    "aaaaaaaaaa"
    "..aaaaaa.."
    "...aaaa..."
    "....aa...."
    ".........."
    ".........."
    ".........."
    "..........",
    /* 3 — CHECKER */
    "ababababab"
    "bababababa"
    "ababababab"
    "bababababa"
    "ababababab"
    ".........."
    ".........."
    ".........."
    ".........."
    ".........."
    ".........."
    "..........",
    /* 4 — FORTRESS: steel pillars between the bricks */
    "aSaaaSaaaS"
    "aSaaaSaaaS"
    "aSaaaSaaaS"
    "aaaaaaaaaa"
    "aSaaaSaaaS"
    "aSaaaSaaaS"
    ".........."
    ".........."
    ".........."
    ".........."
    ".........."
    "..........",
    /* 5 — DIAMOND */
    "....aa...."
    "...aaba..."
    "..aabbba.."
    ".abbddbba."
    "aabbddbbaa"
    ".abbddbba."
    "..aabbba.."
    "...aaba..."
    "....aa...."
    ".........."
    ".........."
    "..........",
    /* 6 — HIVE: a tough frame around a soft core */
    "TTTTTTTTTT"
    "T........T"
    "T.TTTTTT.T"
    "T.TbbbbT.T"
    "T.Tb..bT.T"
    "T.Tb..bT.T"
    "T.TbbbbT.T"
    "T.TTTTTT.T"
    "T........T"
    "TTTTTTTTTT"
    ".........."
    "..........",
    /* 7 — COLUMNS */
    "a.c.c.c.c."
    "a.c.c.c.c."
    "a.c.c.c.c."
    "a.c.c.c.c."
    "a.c.c.c.c."
    "a.c.c.c.c."
    "a.c.c.c.c."
    "a.c.c.c.c."
    ".........."
    ".........."
    ".........."
    "..........",
    /* 8 — STAIRCASE */
    "........a."
    ".......aa."
    "......aaa."
    ".....aaaa."
    "....aaaaa."
    "...aaaaaa."
    "..aaaaaaa."
    ".aaaaaaaa."
    ".........."
    ".........."
    ".........."
    "..........",
    /* 9 — GAUNTLET: bombs among steel */
    "XaSaSaSaSX"
    "aSaSaSaSaS"
    "XaSaSaSaSX"
    "aSaSaSaSaS"
    "XaSaSaSaSX"
    "aSaSaSaSaS"
    ".........."
    ".........."
    ".........."
    ".........."
    ".........."
    "..........",
    /* 10 — CROSS */
    "....aaaa.."
    "....aaaa.."
    "....aaaa.."
    "....aaaa.."
    "aaaaaaaaaa"
    "....aaaa.."
    "....aaaa.."
    "....aaaa.."
    "....aaaa.."
    ".........."
    ".........."
    "..........",
    /* 11 — LATTICE */
    "a.a.a.a.a."
    ".a.a.a.a.a"
    "a.a.a.a.a."
    ".a.a.a.a.a"
    "a.a.a.a.a."
    ".a.a.a.a.a"
    "a.a.a.a.a."
    ".........."
    ".........."
    ".........."
    ".........."
    "..........",
    /* 12 — CROWN: a steel and tough head over soft rows */
    "TTTTTTTTTT"
    "SSSSSSSSSS"
    "bbbbbbbbbb"
    "cccccccccc"
    "dddddddddd"
    "eeeeeeeeee"
    ".........."
    ".........."
    ".........."
    ".........."
    ".........."
    "..........",
};

#define LEVEL_COUNT ((int)(sizeof(levels) / sizeof(levels[0])))
#define BOSS_LEVEL (LEVEL_COUNT + 1)     /* the thirteenth stage */

/* The stages' names, shown on the title screen when the level is picked. */
static const char *const level_names[] = {
    "FIRST LIGHT", "PYRAMID", "CHECKER", "FORTRESS", "DIAMOND", "HIVE",
    "COLUMNS", "STAIRCASE", "GAUNTLET", "CROSS", "LATTICE", "CROWN",
    "THE CORE"
};

/* ------------------------------------------------------------------------ */
/* helpers                                                                  */
/* ------------------------------------------------------------------------ */

static int overlap(int ax, int ay, int aw, int ah,
                   int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static unsigned lerp565(unsigned a, unsigned b, int num, int den)
{
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    int r = ar + (br - ar) * num / den;
    int g = ag + (bg - ag) * num / den;
    int bl = ab + (bb - ab) * num / den;
    return (unsigned)((r << 11) | (g << 5) | bl);
}

static void add_score(int n)
{
    score += n;
    if (score > hi_score)
        hi_score = score;
    if (score >= 50000 && lives < 5) {
        lives++;
        sfx(880, 1320, 20);
    }
}

static void spawn_sparks(int x, int y, int n, int spread)
{
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < SPARK_MAX; j++) {
            if (!sparks[j].alive) {
                sparks[j].x = x;
                sparks[j].y = y;
                sparks[j].vx = arcade1_rand(spread) - spread / 2;
                sparks[j].vy = -(arcade1_rand(spread) + 1);
                sparks[j].life = 10 + arcade1_rand(12);
                sparks[j].alive = 1;
                break;
            }
        }
    }
}

static void spawn_drop(int x, int y, int kind)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (!drops[i].alive) {
            drops[i].x = x;
            drops[i].y = y;
            drops[i].kind = kind;
            drops[i].alive = 1;
            return;
        }
    }
}

/* The return angles: the paddle's edge returns the ball at a shallow
 * angle and its centre at a steep one — thirteen steps from -60 to +60
 * degrees, as unit vectors in 256ths (so the multiplication into the
 * ball's 1/16 px velocity does not throw the angle away). */
static const short ang_x[7] = { 0, 44, 87, 128, 164, 197, 223 };
static const short ang_y[7] = { 256, 251, 241, 223, 197, 164, 128 };

/* ------------------------------------------------------------------------ */
/* the levels                                                               */
/* ------------------------------------------------------------------------ */

static void load_level(int n)
{
    int r, c;
    bricks_left = 0;
    ball_speed = speed_for_level(n);
    for (r = 0; r < BRICK_ROWS; r++) {
        for (c = 0; c < BRICK_COLS; c++) {
            Brick *b = &bricks[r * BRICK_COLS + c];
            char ch = '.';
            if (n >= 1 && n <= LEVEL_COUNT)
                ch = levels[n - 1][r * BRICK_COLS + c];
            b->alive = 0;
            b->hp = 0;
            b->flash = 0;
            b->kind = B_NORMAL;
            switch (ch) {
            case 'a': case 'b': case 'c': case 'd': case 'e':
                b->alive = 1;
                b->hp = 1;
                b->kind = B_NORMAL;
                bricks_left++;
                break;
            case 'T':
                b->alive = 1;
                b->hp = 2;
                b->kind = B_TOUGH;
                bricks_left++;
                break;
            case 'S':
                b->alive = 1;
                b->hp = 1;
                b->kind = B_STEEL;
                break;          /* steel does not count towards the clear */
            case 'X':
                b->alive = 1;
                b->hp = 1;
                b->kind = B_BOMB;
                bricks_left++;
                break;
            default:
                break;
            }
        }
    }
}

static void serve_ball(void)
{
    int i;
    for (i = 0; i < 8; i++)
        ball_list[i].alive = 0;
    ball_list[0].x = paddle_x * FP;
    ball_list[0].y = (PADDLE_Y - BALL_S) * FP;
    ball_list[0].vx = 0;
    ball_list[0].vy = 0;
    ball_list[0].stuck = 1;
    ball_list[0].alive = 1;
    balls = 1;
    trail_n = 0;
}

/* ------------------------------------------------------------------------ */
/* the power-ups                                                            */
/* ------------------------------------------------------------------------ */

static void apply_power(int kind)
{
    int i;
    switch (kind) {
    case P_EXPAND:
        if (paddle_w < 72)
            paddle_w += 16;
        break;
    case P_MULTI:
        for (i = 1; i < 3; i++) {
            if (!ball_list[i].alive) {
                ball_list[i] = ball_list[0];
                ball_list[i].x = ball_list[0].x + i * 12 * FP;
                ball_list[i].vx = ball_list[0].vx + (i - 1) * 2 * FP;
                ball_list[i].vy = ball_list[0].vy;
                ball_list[i].stuck = 0;
                ball_list[i].alive = 1;
                balls++;
            }
        }
        break;
    case P_SLOW:
        if (ball_speed > SPEED_BASE)
            ball_speed -= 8;
        speed_timer = 600;
        break;
    case P_FAST:
        if (ball_speed < SPEED_MAX)
            ball_speed += 8;
        speed_timer = 600;
        break;
    case P_LASER:
        laser_timer = 900;
        power = P_LASER;
        power_timer = 900;
        break;
    case P_CATCH:
        catch_timer = 600;
        power = P_CATCH;
        power_timer = 600;
        for (i = 0; i < 8; i++)
            if (ball_list[i].alive)
                ball_list[i].stuck = 1;
        break;
    default:
        break;
    }
    add_score(100);
    sfx(700, 1500, 18);
}

/* ------------------------------------------------------------------------ */
/* the boss                                                                 */
/* ------------------------------------------------------------------------ */

static void start_boss(void)
{
    ball_speed = speed_for_level(BOSS_LEVEL);
    boss_x = (SCREEN_W - BOSS_W) / 2;
    boss_y = -BOSS_H;
    boss_state = 1;
    boss_hp = 12;
    boss_timer = 0;
    sfx(160, 420, 50);
}

static void boss_fire(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (!boss_shots[i].alive) {
            boss_shots[i].x = boss_x + BOSS_W / 2 + (arcade1_rand(120) - 60);
            boss_shots[i].y = boss_y + BOSS_H - 10;
            boss_shots[i].vy = 2;
            boss_shots[i].alive = 1;
            return;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* the reset                                                                */
/* ------------------------------------------------------------------------ */

static void clear_run(void)
{
    int i;
    for (i = 0; i < BRICK_ROWS * BRICK_COLS; i++)
        bricks[i].alive = 0;
    for (i = 0; i < 8; i++) {
        ball_list[i].alive = 0;
        drops[i].alive = 0;
        paddle_shots[i].alive = 0;
        boss_shots[i].alive = 0;
    }
    for (i = 0; i < SPARK_MAX; i++)
        sparks[i].alive = 0;
    balls = 0;
    bricks_left = 0;
}

static void reset_game(void)
{
    clear_run();
    score = 0;
    lives = 3;
    level = 1;
    paddle_w = PADDLE_W_MID;
    paddle_x = SCREEN_W / 2;
    ball_speed = speed_for_level(1);
    paddle_vx = 0;
    paddle_squash = 0;
    power = 0;
    power_timer = 0;
    laser_timer = 0;
    catch_timer = 0;
    speed_timer = 0;
    shake = 0;
    boss_state = 0;
    boss_cleared = 0;
    boss_hp = 0;
    frames = 0;
    load_level(level);
    serve_ball();
    state = ST_SERVE;
}

/* ------------------------------------------------------------------------ */
/* the bricks                                                               */
/* ------------------------------------------------------------------------ */

/* Break the brick at (row, col) and whatever it takes with it. */
static void break_brick(int row, int col, int chain)
{
    Brick *b;
    int r, c;
    if (row < 0 || row >= BRICK_ROWS || col < 0 || col >= BRICK_COLS)
        return;
    b = &bricks[row * BRICK_COLS + col];
    if (!b->alive)
        return;
    if (b->kind == B_STEEL)
        return;
    b->alive = 0;
    bricks_left--;
    add_score((BRICK_ROWS - row) * 10);
    spawn_sparks(BRICK_X0 + col * BRICK_PITCH_X + BRICK_W / 2,
                 BRICK_Y0 + row * BRICK_PITCH_Y + BRICK_H / 2, 8, 7);
    sfx(420 + row * 60, 300 + row * 40, 6);
    if (b->kind == B_BOMB && chain < 3) {
        /* the bomb takes its neighbours with it */
        for (r = -1; r <= 1; r++)
            for (c = -1; c <= 1; c++)
                if (r || c)
                    break_brick(row + r, col + c, chain + 1);
        shake = 8;
        sfx(300, 60, 24);
    }
    /* the drop: a power-up every so often, not every fifth brick */
    drop_timer--;
    if (drop_timer <= 0 && bricks_left > 0) {
        drop_timer = 14;
        spawn_drop(BRICK_X0 + col * BRICK_PITCH_X + BRICK_W / 2,
                   BRICK_Y0 + row * BRICK_PITCH_Y, arcade1_rand(P_KINDS));
    }
}

/* ------------------------------------------------------------------------ */
/* the balls                                                                */
/* ------------------------------------------------------------------------ */

/* The ball against the bricks: the overlap decides which side it came in
 * on, so the ball never sticks inside a brick. */
static void ball_vs_bricks(Ball *b)
{
    int px = b->x / FP;
    int py = b->y / FP;
    int r, c;
    for (r = 0; r < BRICK_ROWS; r++) {
        for (c = 0; c < BRICK_COLS; c++) {
            Brick *br = &bricks[r * BRICK_COLS + c];
            int bx = BRICK_X0 + c * BRICK_PITCH_X;
            int by = BRICK_Y0 + r * BRICK_PITCH_Y;
            int over_x, over_y;
            if (!br->alive)
                continue;
            if (!overlap(px, py, BALL_S, BALL_S, bx, by, BRICK_W, BRICK_H))
                continue;

            over_x = (px + BALL_S < bx + BRICK_W) ? (bx + BRICK_W) - (px + BALL_S)
                                                   : px - bx;
            over_y = (py + BALL_S < by + BRICK_H) ? (by + BRICK_H) - (py + BALL_S)
                                                   : py - by;
            if (over_y <= over_x) {
                b->vy = -b->vy;
                if (b->vy > 0)
                    b->y = (by - BALL_S) * FP;
                else
                    b->y = (by + BRICK_H) * FP;
            } else {
                b->vx = -b->vx;
                if (b->vx > 0)
                    b->x = (bx - BALL_S) * FP;
                else
                    b->x = (bx + BRICK_W) * FP;
            }
            limit_flat(b);

            if (br->kind == B_STEEL) {
                sfx(240, 200, 8);
                shake = 3;
                return;
            }
            br->hp--;
            br->flash = 3;
            shake = 2;
            if (br->hp <= 0) {
                break_brick(r, c, 0);
                if (bricks_left <= 0 && boss_state == 0) {
                    clear_timer = 100;
                    state = ST_CLEAR;
                }
            } else {
                spawn_sparks(px + BALL_S / 2, py + BALL_S / 2, 3, 5);
                sfx(600, 400, 4);
            }
            return;
        }
    }
}

/* The ball against the boss: the hull's underside is the weak strip and
 * the three cores in it are worth double — a fight, not a lottery. */
static void ball_vs_boss(Ball *b)
{
    int px = b->x / FP;
    int py = b->y / FP;
    int j, hit = 0;
    if (boss_state != 2)
        return;
    /* the cores: double damage */
    for (j = 0; j < 3; j++) {
        int cx = boss_x + 46 + j * 54;
        int cy = boss_y + 56;
        if (overlap(px, py, BALL_S, BALL_S, cx - 10, cy - 10, 20, 20)) {
            hit = 2;
            spawn_sparks(cx, cy, 6, 9);
            break;
        }
    }
    /* the weak strip along the hull's underside */
    if (!hit && overlap(px, py, BALL_S, BALL_S, boss_x + 6,
                        boss_y + BOSS_H - 22, BOSS_W - 12, 22)) {
        hit = 1;
        spawn_sparks(px + BALL_S / 2, py, 4, 7);
    }
    if (hit) {
        boss_hp -= hit;
        sfx(900, 1500, 6);
        shake = 4;
        if (boss_hp <= 0) {
            boss_state = 3;
            boss_timer = 0;
            sfx(400, 50, 60);
            shake = 20;
        }
        return;
    }
    if (overlap(px, py, BALL_S, BALL_S, boss_x, boss_y, BOSS_W, BOSS_H)) {
        if (b->vy < 0) {
            b->vy = -b->vy;
            b->y = (boss_y + BOSS_H) * FP;
        } else {
            b->vy = -b->vy;
            b->y = (boss_y - BALL_S) * FP;
        }
        limit_flat(b);
        sfx(300, 240, 6);
    }
}

/* The ball against the paddle: where it lands decides the angle.
 *
 * The contact is SWEPT, not sampled: the ball moves three or four pixels
 * a frame, so the position at the moment the overlap was noticed can be
 * a whole ball-width past the paddle — and the launch angle with it.
 * Here the exact point where the ball's lower edge crossed the paddle's
 * top edge is worked out instead, so the angle is the one the player
 * aimed at and not the one the frame rate happened to produce.
 *
 * The paddle's own motion adds a little english: sweeping it as the ball
 * arrives bends the return. */
static void ball_vs_paddle(Ball *b)
{
    int px = b->x / FP;
    int py = b->y / FP;
    int prev_top, now_top, frac, cross_x, off, step, pos_step, k;
    int in_vx;
    if (b->vy <= 0)
        return;
    if (py + BALL_S < PADDLE_Y || py > PADDLE_Y + PADDLE_H)
        return;
    if (px + BALL_S <= paddle_x - paddle_w / 2 ||
        px >= paddle_x + paddle_w / 2)
        return;
    in_vx = b->vx;                 /* the way it was already travelling */

    /* where the ball's lower edge crossed the paddle's top edge */
    prev_top = b->y - b->vy + BALL_S * FP;
    now_top = b->y + BALL_S * FP;
    if (now_top <= prev_top) {
        /* the ball is already past the plane (it tunnelled): use where it
         * is now */
        cross_x = b->x;
    } else {
        frac = ((PADDLE_Y * FP - prev_top) * 256) / (now_top - prev_top);
        if (frac < 0)
            frac = 0;
        if (frac > 256)
            frac = 256;
        cross_x = (b->x - b->vx) + (b->vx * frac) / 256;
    }

    /* The catch point decides the DIRECTION; the paddle's own motion may
     * only bend the angle WITHIN that direction. A ball caught on the
     * left always leaves to the left, a ball caught dead centre leaves
     * the way it was already travelling — the sweep is a garnish, never a
     * second opinion.
     *
     * (The english is signed by the sweep's direction, so sweeping with
     * the ball's line flattens the return and sweeping against it
     * steepens it — the way a moving surface actually works.) */
    /* The offset from the paddle's centre, in 1/16 px — CLAMPED to the
     * paddle's half width. The crossing point is a moment ago and the
     * paddle may have moved since, so without the clamp the catch point
     * can be off the paddle's edge and the launch angle can exceed the
     * angle the paddle is supposed to give. The edge is the maximum
     * angle; that is the contract. */
    off = cross_x - paddle_x * FP;
    {
        int max_off = paddle_w * FP / 2;
        if (off > max_off)
            off = max_off;
        if (off < -max_off)
            off = -max_off;
    }
    pos_step = (off * 6) / (paddle_w * FP / 2);
    step = pos_step;
    if (step == 0) {
        /* A dead-centre catch keeps the way the ball was already going, at
         * the shallowest legal angle. A dead VERTICAL launch is the
         * classic's worst stalemate: the ball goes up and down one
         * column forever and nothing is ever broken. The contact
         * recorder's audit found it: the ball sat in one column for
         * 2,000 frames, the score never moved. */
        step = (in_vx < 0) ? -1 : 1;
    }
    {
        int eng = paddle_vx / 5;
        int want = step + eng;
        /* the sweep bends the angle, never the direction */
        if ((step < 0 && want > 0) || (step > 0 && want < 0))
            want = step;                 /* a left catch never leaves right */
        if (want == 0)
            want = step;                 /* never a dead vertical launch */
        step = want;
    }
    k = step < 0 ? -step : step;
    if (k > 6)
        k = 6;
    step = step < 0 ? -k : k;
    /* the step's SIGN is the direction: the table holds magnitudes, and
     * without this the paddle always kicked the ball to the right —
     * whatever side it was caught on. (The contact recorder found it.) */
    {
        int vx = (ball_speed * ang_x[k]) >> 8;
        b->vx = (step < 0) ? -vx : vx;
    }
    b->vy = -(ball_speed * ang_y[k]) >> 8;
    b->y = (PADDLE_Y - BALL_S) * FP;
    /* publish the contact */
    hit_frame = machine_frame;
    hit_off = off;
    hit_side = pos_step < 0 ? -1 : (pos_step > 0 ? 1 : 0);
    hit_vx = b->vx;
    hit_vx_in = in_vx;             /* how it was travelling when it landed */
    hits++;
    hit_count = hits;             /* published for the tools */
    paddle_squash = 3;
    spawn_sparks(cross_x / FP, PADDLE_Y, 3, 5);
    sfx(440 + k * 90, 300 + k * 60, 6);
    trail_n = 0;
}

static void update_balls(void)
{
    int i, j;
    for (i = 0; i < 8; i++) {
        Ball *b = &ball_list[i];
        int px, py;
        if (!b->alive)
            continue;
        if (b->stuck) {
            b->x = paddle_x * FP;
            b->y = (PADDLE_Y - BALL_S) * FP;
            continue;
        }
        px = b->x / FP;
        py = b->y / FP;

        /* the trail */
        if (trail_n < 6) {
            trail_x[trail_n] = px;
            trail_y[trail_n] = py;
            trail_n++;
        } else {
            for (j = 0; j < 5; j++) {
                trail_x[j] = trail_x[j + 1];
                trail_y[j] = trail_y[j + 1];
            }
            trail_x[5] = px;
            trail_y[5] = py;
        }

        b->x += b->vx;
        b->y += b->vy;

        /* the walls */
        if (b->x < 0) {
            b->x = 0;
            b->vx = -b->vx;
            limit_flat(b);
            sfx(700, 500, 4);
        } else if (b->x > (SCREEN_W - BALL_S) * FP) {
            b->x = (SCREEN_W - BALL_S) * FP;
            b->vx = -b->vx;
            limit_flat(b);
            sfx(700, 500, 4);
        }
        if (b->y < HUD_H * FP) {
            b->y = HUD_H * FP;
            b->vy = -b->vy;
            limit_flat(b);
            sfx(700, 500, 4);
        }

        ball_vs_bricks(b);
        ball_vs_boss(b);
        ball_vs_paddle(b);
    }
}

/* ------------------------------------------------------------------------ */
/* the update                                                               */
/* ------------------------------------------------------------------------ */

static void update(void)
{
    unsigned pad = arcade1_pad();
    int i;

    frames++;

    if (state == ST_TITLE) {
        title_t++;
        if (pressed(pad, BTN_LEFT)) {
            title_level--;
            if (title_level < 1)
                title_level = 1;
            sfx(520, 660, 4);
        }
        if (pressed(pad, BTN_RIGHT)) {
            title_level++;
            if (title_level > BOSS_LEVEL)
                title_level = BOSS_LEVEL;
            sfx(660, 520, 4);
        }
        if (pressed(pad, BTN_START)) {
            reset_game();
            level = title_level;
            load_level(level);          /* the boss stage has no bricks */
            serve_ball();
            if (level == BOSS_LEVEL)
                start_boss();
            state = ST_SERVE;
            sfx(660, 990, 12);
        }
        prev_pad = pad;
        return;
    }
    if (state == ST_OVER || state == ST_WIN) {
        if (pressed(pad, BTN_START)) {
            /* back to the title, where the level can be picked again */
            state = ST_TITLE;
            title_t = 0;
            clear_run();
            sfx(660, 990, 12);
        }
        prev_pad = pad;
        return;
    }
    if (state == ST_DEAD) {
        if (--dead_timer <= 0) {
            if (lives <= 0) {
                state = ST_OVER;
            } else {
                /* the classic rule: a lost ball costs a life, NOT the
                 * bricks you have already broken */
                serve_ball();
                state = ST_SERVE;
            }
        }
        prev_pad = pad;
        return;
    }
    if (state == ST_CLEAR) {
        if (--clear_timer <= 0) {
            if (boss_cleared) {
                state = ST_WIN;
            } else {
                level++;
                if (level == BOSS_LEVEL) {
                    start_boss();
                    state = ST_SERVE;
                } else if (level > BOSS_LEVEL) {
                    state = ST_WIN;
                } else {
                    load_level(level);
                    serve_ball();
                    state = ST_SERVE;
                }
            }
        }
        prev_pad = pad;
        return;
    }

    /* --- the paddle -------------------------------------------------------- */
    {
        int before = paddle_x;
        if (pad & BTN_LEFT) {
            paddle_x -= PADDLE_SPEED;
            if (paddle_x < paddle_w / 2 + 2)
                paddle_x = paddle_w / 2 + 2;
        }
        if (pad & BTN_RIGHT) {
            paddle_x += PADDLE_SPEED;
            if (paddle_x > SCREEN_W - paddle_w / 2 - 2)
                paddle_x = SCREEN_W - paddle_w / 2 - 2;
        }
        paddle_vx = paddle_x - before;     /* the english */
    }
    if (paddle_squash > 0)
        paddle_squash--;

    /* --- the serve --------------------------------------------------------- */
    if (state == ST_SERVE) {
        if (pad & BTN_A) {
            for (i = 0; i < 8; i++) {
                if (!ball_list[i].alive)
                    continue;
                ball_list[i].stuck = 0;
                if (ball_list[i].vx == 0) {
                    /* a small, bounded launch angle — the classic's
                     * coin-flip launch made the first shot a lottery */
                    int k = 1 + arcade1_rand(3);      /* 1..3 */
                    ball_list[i].vx = (ball_speed * ang_x[k] * (arcade1_rand(2) ? 1 : -1)) >> 8;
                    ball_list[i].vy = -(ball_speed * ang_y[k]) >> 8;
                }
            }
            state = ST_PLAY;
            sfx(880, 1320, 8);
        }
        update_balls();
        return;
    }

    /* --- the boss ---------------------------------------------------------- */
    if (boss_state == 1) {
        boss_y += 2;
        if (boss_y >= 26) {
            boss_y = 26;
            boss_state = 2;
        }
    } else if (boss_state == 2) {
        int t = (int)(frames % 240u);
        if (t > 120)
            t = 240 - t;
        boss_x = (SCREEN_W - BOSS_W) / 2 - 60 + t;
        boss_timer++;
        if (boss_timer % 150 == 0)
            boss_fire();
    } else if (boss_state == 3) {
        boss_timer++;
        if (boss_timer % 5 == 0) {
            spawn_sparks(boss_x + arcade1_rand(BOSS_W),
                         boss_y + arcade1_rand(BOSS_H), 6, 11);
            sfx(300, 80, 10);
        }
        if (boss_timer > 80) {
            boss_state = 0;
            add_score(20000);
            /* the last stage ends like any other stage: the clear beat,
             * then the win */
            boss_cleared = 1;
            clear_timer = 120;
            state = ST_CLEAR;
            sfx(500, 1000, 30);
        }
    }

    /* --- the power-up timers ----------------------------------------------- */
    if (power_timer > 0)
        power_timer--;
    if (catch_timer > 0) {
        catch_timer--;
        if (catch_timer == 0) {
            power = 0;
            for (i = 0; i < 8; i++)
                if (ball_list[i].alive)
                    ball_list[i].stuck = 0;
        }
    }
    if (laser_timer > 0) {
        laser_timer--;
        if (laser_timer == 0)
            power = 0;
    }
    if (speed_timer > 0) {
        speed_timer--;
        if (speed_timer == 0)
            ball_speed = speed_for_level(level);   /* the stage's speed back */
    }

    /* --- the paddle's laser ------------------------------------------------ */
    if (laser_timer > 0) {
        if (laser_cool > 0)
            laser_cool--;
        if ((pad & BTN_A) && laser_cool == 0) {
            laser_cool = 10;
            for (i = 0; i < 8; i++) {
                if (!paddle_shots[i].alive) {
                    paddle_shots[i].x = paddle_x + paddle_w / 2 - 4;
                    paddle_shots[i].y = PADDLE_Y - 4;
                    paddle_shots[i].vy = -8;
                    paddle_shots[i].alive = 1;
                    break;
                }
            }
            sfx(1200, 900, 2);
        }
    }

    /* --- the balls --------------------------------------------------------- */
    update_balls();

    /* a ball that falls is a life */
    for (i = 0; i < 8; i++) {
        if (ball_list[i].alive && ball_list[i].y > (SCREEN_H + 8) * FP) {
            ball_list[i].alive = 0;
            balls--;
            if (balls <= 0) {
                lives--;
                dead_timer = 60;
                state = ST_DEAD;
                sfx(240, 60, 40);
                shake = 12;
            }
        }
    }

    /* --- the paddle's shots ------------------------------------------------ */
    for (i = 0; i < 8; i++) {
        Shot *s = &paddle_shots[i];
        int r, c;
        if (!s->alive)
            continue;
        s->y += s->vy;
        if (s->y < HUD_H) {
            s->alive = 0;
            continue;
        }
        for (r = 0; r < BRICK_ROWS; r++) {
            for (c = 0; c < BRICK_COLS; c++) {
                Brick *br = &bricks[r * BRICK_COLS + c];
                int bx = BRICK_X0 + c * BRICK_PITCH_X;
                int by = BRICK_Y0 + r * BRICK_PITCH_Y;
                if (!br->alive || br->kind == B_STEEL)
                    continue;
                if (!overlap(s->x - 1, s->y - 4, 3, 8, bx, by, BRICK_W, BRICK_H))
                    continue;
                s->alive = 0;
                br->hp--;
                if (br->hp <= 0) {
                    break_brick(r, c, 0);
                    if (bricks_left <= 0 && boss_state == 0) {
                        clear_timer = 100;
                        state = ST_CLEAR;
                    }
                }
                goto shot_done;
            }
        }
    shot_done:
        continue;
    }

    /* --- the boss's shots -------------------------------------------------- */
    for (i = 0; i < 8; i++) {
        Shot *s = &boss_shots[i];
        if (!s->alive)
            continue;
        s->y += s->vy;
        if (s->y > SCREEN_H) {
            s->alive = 0;
            continue;
        }
        if (overlap(s->x - 3, s->y - 3, 6, 6,
                    paddle_x - paddle_w / 2, PADDLE_Y, paddle_w, PADDLE_H)) {
            s->alive = 0;
            lives--;
            dead_timer = 60;
            state = ST_DEAD;
            sfx(240, 60, 40);
            shake = 12;
        }
    }

    /* --- the power-up drops ------------------------------------------------ */
    for (i = 0; i < 8; i++) {
        Drop *d = &drops[i];
        if (!d->alive)
            continue;
        d->y += 2;
        if (d->y > SCREEN_H) {
            d->alive = 0;
            continue;
        }
        if (overlap(d->x - 7, d->y - 7, 14, 14,
                    paddle_x - paddle_w / 2, PADDLE_Y, paddle_w, PADDLE_H)) {
            d->alive = 0;
            apply_power(d->kind);
        }
    }

    /* --- the bricks' hit flash --------------------------------------------- */
    for (i = 0; i < BRICK_ROWS * BRICK_COLS; i++)
        if (bricks[i].flash > 0)
            bricks[i].flash--;

    /* --- the sparks -------------------------------------------------------- */
    for (i = 0; i < SPARK_MAX; i++) {
        Spark *s = &sparks[i];
        if (!s->alive)
            continue;
        s->x += s->vx;
        s->y += s->vy;
        s->vy += 1;
        s->life--;
        if (s->life <= 0)
            s->alive = 0;
    }

    if (shake > 0)
        shake--;
    prev_pad = pad;
}

/* ------------------------------------------------------------------------ */
/* the drawing                                                              */
/* ------------------------------------------------------------------------ */

static void draw_background(void)
{
    int i;
    /* a vertical gradient: a dozen bands, one rect each */
    for (i = 0; i < 12; i++) {
        unsigned c = lerp565(arcade1_rgb(0x08, 0x10, 0x20), arcade1_rgb(0x1A, 0x10, 0x38), i, 11);
        arcade1_rect(0, i * 22, SCREEN_W, 23, c);
    }
    /* a faint grid, so the field has a floor to read against */
    for (i = 0; i < 12; i++)
        arcade1_rect(0, HUD_H + i * 20, SCREEN_W, 1, arcade1_rgb(0x14, 0x20, 0x3C));
    for (i = 0; i < 12; i++)
        arcade1_rect(i * 40, HUD_H, 1, SCREEN_H - HUD_H, arcade1_rgb(0x14, 0x20, 0x3C));
}

static void draw_bricks(void)
{
    int r, c;
    for (r = 0; r < BRICK_ROWS; r++) {
        for (c = 0; c < BRICK_COLS; c++) {
            Brick *b = &bricks[r * BRICK_COLS + c];
            int x = BRICK_X0 + c * BRICK_PITCH_X;
            int y = BRICK_Y0 + r * BRICK_PITCH_Y;
            if (!b->alive)
                continue;
            if (b->flash > 0)
                arcade1_rect(x, y, BRICK_W, 2, arcade1_rgb(0xFF, 0xF0, 0xC0));
            switch (b->kind) {
            case B_TOUGH:
                asset_sprite4(&brick_tough_frames[b->hp > 1 ? 0 : 1], x, y, 0);
                break;
            case B_STEEL:
                asset_sprite4(&brick_steel_frames[0], x, y, 0);
                break;
            case B_BOMB:
                asset_sprite4(&brick_bomb_frames[0], x, y, 0);
                break;
            default: {
                /* the row's colour: five banks, so the field reads as a
                 * gradient from the warm top rows to the cold ones */
                const sprite4_ref *fr = &brick0_frames[0];
                int k = r % 5;
                if (k == 1) fr = &brick1_frames[0];
                else if (k == 2) fr = &brick2_frames[0];
                else if (k == 3) fr = &brick3_frames[0];
                else if (k == 4) fr = &brick4_frames[0];
                asset_sprite4(fr, x, y, 0);
                break;
            }
            }
        }
    }
}

static void draw_boss(void)
{
    if (!boss_state)
        return;
    if (boss_state == 3) {
        if ((boss_timer / 3) & 1)
            asset_sprite4(&boss_frames[1], boss_x, boss_y, 0);
        else
            asset_sprite4(&boss_frames[0], boss_x, boss_y, 0);
        return;
    }
    asset_sprite4(&boss_frames[(boss_hp < 6) ? 1 : 0], boss_x, boss_y, 0);
    {
        int j;
        for (j = 0; j < 3; j++)
            asset_sprite4(&boss_core_frames[(frames / 8 + j) & 1],
                          boss_x + 46 + j * 54 - 10, boss_y + 56 - 10, 0);
    }
}

static void draw_paddle(void)
{
    const sprite4_ref *fr;
    if (paddle_w <= 44)
        fr = &paddle_s_frames[0];
    else if (paddle_w <= 64)
        fr = &paddle_m_frames[0];
    else
        fr = &paddle_l_frames[0];
    asset_sprite4(fr, paddle_x - paddle_w / 2,
                  PADDLE_Y + (paddle_squash ? 1 : 0), 0);
}

static void draw_fx(void)
{
    int i;
    /* the ball's trail */
    for (i = 1; i < trail_n; i++) {
        unsigned c = lerp565(arcade1_rgb(0x0A, 0x20, 0x38), arcade1_rgb(0x60, 0xC8, 0xF0), i, 6);
        arcade1_rect(trail_x[i] - 1, trail_y[i] - 1, 4, 4, c);
    }
    /* the sparks */
    for (i = 0; i < SPARK_MAX; i++) {
        Spark *s = &sparks[i];
        if (!s->alive)
            continue;
        arcade1_rect(s->x, s->y, 2, 2, 0xFFE0u);
    }
    /* the power-up drops */
    for (i = 0; i < 8; i++) {
        Drop *d = &drops[i];
        const sprite4_ref *fr = 0;
        if (!d->alive)
            continue;
        switch (d->kind) {
        case P_EXPAND: fr = &pow_expand_frames[0]; break;
        case P_MULTI:  fr = &pow_multi_frames[0]; break;
        case P_SLOW:   fr = &pow_slow_frames[0]; break;
        case P_FAST:   fr = &pow_fast_frames[0]; break;
        case P_LASER:  fr = &pow_laser_frames[0]; break;
        default:       fr = &pow_catch_frames[0]; break;
        }
        asset_sprite4(fr, d->x - 7, d->y - 7, 0);
    }
    /* the shots */
    for (i = 0; i < 8; i++) {
        if (paddle_shots[i].alive)
            arcade1_rect(paddle_shots[i].x - 1, paddle_shots[i].y - 4, 3, 8,
                         0xFFE0u);
        if (boss_shots[i].alive)
            arcade1_rect(boss_shots[i].x - 2, boss_shots[i].y - 3, 5, 6,
                         0xF060u);
    }
}

static void draw_hud(void)
{
    int i;
    arcade1_cam(0, 0);
    arcade1_rect(0, 0, SCREEN_W, HUD_H - 1, arcade1_rgb(0x0A, 0x10, 0x24));
    arcade1_rect(0, HUD_H - 1, SCREEN_W, 1, arcade1_rgb(0x2A, 0x3A, 0x70));

    text_draw(6, 2, "SCORE", arcade1_rgb(0x80, 0xC0, 0xFF));
    text_u32(46, 2, (unsigned)score, 7, 0xFFFFu);
    text_draw(SCREEN_W - 100, 2, "HI", arcade1_rgb(0xFF, 0xC0, 0x60));
    text_u32(SCREEN_W - 82, 2, (unsigned)hi_score, 7, 0xFFFFu);

    text_draw(150, 2, "LEVEL", arcade1_rgb(0x80, 0xC0, 0xFF));
    text_u32(186, 2, (unsigned)level, 2, 0xFFFFu);

    /* the lives: a little paddle each */
    text_draw(230, 2, "LIFE", arcade1_rgb(0x80, 0xC0, 0xFF));
    for (i = 0; i < lives && i < 5; i++)
        arcade1_rect(266 + i * 12, 3, 9, 4, arcade1_rgb(0x70, 0xE0, 0xF0));

    /* The power-up in force, with the time left. It lives on the SECOND
     * row: on the first it sat on top of the hi-score. */
    if (power) {
        int left = power_timer;
        text_draw(330, 11, "POWER", arcade1_rgb(0xFF, 0xC0, 0x60));
        arcade1_rect(378, 12, 60, 5, arcade1_rgb(0x20, 0x30, 0x50));
        arcade1_rect(378, 12, (left * 60) / 900, 5, arcade1_rgb(0x60, 0xFF, 0x90));
    }

    /* the boss's health bar */
    if (boss_state == 2 && boss_hp > 0) {
        int w = (boss_hp * 160) / 12;
        arcade1_rect(160, 16, 160, 3, arcade1_rgb(0x20, 0x20, 0x30));
        arcade1_rect(160, 16, w, 3, arcade1_rgb(0xE0, 0x40, 0x50));
    }

    if (state == ST_TITLE) {
        arcade1_rect(0, 84, SCREEN_W, 74, arcade1_rgb(0x08, 0x10, 0x20));
        text_draw(160, 92, "BRICKSTORM", arcade1_rgb(0x70, 0xE0, 0xF0));
        text_draw(148, 110, "TWELVE LEVELS AND A BOSS", arcade1_rgb(0xC0, 0xC0, 0xFF));
        text_draw(176, 122, "LEVEL", arcade1_rgb(0x80, 0xC0, 0xFF));
        text_u32(216, 122, (unsigned)title_level, 2, 0xFFFF);
        text_draw(240, 122, level_names[title_level - 1], arcade1_rgb(0xFF, 0xD0, 0x60));
        text_draw(120, 136, "LEFT RIGHT PICK", arcade1_rgb(0x70, 0x80, 0xA0));
        if ((title_t / 30) & 1)
            text_draw(160, 148, "PRESS START", arcade1_rgb(0xFF, 0xD0, 0x60));
    } else if (state == ST_OVER) {
        arcade1_rect(0, 100, SCREEN_W, 48, arcade1_rgb(0x08, 0x10, 0x20));
        text_draw(180, 108, "GAME OVER", arcade1_rgb(0xFF, 0x50, 0x50));
        text_u32(150, 124, (unsigned)score, 7, 0xFFFFu);
        if ((frames / 30) & 1)
            text_draw(160, 136, "PRESS START", arcade1_rgb(0xFF, 0xD0, 0x60));
    } else if (state == ST_WIN) {
        arcade1_rect(0, 100, SCREEN_W, 48, arcade1_rgb(0x08, 0x10, 0x20));
        text_draw(196, 108, "YOU WIN", arcade1_rgb(0x60, 0xFF, 0x90));
        text_u32(150, 124, (unsigned)score, 7, 0xFFFFu);
        if ((frames / 30) & 1)
            text_draw(160, 136, "PRESS START", arcade1_rgb(0xFF, 0xD0, 0x60));
    } else if (state == ST_CLEAR) {
        if (boss_cleared)
            text_draw(152, 110, "THE CORE IS DOWN", arcade1_rgb(0x60, 0xFF, 0x90));
        else
            text_draw(168, 110, "LEVEL CLEAR", arcade1_rgb(0x60, 0xFF, 0x90));
    } else if (boss_state == 1) {
        text_draw(140, 60, "WARNING", arcade1_rgb(0xFF, 0x40, 0x40));
    }
    /* the level's name, flashed when a stage begins */
    if (state == ST_SERVE && frames < 90 && level <= LEVEL_COUNT)
        text_draw(176, 120, level_names[level - 1], arcade1_rgb(0xFF, 0xD0, 0x60));
}

int main(void)
{
    int i;

    /* the palettes */
    gfx_pal_upload(0 * 16, row0_palette, 16);
    gfx_pal_upload(1 * 16, row1_palette, 16);
    gfx_pal_upload(2 * 16, row2_palette, 16);
    gfx_pal_upload(3 * 16, row3_palette, 16);
    gfx_pal_upload(4 * 16, row4_palette, 16);
    gfx_pal_upload(5 * 16, hero_palette, 16);
    gfx_pal_upload(6 * 16, pow_a_palette, 16);
    gfx_pal_upload(7 * 16, pow_b_palette, 16);
    gfx_pal_upload(8 * 16, pow_c_palette, 16);
    gfx_pal_upload(9 * 16, boss_palette, 16);
    gfx_pal_upload(10 * 16, fx_palette, 16);

    state = ST_TITLE;
    title_t = 0;
    clear_run();

    for (;;) {
        int sx = 0, sy = 0;

        machine_frame = (int)arcade1_timer_count();
        update();
        sfx_update();

        if (shake > 0) {
            sx = arcade1_rand(5) - 2;
            sy = arcade1_rand(5) - 2;
        }

        arcade1_cam(sx, sy);
        draw_background();
        draw_bricks();
        draw_boss();
        draw_fx();
        for (i = 0; i < 8; i++) {
            if (!ball_list[i].alive)
                continue;
            asset_sprite4(&ball_frames[(frames / 6) & 1],
                          ball_list[i].x / FP - 4, ball_list[i].y / FP - 4, 0);
        }
        draw_paddle();
        draw_hud();
        /* The observables must all describe the SAME ball — the first one
         * alive. The position used to come from the LAST ball drawn and
         * the velocity from the FIRST, so with the MULTI power-up in
         * flight the tools read two different balls and every direction
         * change looked like a phantom hand on the ball. */
        for (i = 0; i < 8; i++) {
            if (ball_list[i].alive) {
                ball_x = ball_list[i].x / FP;
                ball_y = ball_list[i].y / FP;
                ball_vx = ball_list[i].vx;
                ball_vy = ball_list[i].vy;
                ball_id = i;
                break;
            }
        }
        if (i == 8)
            ball_id = -1;
        arcade1_present();
    }
    return 0;
}
