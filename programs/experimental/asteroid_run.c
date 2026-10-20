/*
 * ARCADE-1: ASTEROID RUN — Game #1 through the full asset flow.
 *
 * This is the ROADMAP_V3 Wave 5 finish of the original rectangle-only
 * example (the xorshift RNG, the entity arrays, the overlap() tests, the
 * fire-on-held-cadence and the START restart are all its structure). What
 * the finish adds, every piece through the shipped pipeline:
 *
 *   - art       Wave 1: tools/arc1_assetc.py compiles the PLACEHOLDER
 *               sheets (tools/gen_game1_art.py — owner supplied no
 *               designer art for Game #1) into asteroid_run_sheet.h
 *   - animation Wave 3: the player has three states — idle, boost, fire —
 *               and the asteroids a two-frame spin, played from the
 *               emitted tables with anim_tick(), timed by the frame
 *               timer (arcade1_timer_count)
 *   - level     Wave 4: the scrolling background is an imported 30x40
 *               tilemap (asteroid_run_level.txt, incl. the nebula's
 *               flipX twin); the camera runs 0..370 px and reaching the
 *               end WINS the run
 *   - HUD       text.h: SCORE, LIVES and DIST, plus the terminal overlay
 *   - enemies   TWO types: asteroids (spin, slow, +100) and comets
 *               (fast straight falls, +300)
 *
 * Full state lives in named globals the CLI can read back
 * (--vars score,lives,state,scroll,pstate): state 0 = playing,
 * 1 = game over, 2 = level clear; START restarts from either terminal
 * state. tests/test_asteroid_run.sh pins the whole narrative through a
 * committed input script, and tests/golden/asteroid_run.txt locks the
 * frames (golden game 10).
 *
 * Controls:
 *   LEFT / RIGHT  - move (boost animation)
 *   A             - fire (cadence every 8 frames while held)
 *   START         - restart after game over or level clear
 */
#include "asset.h"                /* sprite4_ref, asset_sprite4, anim_tick */
#include "text.h"                 /* HUD font */
#include "asteroid_run_sheet.h"   /* the assetc-compiled art + level */

#define SCREEN_W 480
#define SCREEN_H 270

/* RGB565 colors (direct-draw overlays and bullets; sprites go through
 * the game1 palette bank the refs carry). */
#define WHITE     0xFFFF
#define RED       0xF800
#define GREEN     0x07E0
#define BLUE      0x001F
#define CYAN      0x07FF
#define YELLOW    0xFFE0
#define ORANGE    0xFD20
#define BLACK     0x0000
#define SPACE_BG  ((2u << 11) | (6u << 5) | 14u)   /* deep space navy */

#define MAX_ASTEROIDS 12
#define MAX_BULLETS    8

/* The level is 30x40 tiles = 480x640; the view is 270 tall, so the
 * camera can travel 370 px. Reaching the end is the WIN. */
#define LEVEL_END (LEVEL_ROWS * LEVEL_TILE_H - SCREEN_H)

#define STATE_PLAYING 0
#define STATE_OVER    1
#define STATE_WON     2

#define PLAYER_Y      (SCREEN_H - 32)
#define FIRE_COOLDOWN 10         /* frames the fire pose is held after a shot */

typedef struct {
    int x;
    int y;
    int speed;
    int type;                    /* 0 = asteroid (+100), 1 = comet (+300) */
    int drift;                   /* comet: horizontal drift toward the ship */
    int active;
} Asteroid;

typedef struct {
    int x;
    int y;
    int speed;
    int active;
} Bullet;

static Asteroid asteroids[MAX_ASTEROIDS];
static Bullet bullets[MAX_BULLETS];

static int player_x;
static int player_y;
static int pstate;               /* 0 idle, 1 boost, 2 fire (Wave 3 states) */
static int pprev;                /* the previous frame's state, for resets */
static int fire_timer;           /* frames left in the fire pose */

static int score;
static int lives;
static int state;                /* STATE_* */
static int scroll;               /* camera travel: the level progress */
static int frame;

static unsigned int rng_state;
static unsigned int g_now;       /* arcade1_timer_count(), once per frame */

/* Animation state per table (Wave 3): the player's three states and one
 * spin per asteroid slot. ANIM() is an INITIALIZER, not decoration: a
 * zeroed anim_state has ms = 0 — before the SDK hardening that spun
 * anim_tick()'s catch-up loop forever (found the hard way — the guest
 * hung inside frame 0); anim_tick now holds such a state on its current
 * step instead. The per-slot spin states are all listed because a
 * struct-copy assignment would make the freestanding build emit memcpy
 * (crt0 now provides it; the explicit list is kept regardless). */
