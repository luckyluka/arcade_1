/* PETAL BLADE — a samurai platformer for ARCADE-1 (Game #3).
 *
 * Provenance: built in the round-5 art round (round5/, now programs/
 * petalblade_art/ for the art pipeline) and promoted to a first-class game
 * with a test, a golden and a committed ROM (ROADMAP_V4 card 4.7). The
 * code is the round-5 code; the art, the level and the generated headers
 * live in programs/petalblade_art/ and programs/petalblade_*.h.
 *
 * A Mario-like run-and-jump through a dusk valley: a samurai with a katana
 * crosses a tilemap world, fights oni and kuwagata beetles, collects coins
 * and reaches the torii gate. Everything is drawn with the typed asset API
 * (asset.h) and the assetc-compiled petalblade_assets.h; the guest is
 * freestanding C for RV32IM — no libc, deterministic (particles are driven
 * by the frame timer / a seeded LCG, never host time).
 *
 * Observable globals (read with --vars): state, score, lives, px, py, cam_x.
 *   arcade1 run petalblade.arc1 --vars state,score,lives,px,py,cam_x
 */
#include "asset.h"
#include "petalblade_assets.h"
#include "text.h"
#include "petalblade_level.h"

#define SCREEN_W 480
#define SCREEN_H 270

/* physics */
#define GRAV     1
#define JUMP_V   (-12)
#define MOVE     3
#define MAXFALL  12

/* player collision box (matches the hero sprite's "collision" metadata) */
#define PW 12
#define PH 28

/* hero sheet frames */
#define F_IDLE  0
#define F_JUMP  4
#define F_FALL  5
#define F_HURT  9
#define F_DEAD 10

#define ENEMY_MAX 8
#define COIN_MAX  16
#define PART_MAX  28

/* ---- observable game state -------------------------------------------- */
int state = 0;      /* 0 play, 1 win, 2 dead */
int score = 0;
int lives = 3;

/* ---- player ------------------------------------------------------------ */
int px = 100;       /* collision-box top-left (world) */
int py = 100;
int vx = 0, vy = 0;
int onground = 0;
int facing = 0;     /* 0 = right, 1 = left */
int slash = 0;      /* frames of slash left */
int invuln = 0;     /* invulnerability frames */
int hurt = 0;       /* hurt lock frames */
int hero_frame = F_IDLE;

/* ---- camera ------------------------------------------------------------ */
int cam_x = 0, cam_y = 0;

/* ---- entities ---------------------------------------------------------- */
typedef struct { int x, y, dir, alive, min_x, max_x; } Enemy;
static Enemy enemies[ENEMY_MAX];
static int n_enemies = 0;

typedef struct { int x, y, taken; } Coin;
static Coin coins[COIN_MAX];
static int n_coins = 0;

typedef struct { int x, y, vx, vy, life; } Part;
static Part parts[PART_MAX];
static int n_parts = 0;

static int goal_x = 120 * 16;
static int goal_y = 8 * 16 - GOAL_H;

/* ---- animation states -------------------------------------------------- */
static anim_state run_anim = ANIM(run_frames, RUN_MS, ANIM_LOOP);
static anim_state slash_anim = ANIM(heroslash_frames, HEROSLASH_MS, ANIM_ONCE);

/* ------------------------------------------------------------------------ */
/* helpers                                                                  */
/* ------------------------------------------------------------------------ */

static int solid_at(int tx, int ty)
{
    if (ty < 0 || ty >= LEVEL_ROWS)
        return 0;
    if (tx < 0 || tx >= LEVEL_COLS)
        return 1;                       /* side walls */
    return level_solid[ty * LEVEL_COLS + tx];
}

static int box_hits(int x, int y, int w, int h)
{
    int c0 = x / 16, c1 = (x + w - 1) / 16;
    int r0 = y / 16, r1 = (y + h - 1) / 16;
    int r, c;
    for (r = r0; r <= r1; r++)
        for (c = c0; c <= c1; c++)
            if (solid_at(c, r))
                return 1;
    return 0;
}

