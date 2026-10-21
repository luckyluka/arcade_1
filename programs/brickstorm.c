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
 *   paddle_x, power, boss_hp, boss_state, balls
 *
 *   arcade1 run brickstorm.arc1 --vars state,score,lives,level
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
#define SERVE_SPEED  52       /* 3.25 px per frame */
#define BALL_MAX_V   96       /* 6 px per frame: the fastest it gets */
#define BALL_MIN_V   26       /* 1.6 px: the ball never crawls sideways */

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

/* ------------------------------------------------------------------------ */
/* the entities                                                             */
/* ------------------------------------------------------------------------ */

typedef struct {
    int kind;              /* the brick kind */
    int hp;
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
static int ball_speed = SERVE_SPEED;
static int drop_timer;
static int laser_timer;        /* the frames of the laser left */
static int catch_timer;        /* the frames of the catch left */
static int laser_cool;         /* the frames until the next laser shot */
static int dead_timer;
static int clear_timer;
static int shake;
static int boss_x, boss_y;
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
 * degrees, as unit vectors in hundredths. */
static const signed char ang_x[7] = { 0, 17, 34, 50, 64, 77, 87 };
static const signed char ang_y[7] = { 100, 98, 94, 87, 77, 64, 50 };

/* ------------------------------------------------------------------------ */
/* the levels                                                               */
/* ------------------------------------------------------------------------ */

static void load_level(int n)
{
    int r, c;
    bricks_left = 0;
    for (r = 0; r < BRICK_ROWS; r++) {
        for (c = 0; c < BRICK_COLS; c++) {
            Brick *b = &bricks[r * BRICK_COLS + c];
            char ch = '.';
            if (n >= 1 && n <= LEVEL_COUNT)
                ch = levels[n - 1][r * BRICK_COLS + c];
            b->alive = 0;
            b->hp = 0;
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
        if (ball_speed > 30)
            ball_speed -= 8;
        break;
    case P_FAST:
        if (ball_speed < BALL_MAX_V)
            ball_speed += 8;
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
    ball_speed = SERVE_SPEED;
    power = 0;
    power_timer = 0;
    laser_timer = 0;
    catch_timer = 0;
    shake = 0;
    boss_state = 0;
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
    /* the drop */
    drop_timer--;
    if (drop_timer <= 0 && bricks_left > 0) {
        drop_timer = 5;
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

            if (br->kind == B_STEEL) {
                sfx(240, 200, 8);
                shake = 3;
                return;
            }
            br->hp--;
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
        sfx(300, 240, 6);
    }
}

/* The ball against the paddle: where it lands decides the angle. */
static void ball_vs_paddle(Ball *b)
{
    int px = b->x / FP;
    int py = b->y / FP;
    int off, step, k;
    if (b->vy <= 0)
        return;
    if (py + BALL_S < PADDLE_Y || py > PADDLE_Y + PADDLE_H)
        return;
    if (px + BALL_S <= paddle_x - paddle_w / 2 ||
        px >= paddle_x + paddle_w / 2)
        return;
    off = px + BALL_S / 2 - paddle_x;
    step = (off * 6) / (paddle_w / 2);
    k = step < 0 ? -step : step;
    if (k > 6)
        k = 6;
    step = step < 0 ? -k : k;
    b->vx = (ball_speed * ang_x[k]) / 100;
    b->vy = -(ball_speed * ang_y[k]) / 100;
    if (b->vy > -(BALL_MIN_V * 100) / 100)
        b->vy = -(BALL_MIN_V * 100) / 100;
    b->y = (PADDLE_Y - BALL_S) * FP;
    if (ball_speed < BALL_MAX_V)
        ball_speed += 4;
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
            sfx(700, 500, 4);
        } else if (b->x > (SCREEN_W - BALL_S) * FP) {
            b->x = (SCREEN_W - BALL_S) * FP;
            b->vx = -b->vx;
            sfx(700, 500, 4);
        }
        if (b->y < HUD_H * FP) {
            b->y = HUD_H * FP;
            b->vy = -b->vy;
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
        prev_pad = pad;
        return;
    }

    /* --- the paddle -------------------------------------------------------- */
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

    /* --- the serve --------------------------------------------------------- */
    if (state == ST_SERVE) {
        if (pad & BTN_A) {
            for (i = 0; i < 8; i++) {
                if (!ball_list[i].alive)
                    continue;
                ball_list[i].stuck = 0;
                if (ball_list[i].vx == 0)
                    ball_list[i].vx = (arcade1_rand(2) ? 1 : -1) * 12;
                ball_list[i].vy = -(ball_speed * 70) / 100;
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
            state = ST_WIN;
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
    asset_sprite4(fr, paddle_x - paddle_w / 2, PADDLE_Y, 0);
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

    /* the power-up in force, with the time left */
    if (power) {
        int left = power_timer;
        text_draw(340, 2, "POWER", arcade1_rgb(0xFF, 0xC0, 0x60));
        arcade1_rect(392, 3, 60, 5, arcade1_rgb(0x20, 0x30, 0x50));
        arcade1_rect(392, 3, (left * 60) / 900, 5, arcade1_rgb(0x60, 0xFF, 0x90));
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
            ball_x = ball_list[i].x / FP;
            ball_y = ball_list[i].y / FP;
            asset_sprite4(&ball_frames[(frames / 6) & 1], ball_x - 4,
                          ball_y - 4, 0);
        }
        draw_paddle();
        draw_hud();
        arcade1_present();
    }
    return 0;
}
