/* TEMPEST — a vertical shoot-'em-up for ARCADE-1.
 *
 * A classic arcade shmup, modernised: the player flies a cyan interceptor
 * continuously upward through a scrolling battlefield, shooting formations
 * of fighters, diving drones, attack helicopters, heavy bombers and a
 * cruiser, while anti-aircraft turrets, tanks and missile launchers work
 * the ground below. Every stage ends in a multi-phase boss.
 *
 * The game is freestanding C for RV32IM — no libc, deterministic (the
 * wave schedule is a table, the randomness is a seeded LCG, the audio is a
 * register, never host time).
 *
 * Observable globals (read with --vars):
 *   state, score, hi_score, lives, stage, wave, px, py, weapon, shield,
 *   boss_hp, boss_phase, kills, enemies, shots
 *
 *   arcade1 run tempest.arc1 --vars state,score,lives,boss_hp
 *
 * States: 0 = the title, 1 = flying, 2 = the death pause, 3 = game over.
 *
 * The controls: the D-pad flies, A fires, B is the FOCUS (a slower,
 * tighter shot and the hitbox on screen — the classic bullet-hell
 * bargain), START starts and restarts.
 */
#include "asset.h"
#include "shmup_assets.h"
#include "text.h"

#define SCREEN_W  480
#define SCREEN_H  270
#define HUD_H     20          /* the HUD strip across the top */
#define GROUND_TOP 214        /* the terrain band at the bottom */

/* --- the player's ship ---------------------------------------------------- */
#define P_SPEED   4           /* px per frame */
#define P_FOCUS   1           /* the focus speed (BTN_B) */
#define P_FIRE    5           /* the frames between shots */
#define P_HITBOX  4           /* the hitbox is 4x4 — the classic tiny target */
#define P_BOTTOM  204         /* the lowest the ship may fly */
#define P_TOP     (HUD_H + 14)

/* --- the world ------------------------------------------------------------ */
#define SCROLL_BASE 2         /* the world scrolls down at 2 px/frame */
#define BOSS_SCROLL 1800      /* the boss arrives after this much scroll */
#define STAR_COUNT  76        /* the stars in the two layers */

/* --- the entity pools ----------------------------------------------------- */
#define ENEMY_MAX  48
#define BULLET_MAX 200
#define BOOM_MAX   32
#define PICKUP_MAX 8
#define POPUP_MAX  8
#define SPARK_MAX  48

/* --- the enemy kinds ------------------------------------------------------ */
enum {
    E_FIGHTER, E_DRONE, E_COPTER, E_BOMBER, E_CRUISER,
    E_TURRET, E_TANK, E_LAUNCHER, E_DEBRIS, E_KINDS
};

/* --- the bullet kinds ----------------------------------------------------- */
enum { B_PLAYER, B_FOE, B_MISSILE };

/* --- the wave patterns ---------------------------------------------------- */
enum {
    P_LINE,      /* a horizontal line of them */
    P_V,         /* a V: the middle one flies lower */
    P_DIVE,      /* a diagonal stream */
    P_PAIR,      /* two groups, one from each side */
    P_SINGLE,    /* one */
    P_GROUND     /* along the terrain band */
};

/* ------------------------------------------------------------------------ */
/* the observable state                                                     */
/* ------------------------------------------------------------------------ */

int state = 0;             /* 0 title, 1 flying, 2 dying, 3 game over */
int score = 0;
int hi_score = 0;
int lives = 3;
int stage = 1;
int wave = 0;             /* the wave index the stage is on */
int px = 240, py = 200;   /* the ship's centre */
int weapon = 1;           /* the weapon level, 1..4 */
int shield = 0;           /* the frames of shield left */
int boss_hp = 0;          /* the boss's remaining hit points */
int boss_phase = 0;       /* 1, 2 or 3 */
int kills = 0;
int enemies = 0;          /* the enemies alive */
int shots = 0;            /* the shots fired */

/* ------------------------------------------------------------------------ */
/* the entities                                                             */
/* ------------------------------------------------------------------------ */

typedef struct {
    int type;
    int x, y;              /* the sprite's top-left */
    int vx, vy;
    int hp, maxhp;
    int t;                 /* a per-enemy timer */
    int fire;              /* the frames until the next shot */
    int anchor;            /* the formation's x */
    int alive;
} Enemy;

typedef struct {
    int x, y, vx, vy;
    int kind;
    int alive;
} Bullet;

typedef struct { int x, y, t, alive; } Boom;
typedef struct { int x, y, vy, kind, alive; } Pickup;
typedef struct { int x, y, t, value, alive; } Popup;
typedef struct { int x, y, vx, vy, life, alive; } Spark;

static Enemy   foes[ENEMY_MAX];
static Bullet  bullets[BULLET_MAX];
static Boom    booms[BOOM_MAX];
static Pickup  pickups[PICKUP_MAX];
static Popup   popups[POPUP_MAX];
static Spark   sparks[SPARK_MAX];

/* the stars: a fixed table, scrolled by the world's travel */
static const unsigned short star_x[STAR_COUNT] = {
    308, 306, 224, 478, 76, 10, 376, 150, 388, 354, 112, 430, 92,
    346, 360, 134, 372, 82, 64, 222, 428, 170, 88, 54, 68, 98,
    368, 78, 348, 58, 264, 326, 404, 370, 192, 222, 236, 138, 120,
    374, 356, 34, 48, 430, 476, 58, 104, 422, 340, 242, 352, 318,
    396, 330, 24, 278, 100, 66, 432, 78, 476, 442, 168, 294, 84,
    434, 96, 222, 236, 266, 376, 374, 164, 418, 176, 14,
};
static const unsigned short star_y[STAR_COUNT] = {
    239, 105, 97, 21, 25, 5, 215, 167, 3, 217, 71, 55, 225,
    221, 169, 169, 103, 65, 81, 141, 197, 225, 121, 73, 155, 267,
    259, 175, 61, 73, 81, 217, 91, 261, 73, 225, 181, 149, 19,
    33, 53, 203, 111, 75, 211, 249, 63, 67, 135, 13, 123, 165,
    69, 53, 89, 55, 121, 79, 227, 169, 257, 61, 151, 95, 113,
    71, 31, 87, 169, 137, 185, 111, 75, 261, 109, 139,
};