static anim_state p_idle   = ANIM(idle_frames, IDLE_MS, ANIM_LOOP);
static anim_state p_boost  = ANIM(boost_frames, BOOST_MS, ANIM_LOOP);
static anim_state p_fire   = ANIM(fire_frames, FIRE_MS, ANIM_LOOP);
static anim_state foe_spin[MAX_ASTEROIDS] = {
    ANIM(spin_frames, SPIN_MS, ANIM_LOOP),
    ANIM(spin_frames, SPIN_MS, ANIM_LOOP),
    ANIM(spin_frames, SPIN_MS, ANIM_LOOP),
    ANIM(spin_frames, SPIN_MS, ANIM_LOOP),
    ANIM(spin_frames, SPIN_MS, ANIM_LOOP),
    ANIM(spin_frames, SPIN_MS, ANIM_LOOP),
    ANIM(spin_frames, SPIN_MS, ANIM_LOOP),
    ANIM(spin_frames, SPIN_MS, ANIM_LOOP),
    ANIM(spin_frames, SPIN_MS, ANIM_LOOP),
    ANIM(spin_frames, SPIN_MS, ANIM_LOOP),
    ANIM(spin_frames, SPIN_MS, ANIM_LOOP),
    ANIM(spin_frames, SPIN_MS, ANIM_LOOP)
};

/* ------------------------------------------------------------------------- */
/* Deterministic pseudo-random generator (the original's)                     */
/* ------------------------------------------------------------------------- */

static unsigned int random_u32(void)
{
    unsigned int x = rng_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    rng_state = x;
    return x;
}

static int random_range(int max)
{
    if (max <= 0)
        return 0;

    return (int)(random_u32() % (unsigned int)max);
}

/* ------------------------------------------------------------------------- */
/* Collision (the original's, fed by the manifest's metadata where it exists) */
/* ------------------------------------------------------------------------- */

static int overlap(
    int ax, int ay, int aw, int ah,
    int bx, int by, int bw, int bh)
{
    if (ax + aw <= bx)
        return 0;

    if (bx + bw <= ax)
        return 0;

    if (ay + ah <= by)
        return 0;

    if (by + bh <= ay)
        return 0;

    return 1;
}

/* The player's collision box, read from the manifest through the ref —
 * asset.h metadata in a real game (Wave 2's typed accessors). */
static int player_hit(int ax, int ay, int aw, int ah)
{
    return overlap(player_x + player_frames[0].cx,
                   player_y + player_frames[0].cy,
                   player_frames[0].cw,
                   player_frames[0].ch,
                   ax, ay, aw, ah);
}

/* ------------------------------------------------------------------------- */
/* Spawning                                                                   */
/* ------------------------------------------------------------------------- */

/* Park this slot at row `y` (negative = above the screen) at a fresh
 * position: one slot in four spawns a comet — the second enemy type. */
static void respawn_asteroid(Asteroid *a, int y)
{
    /* Comets are AIMED: the spawn offset is computed so that a ship
     * holding its column meets the comet at ship height, and the drift
     * walks the comet onto that column on the way down. Sitting still
     * dies; the sweep (4 px/frame vs the comet's 1) is the counterplay.
     * Asteroids fall anywhere, as the original's did. */
    a->type = random_range(4) == 0 ? 1 : 0;
    a->speed = a->type ? 4 + random_range(2) : 1 + random_range(2);
    a->y = y;
    if (a->type) {
        int lead = (player_y - a->y) / a->speed + 1;   /* frames to ship row */
        int dir = random_range(2) ? 1 : -1;

        a->drift = dir;
        a->x = player_x - dir * lead;
    } else {
        a->x = random_range(SCREEN_W - 16);
        a->drift = 0;
    }
    if (a->x < 0)
        a->x = 0;
    if (a->x > SCREEN_W - 16)
        a->x = SCREEN_W - 16;
    a->active = 1;
    anim_reset(&foe_spin[a - asteroids], g_now);
}

/* ------------------------------------------------------------------------- */
/* Game initialization                                                        */
/* ------------------------------------------------------------------------- */