static int overlap(int ax, int ay, int aw, int ah,
                   int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void add_score(int n)
{
    score += n;
}

/* ---- particles --------------------------------------------------------- */
static void spawn_dust(int x, int y, int n)
{
    int i;
    for (i = 0; i < n && n_parts < PART_MAX; i++) {
        parts[n_parts].x = x;
        parts[n_parts].y = y;
        parts[n_parts].vx = arcade1_rand(5) - 2;
        parts[n_parts].vy = -(arcade1_rand(3) + 1);
        parts[n_parts].life = 18 + arcade1_rand(14);
        n_parts++;
    }
}

/* ---- setup ------------------------------------------------------------- */
static void setup_entities(void)
{
    int i;
    n_enemies = 0;
    for (i = 0; i < ENEMY_MAX; i++) {
        enemies[i].alive = 0;
        enemies[i].dir = 1;
        enemies[i].min_x = 0;
        enemies[i].max_x = 0;
        enemies[i].x = 0;
        enemies[i].y = 0;
    }
    /* oni patrol the flats; beetles scuttle between them */
    {
        static const int ex[6] = { 300, 620, 980, 250, 700, 1030 };
        static const int ex_lo[6] = { 220, 580, 950, 220, 640, 950 };
        static const int ex_hi[6] = { 420, 760, 1100, 360, 780, 1120 };
        for (i = 0; i < 6; i++) {
            enemies[i].x = ex[i];
            enemies[i].min_x = ex_lo[i];
            enemies[i].max_x = ex_hi[i];
            enemies[i].y = 192 - (i < 3 ? ONI_H : BEETLE_H);
            enemies[i].alive = 1;
            enemies[i].dir = (i & 1) ? 1 : -1;
        }
        n_enemies = 6;
    }

    n_coins = 0;
    {
        static const int cx[12] = { 200, 260, 320, 600, 660, 720,
                                    950, 1010, 1070, 1500, 1560, 1620 };
        static const int cy[12] = { 160, 160, 160, 160, 160, 160,
                                    160, 160, 160, 160, 160, 160 };
        for (i = 0; i < 12; i++) {
            coins[i].x = cx[i];
            coins[i].y = cy[i];
            coins[i].taken = 0;
        }
        n_coins = 12;
    }

    goal_x = 120 * 16;
    goal_y = 8 * 16 - GOAL_H;

    n_parts = 0;
}

static void reset_game(void)
{
    int i;
    px = 100;
    py = 100;
    vx = 0;
    vy = 0;
    onground = 0;
    facing = 0;
    slash = 0;
    invuln = 0;
    hurt = 0;
    hero_frame = F_IDLE;
    state = 0;
    score = 0;
    lives = 3;
    setup_entities();
    anim_reset(&run_anim, 0);
    anim_reset(&slash_anim, 0);
    (void)i;
}

/* ---- damage ------------------------------------------------------------ */
static void hurt_player(void)
{
    lives--;
    hurt = 40;
    invuln = 80;
    vx = facing ? MOVE : -MOVE;
    vy = -4;
    onground = 0;
    if (lives <= 0) {
        lives = 0;
        state = 2;
    }
}

/* ---- update ------------------------------------------------------------ */
static void update(void)
{
    unsigned pad = arcade1_pad();
    unsigned t = arcade1_timer_count();
    int i;

    if (state != 0) {
        if (pad & BTN_START)
            reset_game();
        return;
    }

    if (invuln > 0)
        invuln--;
    if (hurt > 0)
        hurt--;

    /* horizontal input */
    vx = 0;
    if (hurt == 0) {
        if (pad & BTN_LEFT) {
            vx = -MOVE;
            facing = 1;
        } else if (pad & BTN_RIGHT) {
            vx = MOVE;
            facing = 0;
        }
    }

    /* jump */
    if ((pad & BTN_A) && onground && hurt == 0) {
        vy = JUMP_V;
        onground = 0;
    }

    /* slash */
    if ((pad & BTN_B) && slash == 0 && hurt == 0) {
        slash = 20;
        anim_reset(&slash_anim, t);
    }

    /* gravity */
    vy += GRAV;
    if (vy > MAXFALL)
        vy = MAXFALL;

    /* X axis */
    px += vx;
    if (vx > 0 && box_hits(px, py, PW, PH)) {
        px = ((px + PW) / 16) * 16 - PW;
        vx = 0;
    } else if (vx < 0 && box_hits(px, py, PW, PH)) {
        px = ((px / 16) + 1) * 16;
        vx = 0;
    }

    /* Y axis */
    {
        int was = onground;
        py += vy;
        onground = 0;
        if (vy > 0 && box_hits(px, py, PW, PH)) {
            py = ((py + PH) / 16) * 16 - PH;
            vy = 0;
            onground = 1;
        } else if (vy < 0 && box_hits(px, py, PW, PH)) {
            py = ((py / 16) + 1) * 16;
            vy = 0;
        }
        if (!was && onground)
            spawn_dust(px + PW / 2, py + PH, 5);
    }

    /* pit death */
    if (py > LEVEL_ROWS * 16 + 40) {
        hurt_player();
        if (state == 0) {
            px = 100;
            py = 100;
            vx = 0;
            vy = 0;
        }
        return;
    }

    /* slash window: hit enemies in front */
    if (slash > 0) {
        int hx = facing ? px - 26 : px + PW;
        int hy = py - 12;
        slash--;
        for (i = 0; i < n_enemies; i++) {
            Enemy *e = &enemies[i];
            int ew = (e->y < 170) ? ONI_W : BEETLE_W;
            int eh = (e->y < 170) ? ONI_H : BEETLE_H;
            if (e->alive && overlap(hx, hy, 26, 52, e->x, e->y, ew, eh)) {
                e->alive = 0;
                add_score(10);
                spawn_dust(e->x + ew / 2, e->y + eh / 2, 8);
            }
        }
    }

    /* enemies patrol / chase */
    for (i = 0; i < n_enemies; i++) {
        Enemy *e = &enemies[i];
        int ew, eh, sp;
        if (!e->alive)
            continue;
        ew = (e->y < 170) ? ONI_W : BEETLE_W;
        eh = (e->y < 170) ? ONI_H : BEETLE_H;
        /* chase when the player is near and on the same level */
        if (overlap(px - 90, py - 30, PW + 180, PH + 60, e->x, e->y, ew, eh)) {
            e->dir = (px + PW / 2 < e->x + ew / 2) ? -1 : 1;
            sp = (e->y < 170) ? 2 : 2;
        } else {
            sp = (e->y < 170) ? 1 : 1;
        }
        e->x += e->dir * sp;
        if (e->x < e->min_x) { e->x = e->min_x; e->dir = 1; }
        if (e->x > e->max_x) { e->x = e->max_x; e->dir = -1; }

        /* contact damage (not while slashing through it) */
        if (invuln == 0 && slash == 0 &&
            overlap(px, py, PW, PH, e->x + 4, e->y + 4, ew - 8, eh - 4))
            hurt_player();
    }

    /* coins */
    for (i = 0; i < n_coins; i++) {
        Coin *c = &coins[i];
        if (!c->taken &&
            overlap(px, py, PW, PH, c->x, c->y, COIN_W, COIN_H)) {
            c->taken = 1;
            add_score(1);
            spawn_dust(c->x + COIN_W / 2, c->y + COIN_H / 2, 4);
        }
    }

    /* goal */
    if (overlap(px, py, PW, PH, goal_x, goal_y, GOAL_W, GOAL_H))
        state = 1;

    /* hero frame for the test / debug */
    if (state == 2)
        hero_frame = F_DEAD;
    else if (hurt > 0)
        hero_frame = F_HURT;
    else if (slash > 0)
        hero_frame = 6 + slash_anim.step;
    else if (!onground)
        hero_frame = (vy < 0) ? F_JUMP : F_FALL;
    else if (vx != 0)
        hero_frame = 1 + run_anim.step;
    else
        hero_frame = F_IDLE;

    /* particles */
    for (i = 0; i < n_parts; i++) {
        Part *p = &parts[i];
        p->x += p->vx;
        p->y += p->vy;
        p->vy += 1;
        p->life--;
    }
    for (i = 0; i < n_parts; i++) {
        if (parts[i].life <= 0) {
            parts[i] = parts[n_parts - 1];
            n_parts--;
            i--;
        }
    }

    /* camera */
    cam_x = px + PW / 2 - SCREEN_W / 2;
    if (cam_x < 0)
        cam_x = 0;
    if (cam_x > LEVEL_COLS * 16 - SCREEN_W)
        cam_x = LEVEL_COLS * 16 - SCREEN_W;
    cam_y = py + PH / 2 - SCREEN_H / 2;
    if (cam_y < 0)
        cam_y = 0;
    if (cam_y > LEVEL_ROWS * 16 - SCREEN_H)
        cam_y = LEVEL_ROWS * 16 - SCREEN_H;
}

/* ------------------------------------------------------------------------ */
/* drawing                                                                  */
/* ------------------------------------------------------------------------ */

static void upload_palettes(void)
{
    gfx_pal_upload(0, hero_palette, 16);
    gfx_pal_upload(1 * 16, oni_palette, 16);
    gfx_pal_upload(2 * 16, beetle_palette, 16);
    gfx_pal_upload(3 * 16, terrain_palette, 16);
    gfx_pal_upload(4 * 16, ridge_palette, 16);
    gfx_pal_upload(5 * 16, sky_palette, 16);
    gfx_pal_upload(6 * 16, mid_palette, 16);
    gfx_pal_upload(7 * 16, near_palette, 16);
    gfx_pal_upload(8 * 16, fx_palette, 16);
    gfx_pal_upload(9 * 16, goal_palette, 16);
}

/* A seamless W-wide strip drawn with horizontal parallax num/den. */
static void draw_layer(const sprite4_ref *fr, int num, int den, int yoff)
{
    int W = fr->w;
    int L = (cam_x * num) / den;
    int start = L % W;
    int sx;
    if (start < 0)
        start += W;
    sx = -start;
    while (sx < SCREEN_W) {
        asset_sprite4(fr, cam_x + sx, cam_y + yoff, 0);
        sx += W;
    }
}

static const unsigned sky_bands[6] = {
    ((3u << 11) | (8u << 5) | 6u),     /* deep indigo */
    ((5u << 11) | (6u << 5) | 8u),
    ((9u << 11) | (13u << 5) | 10u),
    ((15u << 11) | (14u << 5) | 10u),
    ((22u << 11) | (20u << 5) | 9u),
    ((29u << 11) | (36u << 5) | 11u),  /* warm gold */
};

static const int star_pos[14][2] = {
    {30, 22}, {80, 40}, {140, 18}, {200, 55}, {250, 30}, {300, 48},
    {350, 20}, {400, 40}, {450, 26}, {110, 70}, {230, 74}, {370, 66},
    {60, 90}, {430, 84}
};

static void draw_clouds(void)
{
    unsigned t = arcade1_timer_count();
    int i;
    static const int cy[4] = {42, 74, 30, 96};
    static const int cw[4] = {64, 88, 52, 76};
    for (i = 0; i < 4; i++) {
        int x = (i * 137 + (int)(t / 10)) % (SCREEN_W + 90) - 45;
        int y = cy[i];
        int w = cw[i];
        unsigned c = (i & 1) ? 0xEE94 : 0xDDD3;
        arcade1_rect(x, y, w, 8, c);
        arcade1_rect(x + 6, y - 5, w - 12, 6, c);
        arcade1_rect(x + 16, y - 9, w - 32, 6, c);
        arcade1_rect(x + 10, y + 6, w - 20, 5, c);
    }
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

static void draw_sky(void)
{
    int j;
    for (j = 0; j < 12; j++) {
        int seg = j * 5 / 11;
        int frac = (j * 5) % 11;
        unsigned c = (seg >= 5) ? sky_bands[5]
                                : lerp565(sky_bands[seg], sky_bands[seg + 1],
                                           frac, 11);
        arcade1_rect(0, j * 16, SCREEN_W, 16, c);
    }
    for (j = 0; j < 14; j++)
        arcade1_pixel(star_pos[j][0], star_pos[j][1], 0xFFFF);
}

static void draw_world(void)
{
    int i;
    unsigned t = arcade1_timer_count();
    const sprite4_ref *fr;

    /* moon, pinned to the sky (screen-fixed) */
    asset_sprite4(&moon_frames[0], cam_x + 376, cam_y + 34, 0);

    /* parallax background strips */
    draw_layer(&ridge_frames[0], 1, 4, 36);
    draw_layer(&mid_frames[0], 1, 2, 92);
    draw_layer(&near_frames[0], 3, 4, 140);

    /* terrain */
    arcade1_tilemap4(0, 0, level1_cells, level1_tiles,
                     LEVEL_COLS, LEVEL_ROWS, LEVEL1_TILE_W, LEVEL1_TILE_H);

    /* goal */
    asset_sprite4(&goal_frames[0], goal_x, goal_y, 0);

    /* coins */
    for (i = 0; i < n_coins; i++) {
        if (!coins[i].taken) {
            fr = &coin_frames[(t / 20) & 1];
            asset_sprite4(fr, coins[i].x, coins[i].y, 0);
        }
    }

    /* enemies */
    for (i = 0; i < n_enemies; i++) {
        Enemy *e = &enemies[i];
        int flip;
        if (!e->alive)
            continue;
        flip = (e->dir < 0) ? 1u : 0u;
        if (e->y < 170)
            asset_sprite4(&oni_frames[(t / 14) & 1], e->x, e->y, flip);
        else
            asset_sprite4(&beetle_frames[(t / 15) & 1], e->x, e->y, flip);
    }

    /* particles (dust) */
    for (i = 0; i < n_parts; i++)
        asset_sprite4(&fx_frames[2], parts[i].x, parts[i].y, 0);

    /* player */
    if (state == 2) {
        asset_sprite4(&hero_frames[F_DEAD], px - 8, py - 12, 0);
    } else if (hurt > 0) {
        asset_sprite4(&hero_frames[F_HURT], px - 8, py - 12, facing);
    } else if (slash > 0) {
        fr = anim_tick(&slash_anim, t);
        asset_sprite4(fr, px - 8, py - 12, facing);
        /* slash arc in front */
        {
            int hx = facing ? px - 22 : px + PW - 4;
            asset_sprite4(&slash_frames[slash_anim.step % SLASH_FRAMES],
                          hx, py - 8, 0);
        }
    } else if (!onground) {
        asset_sprite4(&hero_frames[(vy < 0) ? F_JUMP : F_FALL],
                      px - 8, py - 12, facing);
    } else if (vx != 0) {
        fr = anim_tick(&run_anim, t);
        asset_sprite4(fr, px - 8, py - 12, facing);
    } else {
        asset_sprite4(&hero_frames[F_IDLE], px - 8, py - 12, facing);
    }
}

static void draw_hud(void)
{
    arcade1_cam(0, 0);
    text_draw(8, 8, "SCORE", 0xFFFF);
    text_u32(8, 20, (unsigned)score, 3, 0xFFFF);
    text_draw(360, 8, "LIVES", 0xFFFF);
    text_u32(436, 20, (unsigned)lives, 1, 0xFFFF);

    if (state == 1) {
        text_draw(180, 120, "CLEAR", 0xFFFF);
        text_draw(150, 136, "START=RETRY", 0xE60B);
    } else if (state == 2) {
        text_draw(190, 120, "DEAD", 0xF800);
        text_draw(150, 136, "START=RETRY", 0xE60B);
    }
}

int main(void)
{
    upload_palettes();
    reset_game();

    for (;;) {
        unsigned t = arcade1_timer_count();
        update();
        arcade1_clear(sky_bands[0]);
        draw_sky();
        draw_clouds();
        arcade1_cam(cam_x, cam_y);
        draw_world();
        draw_hud();
        arcade1_present();
        (void)t;
    }
    return 0;
}