/* ------------------------------------------------------------------------ */
/* the run's private state                                                  */
/* ------------------------------------------------------------------------ */

static unsigned frames;         /* the frames since the reset */
static unsigned prev_pad;       /* the pad of the last frame */
static int scroll;               /* how far the world has scrolled, in px */
static int scroll_speed;         /* the current scroll speed */
static int stage_scroll;         /* the scroll at the start of the stage */
static int invuln;               /* the frames of invulnerability left */
static int dead_timer;           /* the death pause */
static int combo;                /* the kill combo */
static int combo_timer;          /* the frames left on the combo */
static int shake;                /* the screen-shake frames */
static int fire_timer;           /* the frames until the next shot */
static int boss_timer;           /* the boss's behaviour timer */
static int boss_x, boss_y;       /* the boss's top-left */
static int boss_state;           /* 0 none, 1 entering, 2 fighting, 3 dying */
static int boss_dying;           /* the frames the death animation runs */
static int boss_hp_max;
static int banner;               /* the banner's frames */
static int banner_msg;           /* 0 = warning, 1 = stage clear */
static int title_t;              /* the title screen's clock */
static int extra_life;           /* the extra life already given */

/* the audio: one tone at a time, with a pitch sweep */
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
/* the wave table: the stage, written out                                   */
/* ------------------------------------------------------------------------ */

typedef struct {
    short scroll;     /* the distance into the stage at which it appears */
    short type;
    short count;
    short pattern;
    short x;
    short y;
} Wave;

static const Wave wave_table[] = {
    /* --- stage 1: the approach ------------------------------------- */
    {   0, E_FIGHTER, 5, P_LINE,   150, -20 },
    { 120, E_FIGHTER, 5, P_V,      170, -30 },
    { 300, E_DRONE,   4, P_DIVE,    60, -20 },
    { 420, E_TURRET,  2, P_GROUND,  80, 0 },
    { 600, E_FIGHTER, 7, P_LINE,   100, -20 },
    { 760, E_COPTER,  1, P_SINGLE,  40, -40 },
    { 900, E_DEBRIS,  3, P_SINGLE, 150, -20 },
    {1050, E_DRONE,   6, P_DIVE,   300, -20 },
    {1200, E_BOMBER,  1, P_SINGLE, 200, -40 },
    {1350, E_FIGHTER, 5, P_V,      140, -30 },
    {1500, E_TANK,    2, P_GROUND,  60, 0 },
    {1650, E_COPTER,  2, P_PAIR,    40, -40 },
    {1800, E_CRUISER, 1, P_SINGLE, 200, -45 },
    {1950, E_FIGHTER, 6, P_LINE,    90, -20 },
    {2100, E_LAUNCHER,2, P_GROUND, 220, 0 },
    {2250, E_DRONE,   8, P_DIVE,    40, -20 },
    /* --- stage 2: the pressure ------------------------------------- */
    { 200, E_FIGHTER, 7, P_V,      120, -30 },
    { 420, E_DRONE,   8, P_DIVE,   260, -20 },
    { 640, E_COPTER,  3, P_PAIR,    60, -40 },
    { 900, E_BOMBER,  2, P_SINGLE, 140, -40 },
    {1150, E_TURRET,  3, P_GROUND, 160, 0 },
    {1400, E_FIGHTER, 9, P_LINE,    70, -20 },
    {1650, E_CRUISER, 1, P_SINGLE, 190, -45 },
    {1900, E_DRONE,  10, P_DIVE,    30, -20 },
    {2150, E_TANK,    3, P_GROUND, 100, 0 },
    {2350, E_LAUNCHER,3, P_GROUND, 240, 0 },
    {2500, E_COPTER,  4, P_PAIR,    40, -40 },
    {2750, E_FIGHTER, 9, P_V,      110, -30 },
    {3000, E_DEBRIS,  5, P_SINGLE, 120, -20 },
};

#define WAVE_TOTAL ((int)(sizeof(wave_table) / sizeof(wave_table[0])))

/* Which slice of the table a stage flies: stage 1 the first sixteen
 * waves, stage 2 the rest, and stage 3+ flies stage 2's waves with the
 * difficulty turned up. */
static int stage_first(void)
{
    return (stage == 1) ? 0 : 16;
}

static int stage_count(void)
{
    return (stage == 1) ? 16 : (WAVE_TOTAL - 16);
}

/* ------------------------------------------------------------------------ */
/* helpers                                                                  */
/* ------------------------------------------------------------------------ */

/* A press is an edge, not a hold: START must not restart the game over
 * and over while it is held down. */
static int pressed(unsigned pad, unsigned bit)
{
    return (pad & bit) != 0u && (prev_pad & bit) == 0u;
}