static void reset_game(void)
{
    int i;

    rng_state = 0x1A2B3C4Du;

    player_x = SCREEN_W / 2 - 8;
    player_y = PLAYER_Y;

    score = 0;
    lives = 3;
    state = STATE_PLAYING;
    scroll = 0;
    frame = 0;
    pstate = pprev = 0;
    fire_timer = 0;

    /* Every table re-anchors now, so no state inherits the old carry
     * (the Wave 3 walkdemo finding). */
    anim_reset(&p_idle, g_now);
    anim_reset(&p_boost, g_now);
    anim_reset(&p_fire, g_now);

    for (i = 0; i < MAX_BULLETS; i++)
        bullets[i].active = 0;

    for (i = 0; i < MAX_ASTEROIDS; i++) {
        /* Stagger the initial field down the level, the original's way
         * (respawn also re-anchors this slot's spin animation and aims
         * comets from wherever the slot starts). */
        respawn_asteroid(&asteroids[i], -random_range(SCREEN_H));
    }
}

/* ------------------------------------------------------------------------- */
/* Bullets (the original's cadence and rects)                                 */
/* ------------------------------------------------------------------------- */

static void fire_bullet(void)
{
    int i;

    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i].x = player_x + 6;
            bullets[i].y = player_y - 6;
            bullets[i].speed = 5;
            bullets[i].active = 1;
            fire_timer = FIRE_COOLDOWN;
            return;
        }
    }
}

static void update_bullets(void)
{
    int i;

    for (i = 0; i < MAX_BULLETS; i++) {
        int j;

        if (!bullets[i].active)
            continue;

        bullets[i].y -= bullets[i].speed;

        if (bullets[i].y < -8) {
            bullets[i].active = 0;
            continue;
        }

        for (j = 0; j < MAX_ASTEROIDS; j++) {
            Asteroid *a = &asteroids[j];

            if (!a->active)
                continue;

            if (overlap(
                    bullets[i].x,
                    bullets[i].y,
                    3,
                    8,
                    a->x + foe_frames[0].cx,
                    a->y + foe_frames[0].cy,
                    foe_frames[0].cw,
                    foe_frames[0].ch)) {

                bullets[i].active = 0;

                /* Comets are worth more (the second type pays off). */
                score += a->type ? 300 : 100;

                respawn_asteroid(a, -16 - random_range(80));
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Enemies                                                                    */
/* ------------------------------------------------------------------------- */

static void update_enemies(void)
{
    int i;

    for (i = 0; i < MAX_ASTEROIDS; i++) {
        Asteroid *a = &asteroids[i];

        if (!a->active)
            continue;

        a->y += a->speed;
        if (a->drift) {
            a->x += a->drift;
            if (a->x < 0)
                a->x = 0;
            if (a->x > SCREEN_W - 16)
                a->x = SCREEN_W - 16;
        }

        /* Missed: recycle above the screen (the original's). */
        if (a->y > SCREEN_H) {
            respawn_asteroid(a, -16);
            continue;
        }

        /* Player collision (only while the player can be hit). */
        if (state == STATE_PLAYING &&
            player_hit(a->x + foe_frames[0].cx,
                       a->y + foe_frames[0].cy,
                       foe_frames[0].cw,
                       foe_frames[0].ch)) {

            lives--;

            respawn_asteroid(a, -16 - random_range(80));

            if (lives <= 0) {
                lives = 0;
                state = STATE_OVER;
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Player                                                                     */
/* ------------------------------------------------------------------------- */

static void update_player(unsigned int pad)
{
    int moving = 0;

    if (pad & BTN_LEFT) {
        player_x -= 4;
        moving = 1;
    }

    if (pad & BTN_RIGHT) {
        player_x += 4;
        moving = 1;
    }

    if (player_x < 4)
        player_x = 4;

    if (player_x > SCREEN_W - 20)
        player_x = SCREEN_W - 20;

    /*
     * Fire on a simple frame cadence while A is held (the original's).
     */
    if ((pad & BTN_A) && ((frame & 7) == 0))
        fire_bullet();

    if (fire_timer > 0)
        fire_timer--;

    /* The three Wave 3 states: boost while moving, fire while the shot
     * pose is fresh, idle otherwise. */
    pstate = moving ? 1 : (fire_timer > 0 ? 2 : 0);
}

/* The player's current animation frame, from the emitted tables. */
static const sprite4_ref *player_frame(unsigned t)
{
    if (pstate != pprev) {                 /* re-anchor on every change */
        if (pstate == 1)
            anim_reset(&p_boost, t);
        else if (pstate == 2)
            anim_reset(&p_fire, t);
        else
            anim_reset(&p_idle, t);
        pprev = pstate;
    }

    if (pstate == 1) {
        /* The idle and fire tables keep re-anchoring while unused, so a
         * later visit to them starts at their first step. */
        anim_reset(&p_idle, t);
        anim_reset(&p_fire, t);
        return anim_tick(&p_boost, t);
    }
    if (pstate == 2) {
        anim_reset(&p_idle, t);
        anim_reset(&p_boost, t);
        return anim_tick(&p_fire, t);
    }
    anim_reset(&p_boost, t);
    anim_reset(&p_fire, t);
    return anim_tick(&p_idle, t);
}

/* ------------------------------------------------------------------------- */
/* Rendering                                                                  */
/* ------------------------------------------------------------------------- */

static void draw_enemies(void)
{
    int i;

    for (i = 0; i < MAX_ASTEROIDS; i++) {
        Asteroid *a = &asteroids[i];

        if (!a->active)
            continue;

        if (a->type)
            asset_sprite4(&foe_frames[2], a->x, a->y, 0);   /* comet */
        else
            asset_sprite4(anim_tick(&foe_spin[i], g_now),
                          a->x, a->y, 0);                   /* spin */
    }
}

static void draw_bullets(void)
{
    int i;

    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active)
            continue;

        gfx_rect(
            bullets[i].x,
            bullets[i].y,
            3,
            8,
            YELLOW);
    }
}

static void draw_hud(void)
{
    /* The HUD must not scroll: text glyphs are world-space, so drop the
     * camera first (programs/text.h's contract). */
    arcade1_cam(0, 0);

    text_draw(8, 4, "SCORE", WHITE);
    text_u32(8 + 6 * ARCADE1_TEXT_CELL, 4, (unsigned)score, 6, YELLOW);

    text_draw(416, 4, "LIVES", WHITE);
    text_u32(416 + 6 * ARCADE1_TEXT_CELL, 4, (unsigned)lives, 1, GREEN);

    text_draw(8, 16, "DIST", WHITE);
    text_u32(8 + 5 * ARCADE1_TEXT_CELL, 16, (unsigned)scroll, 3, CYAN);
}

static void draw_terminal(void)
{
    unsigned edge = state == STATE_WON ? CYAN : RED;

    if (state == STATE_PLAYING)
        return;

    gfx_rect(120, 96, 240, 78, BLACK);
    gfx_rect(120, 96, 240, 3, edge);
    gfx_rect(120, 171, 240, 3, edge);

    if (state == STATE_WON)
        text_draw(196, 116, "LEVEL CLEAR", WHITE);
    else
        text_draw(204, 116, "GAME OVER", WHITE);

    if ((frame & 31) < 20)
        text_draw(196, 144, "PRESS START", YELLOW);
}

static void render(const sprite4_ref *ship)
{
    arcade1_clear(SPACE_BG);

    /* World pass: the imported level under a travelling camera. */
    arcade1_cam(0, scroll);
    arcade1_tilemap4(0, 0, level_cells, level_tiles,
                     LEVEL_COLS, LEVEL_ROWS, LEVEL_TILE_W, LEVEL_TILE_H);
    arcade1_cam(0, 0);

    /* Screen-space pass: entities and HUD. */
    draw_enemies();
    draw_bullets();
    if (state != STATE_OVER)
        asset_sprite4(ship, player_x, player_y, 0);
    draw_hud();
    draw_terminal();

    arcade1_present();
}

/* ------------------------------------------------------------------------- */
/* Main                                                                       */
/* ------------------------------------------------------------------------- */

int main(void)
{
    unsigned int previous_pad = 0;
    int i;

    /* Upload the game1 palette to the bank the refs carry. */
    for (i = 0; i < 16; i++)
        arcade1_pal(player_frames[0].bank * 16 + i, game1_palette[i]);

    reset_game();

    while (1) {
        unsigned int pad = arcade1_pad();
        const sprite4_ref *ship;

        g_now = arcade1_timer_count();

        /*
         * Restart on START after game over OR level clear.
         */
        if (state != STATE_PLAYING) {
            if ((pad & BTN_START) && !(previous_pad & BTN_START))
                reset_game();

            render(player_frame(g_now));
            previous_pad = pad;
            frame++;
            continue;
        }

        update_player(pad);
        update_bullets();
        update_enemies();

        /* The run advances the camera a pixel a frame; the end of the
         * level is the win. */
        if (scroll < LEVEL_END)
            scroll++;
        if (scroll >= LEVEL_END)
            state = STATE_WON;

        ship = player_frame(g_now);
        render(ship);

        previous_pad = pad;
        frame++;
    }

    return 0;
}