static int overlap(int ax, int ay, int aw, int ah,
                   int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

/* An enemy's hit box: the sprite is bigger than the thing you can hit. */
static void foe_box(int type, int *bx, int *by, int *bw, int *bh)
{
    switch (type) {
    case E_FIGHTER: *bx = 4; *by = 4; *bw = 8;  *bh = 8;  break;
    case E_DRONE:   *bx = 3; *by = 3; *bw = 6;  *bh = 6;  break;
    case E_COPTER:  *bx = 6; *by = 6; *bw = 20; *bh = 12; break;
    case E_BOMBER:  *bx = 8; *by = 6; *bw = 32; *bh = 20; break;
    case E_CRUISER: *bx = 10; *by = 8; *bw = 44; *bh = 24; break;
    case E_TURRET:  *bx = 3; *by = 8; *bw = 10; *bh = 8;  break;
    case E_TANK:    *bx = 3; *by = 3; *bw = 18; *bh = 8;  break;
    case E_LAUNCHER:*bx = 3; *by = 3; *bw = 18; *bh = 8;  break;
    case E_DEBRIS:  *bx = 4; *by = 4; *bw = 8;  *bh = 8;  break;
    default:        *bx = 0; *by = 0; *bw = 8;  *bh = 8;  break;
    }
}

static int foe_score(int type)
{
    switch (type) {
    case E_FIGHTER: return 100;
    case E_DRONE:   return 150;
    case E_COPTER:  return 300;
    case E_BOMBER:  return 1000;
    case E_CRUISER: return 2000;
    case E_TURRET:  return 200;
    case E_TANK:    return 400;
    case E_LAUNCHER:return 500;
    case E_DEBRIS:  return 50;
    default:        return 100;
    }
}

static void add_score(int n)
{
    int mult = 1 + combo / 4;
    if (mult > 8)
        mult = 8;
    score += n * mult;
    if (score > hi_score)
        hi_score = score;
    if (!extra_life && score >= 50000) {
        extra_life = 1;
        lives++;
        sfx(880, 1320, 20);
    }
}

static void spawn_boom(int x, int y)
{
    int i;
    for (i = 0; i < BOOM_MAX; i++) {
        if (!booms[i].alive) {
            booms[i].x = x;
            booms[i].y = y;
            booms[i].t = 0;
            booms[i].alive = 1;
            return;
        }
    }
}

static void spawn_sparks(int x, int y, int n)
{
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < SPARK_MAX; j++) {
            if (!sparks[j].alive) {
                sparks[j].x = x;
                sparks[j].y = y;
                sparks[j].vx = arcade1_rand(7) - 3;
                sparks[j].vy = arcade1_rand(7) - 3;
                sparks[j].life = 8 + arcade1_rand(10);
                sparks[j].alive = 1;
                break;
            }
        }
    }
}

static void spawn_popup(int x, int y, int value)
{
    int i;
    for (i = 0; i < POPUP_MAX; i++) {
        if (!popups[i].alive) {
            popups[i].x = x;
            popups[i].y = y;
            popups[i].value = value;
            popups[i].t = 40;
            popups[i].alive = 1;
            return;
        }
    }
}

static void spawn_pickup(int x, int y, int kind)
{
    int i;
    for (i = 0; i < PICKUP_MAX; i++) {
        if (!pickups[i].alive) {
            pickups[i].x = x;
            pickups[i].y = y;
            pickups[i].vy = 1;
            pickups[i].kind = kind;
            pickups[i].alive = 1;
            return;
        }
    }
}

static Enemy *new_foe(int type, int x, int y)
{
    int i;
    for (i = 0; i < ENEMY_MAX; i++) {
        if (!foes[i].alive) {
            Enemy *e = &foes[i];
            e->type = type;
            e->x = x;
            e->y = y;
            e->vx = 0;
            e->vy = 0;
            e->t = 0;
            e->anchor = x;
            e->alive = 1;
            switch (type) {
            case E_FIGHTER: e->hp = 2; e->fire = 60; break;
            case E_DRONE:   e->hp = 1; e->fire = 0;  break;
            case E_COPTER:  e->hp = 8; e->fire = 50; break;
            case E_BOMBER:  e->hp = 24; e->fire = 70; break;
            case E_CRUISER: e->hp = 40; e->fire = 60; break;
            case E_TURRET:  e->hp = 5; e->fire = 80; break;
            case E_TANK:    e->hp = 10; e->fire = 90; break;
            case E_LAUNCHER:e->hp = 8; e->fire = 110; break;
            case E_DEBRIS:  e->hp = 3; e->fire = 0;  break;
            default:        e->hp = 1; e->fire = 0;  break;
            }
            e->maxhp = e->hp;
            return e;
        }
    }
    return 0;
}

static void spawn_bullet(int x, int y, int vx, int vy, int kind)
{
    int i;
    for (i = 0; i < BULLET_MAX; i++) {
        if (!bullets[i].alive) {
            bullets[i].x = x;
            bullets[i].y = y;
            bullets[i].vx = vx;
            bullets[i].vy = vy;
            bullets[i].kind = kind;
            bullets[i].alive = 1;
            return;
        }
    }
}

/* An aimed shot: the direction is an octant, so the shot always arrives
 * at about the speed it was aimed at. */
static void fire_aimed(int x, int y, int speed)
{
    int dx = px - x, dy = py - y;
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    int m = ax > ay ? ax : ay;
    int vx, vy;
    if (m == 0) {
        vx = 0;
        vy = speed;
    } else {
        vx = (dx * speed) / m;
        vy = (dy * speed) / m;
    }
    spawn_bullet(x, y, vx, vy, B_FOE);
}

/* A fan of shots from a point: `spread` shots, `ang` apart. */
static void fire_fan(int x, int y, int spread, int ang, int speed)
{
    int i;
    for (i = 0; i < spread; i++) {
        spawn_bullet(x, y, (i - spread / 2) * ang, speed, B_FOE);
    }
}

static void kill_foe(Enemy *e)
{
    int bx, by, bw, bh, cx, cy;
    foe_box(e->type, &bx, &by, &bw, &bh);
    cx = e->x + bx + bw / 2;
    cy = e->y + by + bh / 2;
    e->alive = 0;
    kills++;
    combo++;
    combo_timer = 60;
    add_score(foe_score(e->type));
    spawn_popup(e->x + 8, e->y, foe_score(e->type));
    spawn_sparks(cx, cy, 6);
    if (e->type == E_BOMBER || e->type == E_CRUISER) {
        spawn_boom(e->x + 24, e->y + 16);
        spawn_boom(e->x + 12, e->y + 24);
        spawn_boom(e->x + 36, e->y + 24);
        sfx(320, 60, 26);
        if (arcade1_rand(3) == 0)
            spawn_pickup(e->x + 24, e->y + 16, 0);
    } else {
        spawn_boom(cx, cy);
        sfx(520, 120, 16);
        if (e->type == E_COPTER && arcade1_rand(4) == 0)
            spawn_pickup(cx, cy, 1);
    }
    shake = 3;
}

static void hurt_player(void)
{
    if (state != 1)
        return;
    if (shield > 0) {
        shield = 0;
        invuln = 60;
        spawn_sparks(px, py, 8);
        sfx(300, 120, 20);
        shake = 6;
        return;
    }
    lives--;
    state = 2;
    dead_timer = 70;
    invuln = 0;
    {
        /* the classic mercy: the screen clears when the ship dies, so the
         * respawn is not a death spiral */
        int i;
        for (i = 0; i < BULLET_MAX; i++)
            bullets[i].alive = 0;
    }
    spawn_boom(px, py);
    spawn_sparks(px, py, 12);
    sfx(240, 60, 40);
    shake = 12;
}

/* ------------------------------------------------------------------------ */
/* the waves                                                                */
/* ------------------------------------------------------------------------ */

static void spawn_wave(const Wave *w)
{
    int i, x, y;
    switch (w->pattern) {
    case P_LINE:
        for (i = 0; i < w->count; i++) {
            x = w->x + i * 26;
            if (x > SCREEN_W - 20)
                x = w->x - i * 26;
            new_foe(w->type, x, w->y);
        }
        break;
    case P_V:
        for (i = 0; i < w->count; i++) {
            int off = i - w->count / 2;
            x = w->x + off * 26;
            y = w->y + (off < 0 ? -off : off) * 12;
            new_foe(w->type, x, y);
        }
        break;
    case P_DIVE:
        for (i = 0; i < w->count; i++) {
            Enemy *e;
            x = w->x + i * 22;
            e = new_foe(w->type, x, w->y - i * 14);
            if (e && e->type == E_DRONE)
                e->vx = (i & 1) ? 1 : -1;
        }
        break;
    case P_PAIR:
        for (i = 0; i < w->count; i++) {
            if (i & 1) {
                x = SCREEN_W - 40 - (i / 2) * 26;
                new_foe(w->type, x, w->y - (i / 2) * 16);
            } else {
                x = 40 + (i / 2) * 26;
                new_foe(w->type, x, w->y - (i / 2) * 16);
            }
        }
        break;
    case P_GROUND:
        for (i = 0; i < w->count; i++) {
            x = w->x + i * 40;
            if (x > SCREEN_W - 30)
                x = w->x - i * 40;
            {
                Enemy *e = new_foe(w->type, x, GROUND_TOP - 12);
                if (e && w->type == E_TANK)
                    e->vx = (i & 1) ? -1 : 1;
            }
        }
        break;
    default:
        new_foe(w->type, w->x, w->y);
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* the boss                                                                 */
/* ------------------------------------------------------------------------ */

static void start_boss(void)
{
    boss_x = (SCREEN_W - BOSS_W) / 2;
    boss_y = -BOSS_H;
    boss_state = 1;
    boss_timer = 0;
    boss_phase = 1;
    boss_hp_max = 120 + (stage - 1) * 60;
    boss_hp = boss_hp_max;
    sfx(160, 480, 60);
    banner = 100;
    banner_msg = 0;
}

static void update_boss(void)
{
    if (boss_state == 1) {
        /* the entrance */
        boss_y += 2;
        if (boss_y >= 24) {
            boss_y = 24;
            boss_state = 2;
        }
        return;
    }
    if (boss_state != 2)
        return;

    /* the hover: a slow, SMOOTH sweep across the top (a square wave would
     * jump, and the player could never line a shot up with it) */
    {
        int t = (int)(frames % 200u);
        if (t > 100)
            t = 200 - t;
        boss_x = (SCREEN_W - BOSS_W) / 2 - 80 + (t * 160) / 100;
    }
    boss_timer++;

    /* the phases: the boss gets angrier as it dies */
    if (boss_hp * 3 < boss_hp_max)
        boss_phase = 3;
    else if (boss_hp * 3 < boss_hp_max * 2 && boss_phase < 2)
        boss_phase = 2;

    if (boss_phase == 1) {
        if (boss_timer % 70 == 0) {
            fire_aimed(boss_x + 30, boss_y + BOSS_H - 12, 3);
            fire_aimed(boss_x + BOSS_W - 30, boss_y + BOSS_H - 12, 3);
        }
    } else if (boss_phase == 2) {
        if (boss_timer % 80 == 0) {
            fire_fan(boss_x + BOSS_W / 2, boss_y + BOSS_H - 12, 5, 2, 2);
            fire_aimed(boss_x + 20, boss_y + BOSS_H - 12, 3);
            fire_aimed(boss_x + BOSS_W - 20, boss_y + BOSS_H - 12, 3);
        }
        if (boss_timer % 150 == 0) {
            Enemy *e = new_foe(E_DRONE, boss_x + 20, boss_y + BOSS_H - 24);
            if (e)
                e->vx = -2;
            e = new_foe(E_DRONE, boss_x + BOSS_W - 32, boss_y + BOSS_H - 24);
            if (e)
                e->vx = 2;
        }
    } else {
        if (boss_timer % 55 == 0) {
            fire_fan(boss_x + BOSS_W / 2, boss_y + BOSS_H - 12, 7, 2, 2);
            if (boss_timer % 80 == 0)
                fire_aimed(boss_x + BOSS_W / 2, boss_y + BOSS_H - 12, 4);
        }
        if (boss_timer % 120 == 0) {
            int i;
            for (i = 0; i < 10; i++) {
                int a = i * 36;
                spawn_bullet(boss_x + BOSS_W / 2, boss_y + BOSS_H / 2,
                             (a - 180) / 40, 2, B_FOE);
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* the player's shot                                                        */
/* ------------------------------------------------------------------------ */

static void fire_player(void)
{
    int level = weapon;
    int nose = py - 14;
    shots++;
    if (level == 1) {
        spawn_bullet(px, nose, 0, -9, B_PLAYER);
    } else if (level == 2) {
        spawn_bullet(px - 4, nose, 0, -9, B_PLAYER);
        spawn_bullet(px + 4, nose, 0, -9, B_PLAYER);
    } else if (level == 3) {
        spawn_bullet(px - 6, nose, 0, -9, B_PLAYER);
        spawn_bullet(px, nose - 2, 0, -10, B_PLAYER);
        spawn_bullet(px + 6, nose, 0, -9, B_PLAYER);
    } else {
        spawn_bullet(px - 8, nose, -1, -9, B_PLAYER);
        spawn_bullet(px - 3, nose, 0, -10, B_PLAYER);
        spawn_bullet(px + 3, nose, 0, -10, B_PLAYER);
        spawn_bullet(px + 8, nose, 1, -9, B_PLAYER);
    }
    sfx(1500, 900, 3);
}

/* ------------------------------------------------------------------------ */
/* the reset                                                                */
/* ------------------------------------------------------------------------ */

static void clear_run(void)
{
    int i;
    for (i = 0; i < ENEMY_MAX; i++)
        foes[i].alive = 0;
    for (i = 0; i < BULLET_MAX; i++)
        bullets[i].alive = 0;
    for (i = 0; i < BOOM_MAX; i++)
        booms[i].alive = 0;
    for (i = 0; i < PICKUP_MAX; i++)
        pickups[i].alive = 0;
    for (i = 0; i < POPUP_MAX; i++)
        popups[i].alive = 0;
    for (i = 0; i < SPARK_MAX; i++)
        sparks[i].alive = 0;
    enemies = 0;
}

static void reset_game(void)
{
    clear_run();
    score = 0;
    lives = 3;
    stage = 1;
    wave = 0;
    weapon = 1;
    shield = 0;
    kills = 0;
    px = 240;
    py = 200;
    scroll = 0;
    stage_scroll = 0;
    scroll_speed = SCROLL_BASE;
    invuln = 90;
    dead_timer = 0;
    combo = 0;
    combo_timer = 0;
    shake = 0;
    fire_timer = 0;
    boss_state = 0;
    boss_dying = 0;
    boss_hp = 0;
    boss_phase = 0;
    banner = 0;
    extra_life = 0;
    frames = 0;
    state = 1;
}

/* ------------------------------------------------------------------------ */
/* the update                                                               */
/* ------------------------------------------------------------------------ */

static void update_fx(void)
{
    int i;
    for (i = 0; i < BOOM_MAX; i++) {
        if (booms[i].alive) {
            booms[i].t++;
            if (booms[i].t > 16)
                booms[i].alive = 0;
        }
    }
    for (i = 0; i < SPARK_MAX; i++) {
        Spark *s = &sparks[i];
        if (!s->alive)
            continue;
        s->x += s->vx;
        s->y += s->vy;
        s->life--;
        if (s->life <= 0)
            s->alive = 0;
    }
    for (i = 0; i < POPUP_MAX; i++) {
        Popup *p = &popups[i];
        if (!p->alive)
            continue;
        p->y -= 1;
        p->t--;
        if (p->t <= 0)
            p->alive = 0;
    }
}

static void update(void)
{
    unsigned pad = arcade1_pad();
    int i;

    frames++;

    if (state == 0) {
        /* the title: START flies */
        title_t++;
        update_fx();
        if (pressed(pad, BTN_START)) {
            reset_game();
            sfx(660, 990, 12);
        }
        prev_pad = pad;
        return;
    }

    if (state == 3) {
        /* game over: START restarts */
        update_fx();
        if (pressed(pad, BTN_START)) {
            reset_game();
            sfx(660, 990, 12);
        }
        prev_pad = pad;
        return;
    }

    if (state == 2) {
        /* the death pause: the world keeps coming */
        update_fx();
        scroll += scroll_speed;
        if (--dead_timer <= 0) {
            if (lives <= 0) {
                state = 3;
            } else {
                px = 240;
                py = 190;
                invuln = 150;
                state = 1;
            }
        }
        prev_pad = pad;
        return;
    }

    /* --- the world scrolls ------------------------------------------------ */
    scroll += scroll_speed;
    if (banner > 0)
        banner--;

    /* --- the waves -------------------------------------------------------- */
    if (boss_state == 0) {
        int rel = scroll - stage_scroll;
        int first = stage_first();
        int n = stage_count();
        if (rel >= BOSS_SCROLL) {
            /* the boss comes when the stage says so, and the waves stop
             * for it */
            start_boss();
        } else if (wave < n && rel >= wave_table[first + wave].scroll) {
            spawn_wave(&wave_table[first + wave]);
            wave++;
        }
    }

    /* --- the player ------------------------------------------------------- */
    if (invuln > 0)
        invuln--;
    if (shield > 0)
        shield--;
    if (combo_timer > 0) {
        combo_timer--;
        if (combo_timer == 0)
            combo = 0;
    }
    if (shake > 0)
        shake--;

    {
        int sp = (pad & BTN_B) ? P_FOCUS : P_SPEED;
        int dx = 0, dy = 0;
        if (pad & BTN_LEFT)
            dx = -1;
        if (pad & BTN_RIGHT)
            dx = 1;
        if (pad & BTN_UP)
            dy = -1;
        if (pad & BTN_DOWN)
            dy = 1;
        px += dx * sp;
        py += dy * sp;
        if (px < 14)
            px = 14;
        if (px > SCREEN_W - 14)
            px = SCREEN_W - 14;
        if (py < P_TOP)
            py = P_TOP;
        if (py > P_BOTTOM)
            py = P_BOTTOM;
    }

    /* the shot */
    if (fire_timer > 0)
        fire_timer--;
    if ((pad & BTN_A) && fire_timer == 0) {
        fire_player();
        fire_timer = (pad & BTN_B) ? 4 : P_FIRE;
    }

    /* --- the enemies ------------------------------------------------------ */
    enemies = 0;
    for (i = 0; i < ENEMY_MAX; i++) {
        Enemy *e = &foes[i];
        int bx, by, bw, bh;
        if (!e->alive)
            continue;
        e->t++;

        switch (e->type) {
        case E_FIGHTER:
            e->y += 2 + scroll_speed / 2;
            e->x = e->anchor + (((e->t / 40) % 2) ? 12 : -12);
            break;
        case E_DRONE:
            /* the drones dive in a straight line — they must be dodgeable,
             * not homing */
            e->x += e->vx;
            e->y += 2 + scroll_speed / 2;
            if (e->x < 4)
                e->vx = 1;
            if (e->x > SCREEN_W - 16)
                e->vx = -1;
            break;
        case E_COPTER:
            e->x += e->vx;
            if (e->t < 70)
                e->y += 1;
            if (e->x < -40) {
                e->x = -40;
                e->vx = 2;
            }
            if (e->x > SCREEN_W - 32) {
                e->x = SCREEN_W - 32;
                e->vx = -2;
            }
            break;
        case E_BOMBER:
            e->y += 1;
            e->x = e->anchor + (((e->t / 90) % 2) ? 16 : -16);
            break;
        case E_CRUISER:
            e->y += 1;
            e->x = e->anchor + (((e->t / 120) % 2) ? 30 : -30);
            break;
        case E_TURRET:
        case E_LAUNCHER:
            /* they sit on the ground line */
            break;
        case E_TANK:
            /* the tank drives along it */
            e->x += e->vx;
            if (e->x < -30) {
                e->x = SCREEN_W;
                e->vx = -1;
            }
            if (e->x > SCREEN_W) {
                e->x = -30;
                e->vx = 1;
            }
            break;
        case E_DEBRIS:
            e->y += 2 + scroll_speed / 2;
            e->x += (((e->t / 50) % 2) ? 1 : -1);
            break;
        default:
            break;
        }

        /* the enemy's shooting */
        if (e->fire > 0) {
            e->fire--;
        } else {
            switch (e->type) {
            case E_FIGHTER:
                fire_aimed(e->x + 8, e->y + 14, 2);
                e->fire = 150 - stage * 10;
                break;
            case E_COPTER:
                fire_aimed(e->x + 16, e->y + 20, 3);
                e->fire = 70 - stage * 5;
                break;
            case E_BOMBER:
                spawn_bullet(e->x + 24, e->y + 30, 0, 2, B_FOE);
                e->fire = 80 - stage * 6;
                break;
            case E_CRUISER:
                fire_fan(e->x + 32, e->y + 38, 3, 2, 2);
                e->fire = 90 - stage * 6;
                break;
            case E_TURRET:
                spawn_bullet(e->x + 8, e->y, 0, -3, B_FOE);
                e->fire = 90 - stage * 6;
                break;
            case E_TANK:
                spawn_bullet(e->x + 12, e->y, 0, -3, B_FOE);
                e->fire = 110 - stage * 8;
                break;
            case E_LAUNCHER:
                spawn_bullet(e->x + 12, e->y, 0, -1, B_MISSILE);
                e->fire = 120 - stage * 8;
                break;
            default:
                break;
            }
        }

        /* off the screen */
        if (e->y > SCREEN_H + 40 || e->y < -140 || e->x < -90 ||
            e->x > SCREEN_W + 90) {
            e->alive = 0;
            continue;
        }
        enemies++;

        /* ramming the player */
        foe_box(e->type, &bx, &by, &bw, &bh);
        if (invuln == 0 && shield == 0 &&
            overlap(px - P_HITBOX / 2, py - P_HITBOX / 2, P_HITBOX, P_HITBOX,
                    e->x + bx, e->y + by, bw, bh)) {
            kill_foe(e);
            hurt_player();
            continue;
        }
    }

    /* --- the boss --------------------------------------------------------- */
    if (boss_state) {
        update_boss();
        if (boss_state == 2) {
            int core_x = boss_x + BOSS_W / 2 - 8;
            int core_y = boss_y + BOSS_H / 2 - 8;
            int bx = boss_x + 8, by = boss_y + 8;
            int bw = BOSS_W - 16, bh = BOSS_H - 16;
            for (i = 0; i < BULLET_MAX; i++) {
                Bullet *b = &bullets[i];
                if (!b->alive || b->kind != B_PLAYER)
                    continue;
                if (overlap(b->x - 2, b->y - 6, 4, 12,
                            core_x, core_y, 16, 16)) {
                    boss_hp -= 6;
                    b->alive = 0;
                    spawn_sparks(core_x + 8, core_y + 8, 3);
                    sfx(900, 1400, 4);
                } else if (overlap(b->x - 2, b->y - 6, 4, 12,
                                   bx, by, bw, bh)) {
                    boss_hp -= 2;
                    b->alive = 0;
                    spawn_sparks(b->x, b->y, 2);
                }
            }
            if (boss_hp <= 0) {
                boss_state = 3;
                boss_dying = 90;
                add_score(10000);
                sfx(400, 40, 60);
                shake = 20;
            }
        } else if (boss_state == 3) {
            boss_dying--;
            if (boss_dying % 6 == 0) {
                spawn_boom(boss_x + arcade1_rand(BOSS_W - 20),
                           boss_y + arcade1_rand(BOSS_H - 20));
                sfx(300, 80, 10);
            }
            if (boss_dying <= 0) {
                boss_state = 0;
                boss_hp = 0;
                stage++;
                wave = 0;
                stage_scroll = scroll;
                scroll_speed = SCROLL_BASE + (stage - 1);
                add_score(5000);
                banner = 100;
                banner_msg = 1;
            }
        }
    }

    /* --- the shots -------------------------------------------------------- */
    for (i = 0; i < BULLET_MAX; i++) {
        Bullet *b = &bullets[i];
        int bx, by, bw, bh;
        if (!b->alive)
            continue;
        if (b->kind == B_MISSILE) {
            /* the missiles curve toward the player */
            int dx = px - b->x, dy = py - b->y;
            int ax = dx < 0 ? -dx : dx;
            int ay = dy < 0 ? -dy : dy;
            int m = ax > ay ? ax : ay;
            if (m > 0) {
                b->vx += (((dx * 2) / m) - b->vx) / 8;
                b->vy += (((dy * 2) / m) - b->vy) / 8;
            }
        }
        b->x += b->vx;
        b->y += b->vy;
        if (b->y < -20 || b->y > SCREEN_H + 20 || b->x < -20 ||
            b->x > SCREEN_W + 20) {
            b->alive = 0;
            continue;
        }

        if (b->kind == B_PLAYER) {
            /* the player's shot against the enemies */
            int j;
            for (j = 0; j < ENEMY_MAX; j++) {
                Enemy *e = &foes[j];
                if (!e->alive)
                    continue;
                foe_box(e->type, &bx, &by, &bw, &bh);
                if (overlap(b->x - 2, b->y - 6, 4, 12,
                            e->x + bx, e->y + by, bw, bh)) {
                    e->hp--;
                    b->alive = 0;
                    spawn_sparks(b->x, b->y, 2);
                    if (e->hp <= 0)
                        kill_foe(e);
                    break;
                }
            }
        } else {
            /* the enemy's shot against the player */
            if (invuln == 0 && shield == 0 &&
                overlap(b->x - 2, b->y - 2, 4, 4,
                        px - P_HITBOX / 2, py - P_HITBOX / 2,
                        P_HITBOX, P_HITBOX)) {
                b->alive = 0;
                hurt_player();
            }
        }
    }

    /* --- the pickups ------------------------------------------------------ */
    for (i = 0; i < PICKUP_MAX; i++) {
        Pickup *k = &pickups[i];
        if (!k->alive)
            continue;
        k->y += k->vy;
        if (k->y > SCREEN_H + 20) {
            k->alive = 0;
            continue;
        }
        if (overlap(k->x - 5, k->y - 5, 10, 10,
                    px - P_HITBOX / 2, py - P_HITBOX / 2, P_HITBOX, P_HITBOX)) {
            k->alive = 0;
            if (k->kind == 0) {
                if (weapon < 4)
                    weapon++;
                sfx(700, 1400, 16);
            } else {
                shield = 300;
                sfx(500, 1000, 20);
            }
            add_score(50);
        }
    }

    update_fx();
    prev_pad = pad;
}

/* ------------------------------------------------------------------------ */
/* the drawing                                                              */
/* ------------------------------------------------------------------------ */

static void draw_background(void)
{
    int i;

    arcade1_clear(arcade1_rgb(0x08, 0x0C, 0x18));

    /* the stars: two layers, scrolling down at different rates */
    for (i = 0; i < STAR_COUNT; i++) {
        int rate = (i & 1) ? 2 : 1;
        int y = (star_y[i] + (int)((scroll * rate) & 0xFFFF) % 270) % 270;
        unsigned c = (i & 1) ? arcade1_rgb(0xC0, 0xD0, 0xE0)
                             : arcade1_rgb(0x60, 0x78, 0x98);
        arcade1_pixel(star_x[i], y, c);
    }

    /* the terrain band at the bottom: a landscape, not a scrolling texture */
    asset_sprite4(&ground_frames[0], 0, GROUND_TOP, 0);
}

static void draw_foes(void)
{
    int i;
    for (i = 0; i < ENEMY_MAX; i++) {
        Enemy *e = &foes[i];
        if (!e->alive)
            continue;
        switch (e->type) {
        case E_FIGHTER:  asset_sprite4(&fighter_frames[0], e->x, e->y, 0); break;
        case E_DRONE:    asset_sprite4(&drone_frames[0], e->x, e->y, 0); break;
        case E_COPTER:   asset_sprite4(&copter_frames[0], e->x, e->y, 0); break;
        case E_BOMBER:   asset_sprite4(&bomber_frames[0], e->x, e->y, 0); break;
        case E_CRUISER:  asset_sprite4(&cruiser_frames[0], e->x, e->y, 0); break;
        case E_TURRET:   asset_sprite4(&turret_frames[0], e->x, e->y, 0); break;
        case E_TANK:     asset_sprite4(&tank_frames[0], e->x, e->y, 0); break;
        case E_LAUNCHER: asset_sprite4(&launcher_frames[0], e->x, e->y, 0); break;
        case E_DEBRIS:   asset_sprite4(&debris_frames[0], e->x, e->y, 0); break;
        default: break;
        }
    }
}

static void draw_boss(void)
{
    if (!boss_state)
        return;
    if (boss_state == 3) {
        /* the death: a wreck that flickers */
        if ((boss_dying / 4) & 1)
            asset_sprite4(&boss_frames[1], boss_x, boss_y, 0);
        else
            asset_sprite4(&boss_frames[0], boss_x, boss_y, 0);
        return;
    }
    if (boss_state == 1) {
        asset_sprite4(&boss_frames[0], boss_x, boss_y, 0);
        return;
    }
    /* the core: the weak point, pulsing */
    asset_sprite4(&boss_frames[(boss_hp * 3 < boss_hp_max) ? 1 : 0],
                  boss_x, boss_y, 0);
    asset_sprite4(&boss_core_frames[(frames / 8) & 1],
                  boss_x + BOSS_W / 2 - 8, boss_y + BOSS_H / 2 - 8, 0);
}

static void draw_bullets(void)
{
    int i;
    for (i = 0; i < BULLET_MAX; i++) {
        Bullet *b = &bullets[i];
        if (!b->alive)
            continue;
        if (b->kind == B_PLAYER)
            asset_sprite4(&pshot_frames[0], b->x - 3, b->y - 6, 0);
        else if (b->kind == B_MISSILE)
            asset_sprite4(&missile_frames[0], b->x - 4, b->y - 4, 0);
        else
            asset_sprite4(&fshot_frames[0], b->x - 3, b->y - 3, 0);
    }
}

static void draw_fx(void)
{
    int i;
    for (i = 0; i < BOOM_MAX; i++) {
        Boom *b = &booms[i];
        int f;
        if (!b->alive)
            continue;
        f = b->t / 4;
        if (f > 3)
            f = 3;
        asset_sprite4(&boom_frames[f], b->x - 12, b->y - 12, 0);
    }
    for (i = 0; i < SPARK_MAX; i++) {
        Spark *s = &sparks[i];
        if (!s->alive)
            continue;
        arcade1_rect(s->x, s->y, 2, 2, arcade1_rgb(0xFF, 0xE0, 0x90));
    }
    for (i = 0; i < PICKUP_MAX; i++) {
        Pickup *k = &pickups[i];
        if (!k->alive)
            continue;
        if (k->kind == 0)
            asset_sprite4(&pickup_power_frames[0], k->x - 6, k->y - 6, 0);
        else
            asset_sprite4(&pickup_shield_frames[0], k->x - 6, k->y - 6, 0);
    }
    for (i = 0; i < POPUP_MAX; i++) {
        Popup *p = &popups[i];
        if (!p->alive)
            continue;
        text_u32(p->x, p->y, (unsigned)p->value, 4,
                 arcade1_rgb(0xFF, 0xE0, 0x60));
    }
}

static void draw_player(void)
{
    unsigned focus = arcade1_pad() & BTN_B;
    if (state == 2)
        return;
    if (invuln > 0 && (frames / 3) & 1)
        return;                     /* the blink */
    if (shield > 0) {
        /* the shield ring */
        unsigned c = arcade1_rgb(0x60, 0xA0, 0xFF);
        arcade1_rect(px - 13, py - 12, 26, 2, c);
        arcade1_rect(px - 13, py + 11, 26, 2, c);
        arcade1_rect(px - 14, py - 10, 2, 22, c);
        arcade1_rect(px + 12, py - 10, 2, 22, c);
    }
    asset_sprite4(&player_frames[(frames / 6) & 1], px - 12, py - 12, 0);
    if (focus) {
        /* the focus shows the hitbox: the classic bullet-hell bargain */
        arcade1_rect(px - 2, py - 2, 4, 4, 0xFFFF);
    }
}

static void draw_hud(void)
{
    int i;
    arcade1_cam(0, 0);
    /* the HUD strip */
    arcade1_rect(0, 0, SCREEN_W, HUD_H - 1, arcade1_rgb(0x0C, 0x10, 0x20));
    arcade1_rect(0, HUD_H - 1, SCREEN_W, 1, arcade1_rgb(0x2A, 0x38, 0x60));

    text_draw(6, 2, "SCORE", arcade1_rgb(0x80, 0xC0, 0xFF));
    text_u32(46, 2, (unsigned)score, 7, 0xFFFF);
    text_draw(SCREEN_W - 100, 2, "HI", arcade1_rgb(0xFF, 0xC0, 0x60));
    text_u32(SCREEN_W - 82, 2, (unsigned)hi_score, 7, 0xFFFF);

    /* the lives: a tiny ship each */
    text_draw(6, 11, "LIFE", arcade1_rgb(0x80, 0xC0, 0xFF));
    for (i = 0; i < lives && i < 5; i++) {
        int x = 42 + i * 10;
        arcade1_rect(x + 3, 11, 3, 1, arcade1_rgb(0x70, 0xE0, 0xF0));
        arcade1_rect(x + 2, 12, 5, 1, arcade1_rgb(0x70, 0xE0, 0xF0));
        arcade1_rect(x + 1, 13, 7, 1, arcade1_rgb(0x40, 0x90, 0xC0));
        arcade1_rect(x + 3, 14, 3, 1, arcade1_rgb(0xFF, 0xA0, 0x40));
    }
    /* the weapon level: the pips */
    text_draw(120, 11, "PWR", arcade1_rgb(0x80, 0xC0, 0xFF));
    for (i = 0; i < 4; i++) {
        if (i < weapon)
            arcade1_rect(146 + i * 7, 12, 5, 4, arcade1_rgb(0xFF, 0xD0, 0x60));
        else
            arcade1_rect(146 + i * 7, 12, 5, 4, arcade1_rgb(0x30, 0x38, 0x50));
    }
    text_draw(190, 11, "STAGE", arcade1_rgb(0x80, 0xC0, 0xFF));
    text_u32(230, 11, (unsigned)stage, 2, 0xFFFF);
    /* the combo multiplier: it shows while the combo is alive */
    if (combo > 0 && combo_timer > 0) {
        int mult = 1 + combo / 4;
        if (mult > 8)
            mult = 8;
        text_draw(270, 11, "COMBO", arcade1_rgb(0xFF, 0xC0, 0x60));
        text_u32(320, 11, (unsigned)mult, 1, arcade1_rgb(0xFF, 0xE0, 0x90));
    }

    /* the boss's health bar */
    if (boss_state == 2 && boss_hp > 0) {
        int w = (boss_hp * 200) / boss_hp_max;
        arcade1_rect(140, 16, 200, 3, arcade1_rgb(0x20, 0x20, 0x30));
        arcade1_rect(140, 16, w, 3, arcade1_rgb(0xE0, 0x40, 0x50));
    }

    /* the banners */
    if (banner > 0) {
        if (banner_msg == 0)
            text_draw(170, 110, "WARNING", arcade1_rgb(0xFF, 0x40, 0x40));
        else
            text_draw(170, 110, "STAGE CLEAR", arcade1_rgb(0x60, 0xFF, 0x90));
    }

    if (state == 0) {
        /* the title */
        arcade1_rect(0, 90, SCREEN_W, 60, arcade1_rgb(0x08, 0x0C, 0x18));
        text_draw(168, 100, "TEMPEST", arcade1_rgb(0x70, 0xE0, 0xF0));
        text_draw(160, 118, "VERTICAL ASSAULT", arcade1_rgb(0xC0, 0xC0, 0xFF));
        if ((title_t / 30) & 1)
            text_draw(148, 134, "PRESS START", arcade1_rgb(0xFF, 0xD0, 0x60));
    } else if (state == 3) {
        arcade1_rect(0, 100, SCREEN_W, 48, arcade1_rgb(0x08, 0x0C, 0x18));
        text_draw(180, 108, "GAME OVER", arcade1_rgb(0xFF, 0x50, 0x50));
        text_u32(150, 124, (unsigned)score, 7, 0xFFFF);
        if ((frames / 30) & 1)
            text_draw(160, 136, "PRESS START", arcade1_rgb(0xFF, 0xD0, 0x60));
    }
}

int main(void)
{
    /* the palettes */
    gfx_pal_upload(0 * 16, hero_palette, 16);
    gfx_pal_upload(1 * 16, foe_palette, 16);
    gfx_pal_upload(2 * 16, ground_palette, 16);
    gfx_pal_upload(3 * 16, shot_palette, 16);
    gfx_pal_upload(4 * 16, fx_palette, 16);
    gfx_pal_upload(5 * 16, rock_palette, 16);
    gfx_pal_upload(6 * 16, boss_palette, 16);
    gfx_pal_upload(7 * 16, sky_palette, 16);

    state = 0;
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
        draw_foes();
        draw_boss();
        draw_bullets();
        draw_player();
        draw_fx();
        draw_hud();
        arcade1_present();
    }
    return 0;
}
