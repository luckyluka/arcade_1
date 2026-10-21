/* ARCADE-1 round 6 — "The Farmer of the Village".
 *
 * One screen: the 30x16 village map (480x256 px) IS the world — the
 * camera never moves. The farmer walks over it with the d-pad, picks up
 * the four harvests and keeps away from the slime that hunts him.
 *
 * Controls
 *   d-pad          walk (the farmer moves 3 px a frame, the slime 1)
 *   START          restart after a win or a loss
 *
 * The whole observable state is file-scope, so the tools can read it:
 *
 *   arcade1 run round6/round6.arc1 --input round6/in.txt --frames 400 \
 *       --syms round6/round6.syms --vars state,score,lives
 *
 *   state   0 = playing, 1 = won (all four harvests), 2 = lost
 *   score   the harvests collected (0..4)
 *   lives   3, 2, 1, 0
 *   farmer_x, farmer_y   the farmer's feet, in world pixels
 *   slime_x,  slime_y    the slime's feet, in world pixels
 *   pickups              the harvests still on the ground
 */

#include "asset.h"      /* sprite4_ref, asset_sprite4, map_ref, map_solid */
#include "text.h"       /* the 5x7 HUD font (opt-in) */
#include "assets.h"     /* the generated header: the art, the map, the banks */

/* --- the world --------------------------------------------------------- */

#define TILE            16
#define FARMER_SPEED     3      /* px a frame */
#define SLIME_SPEED      1      /* px a frame, every other frame */
#define SLIME_THINK     20      /* frames between two decisions */
#define STUN            45      /* frames the farmer is knocked out for */
#define START_X         (5 * TILE + 8)
#define START_Y         (12 * TILE + 8)
#define SLIME_X         (1 * TILE + 8)
#define SLIME_Y         (1 * TILE + 8)

/* the end-of-game panel */
#define SCREEN_CX       240
#define BANNER_X        136
#define BANNER_Y        40
#define BANNER_W        208
#define BANNER_H        56

/* The four harvests, in cells: a coin, an apple, a carrot, a cabbage. */
static const int harvest_cx[4] = { 13, 22, 13, 22 };
static const int harvest_cy[4] = { 8, 8, 10, 10 };
static const sprite4_ref *const harvest_ref[4] = {
    &coin_frames[0], &apple_frames[0], &carrot_frames[0], &cabbage_frames[0]
};

/* --- the observable state ---------------------------------------------- */

static int state = 0;               /* 0 = play, 1 = won, 2 = lost */
static int score = 0;               /* the harvests collected */
static int lives = 3;
static int farmer_x = START_X;      /* the farmer's feet */
static int farmer_y = START_Y;
static int slime_x = SLIME_X;       /* the slime's feet */
static int slime_y = SLIME_Y;
static int pickups = 4;             /* the harvests still on the ground */
static int frame = 0;               /* the frame counter */
static int stun = 0;                /* the frames the farmer is knocked out */
static int facing = 1;              /* 1 = right, 0 = left */
static int taken[4] = { 0, 0, 0, 0 };
static int slime_dx = 1, slime_dy = 1;

static anim_state walk = ANIM(farmer_walk_frames, FARMER_WALK_MS, ANIM_LOOP);
static anim_state hop  = ANIM(slime_hop_frames, SLIME_HOP_MS, ANIM_LOOP);

/* --- the collision layer ----------------------------------------------- */

/* Is the box (x0,y0)-(x1,y1) inside the world and free of solid cells? */
static int box_free(int x0, int y0, int x1, int y1)
{
    int tx, ty;

    if (x0 < 0 || y0 < 0 ||
        x1 >= VILLAGE_COLS * TILE || y1 >= VILLAGE_ROWS * TILE)
        return 0;                   /* outside the map is solid */
    for (ty = y0 / TILE; ty <= y1 / TILE; ty++)
        for (tx = x0 / TILE; tx <= x1 / TILE; tx++)
            if (map_solid(&village_map, tx, ty) != MAP_EMPTY)
                return 0;
    return 1;
}

/* The farmer's body, from his feet: the manifest's collision box
 * (3, 12, 10, 11) inside the 16x24 frame, whose anchor is (8, 24). */
static int farmer_free(int x, int y)
{
    return box_free(x - 5, y - 12, x + 4, y - 1);
}

/* The slime's body: the box (3, 8, 10, 6) inside the 16x16 frame. */
static int slime_free(int x, int y)
{
    return box_free(x - 5, y - 8, x + 4, y - 1);
}

static int farmer_hits_slime(void)
{
    int fx0 = farmer_x - 5, fx1 = farmer_x + 4;
    int fy0 = farmer_y - 12, fy1 = farmer_y - 1;
    int sx0 = slime_x - 5, sx1 = slime_x + 4;
    int sy0 = slime_y - 8,  sy1 = slime_y - 1;

    return fx0 <= sx1 && sx0 <= fx1 && fy0 <= sy1 && sy0 <= fy1;
}

/* --- the game ---------------------------------------------------------- */

static void reset_game(void)
{
    int i;

    state = 0;
    score = 0;
    lives = 3;
    farmer_x = START_X;
    farmer_y = START_Y;
    slime_x = SLIME_X;
    slime_y = SLIME_Y;
    pickups = 4;
    stun = 0;
    facing = 1;
    slime_dx = 1;
    slime_dy = 1;
    for (i = 0; i < 4; i++)
        taken[i] = 0;
}

static void move_farmer(unsigned p)
{
    int dx = 0, dy = 0;

    if (stun)
        return;
    if (p & BTN_LEFT) dx = -1;
    else if (p & BTN_RIGHT) dx = 1;
    else if (p & BTN_UP) dy = -1;
    else if (p & BTN_DOWN) dy = 1;
    if (!dx && !dy)
        return;

    if (dx) {
        int nx = farmer_x + dx * FARMER_SPEED;
        if (farmer_free(nx, farmer_y))
            farmer_x = nx;
        facing = (dx > 0);
    }
    if (dy) {
        int ny = farmer_y + dy * FARMER_SPEED;
        if (farmer_free(farmer_x, ny))
            farmer_y = ny;
    }
}

static void move_slime(void)
{
    int nx, ny;

    /* It hops: it moves on every other frame only. */
    if (frame & 1)
        return;

    if (frame % SLIME_THINK == 0) {
        slime_dx = (farmer_x > slime_x) - (farmer_x < slime_x);
        slime_dy = (farmer_y > slime_y) - (farmer_y < slime_y);
        if (!slime_dx && !slime_dy)
            slime_dx = 1;           /* it has arrived: keep drifting */
    }

    nx = slime_x + slime_dx * SLIME_SPEED;
    ny = slime_y + slime_dy * SLIME_SPEED;
    if (slime_free(nx, slime_y))
        slime_x = nx;
    else
        slime_dx = 0;
    if (slime_free(slime_x, ny))
        slime_y = ny;
    else
        slime_dy = 0;
}

static void check_harvest(void)
{
    int cx = farmer_x / TILE;
    int cy = farmer_y / TILE;
    int i;

    for (i = 0; i < 4; i++) {
        if (taken[i])
            continue;
        if (cx == harvest_cx[i] && cy == harvest_cy[i]) {
            taken[i] = 1;
            score++;
            pickups--;
            if (pickups == 0)
                state = 1;           /* every harvest is in the basket */
        }
    }
}

static void check_slime(void)
{
    int away_x, away_y;

    if (!farmer_hits_slime())
        return;

    lives--;
    stun = STUN;
    /* the slime is pushed back, so the farmer gets a moment */
    away_x = slime_x + (slime_x >= farmer_x ? 24 : -24);
    away_y = slime_y + (slime_y >= farmer_y ? 16 : -16);
    if (slime_free(away_x, slime_y))
        slime_x = away_x;
    else if (slime_free(slime_x, away_y))
        slime_y = away_y;
    if (lives <= 0)
        state = 2;                   /* the slime got him */
}

/* A shadow blob under a walker, so it stands on the field instead of
 * floating over it: two rows, a narrow one and a wide one. The light
 * comes from the top left, so the shadow falls to the bottom right. */
static void draw_shadow(int x, int y, int half)
{
    unsigned c = arcade1_rgb(0x1A, 0x1A, 0x22);

    arcade1_rect(x - half + 3, y - 2, half * 2, 1, c);
    arcade1_rect(x - half - 2 + 3, y - 1, half * 2 + 4, 1, c);
}

static void draw_hud(void)
{
    text_draw(8, 258, "SCORE", 0xFFFF);
    text_u32(56, 258, (unsigned)score, 1, 0xFFFF);
    text_draw(100, 258, "LIVES", 0xFFFF);
    text_u32(148, 258, (unsigned)lives, 1, 0xF800);
    text_draw(200, 258, "LEFT", 0xFFFF);
    text_u32(240, 258, (unsigned)pickups, 1, 0xFFFF);
    text_draw(300, 258, "START RESTARTS", arcade1_rgb(0xC6, 0xC3, 0xCE));
}

/* A string centred on cx: the font advances 8 px a cell. */
static void draw_centered(int cx, int y, const char *s, unsigned color)
{
    int n = 0;

    while (s[n])
        n++;
    text_draw(cx - n * 4, y, s, color);
}

static void draw_banner(const char *a, const char *b)
{
    /* a dark panel, so the words read over the field */
    arcade1_rect(BANNER_X, BANNER_Y, BANNER_W, BANNER_H,
                 arcade1_rgb(0x10, 0x10, 0x18));
    draw_centered(SCREEN_CX, BANNER_Y + 8, a, arcade1_rgb(0xFF, 0xE3, 0xA5));
    draw_centered(SCREEN_CX, BANNER_Y + 24, b, 0xFFFF);
    draw_centered(SCREEN_CX, BANNER_Y + 40, "PRESS START", 0xFFFF);
}

int main(void)
{
    unsigned p = 0;
    unsigned p_last = 0;
    int moving = 0;

    /* the four palette banks: farmer 1, slime 2, pickups 3, ground 4 */
    gfx_pal_upload(1 * 16, farmer_palette, 16);
    gfx_pal_upload(2 * 16, slime_palette, 16);
    gfx_pal_upload(3 * 16, props_palette, 16);
    gfx_pal_upload(4 * 16, ground_palette, 16);

    reset_game();

    for (;;) {                       /* one iteration = one 60 fps frame */
        unsigned t = arcade1_timer_count();
        const sprite4_ref *fref;
        int i;

        p = arcade1_pad();

        if (state != 0) {
            if ((p & BTN_START) && !(p_last & BTN_START))
                reset_game();
        } else {
            move_farmer(p);
            move_slime();
            check_harvest();
            check_slime();
        }
        if (stun)
            stun--;

        /* the farmer's walk: it plays while he moves and holds still
         * otherwise; a fresh walk starts at the contact pose */
        if (state == 0) {
            int now_moving = (p & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN))
                             && !stun;
            if (now_moving && !moving)
                anim_reset(&walk, t);
            moving = now_moving;
            fref = moving ? anim_tick(&walk, t) : &farmer_frames[0];
        } else {
            fref = &farmer_frames[0];
        }

        /* ---- draw ---- */
        arcade1_clear(0x0000);
        arcade1_tilemap4(0, 0, village_cells, village_tiles,
                         VILLAGE_COLS, VILLAGE_ROWS,
                         VILLAGE_TILE_W, VILLAGE_TILE_H);

        for (i = 0; i < 4; i++) {
            if (taken[i])
                continue;
            asset_sprite4(harvest_ref[i],
                          harvest_cx[i] * TILE + 2, harvest_cy[i] * TILE + 2, 0);
        }

        draw_shadow(slime_x, slime_y, 5);
        asset_sprite4(anim_tick(&hop, t), slime_x - slime_hop_frames[0]->ox,
                      slime_y - slime_hop_frames[0]->oy, 0);

        draw_shadow(farmer_x, farmer_y, 5);
        asset_sprite4(fref, farmer_x - fref->ox, farmer_y - fref->oy,
                      facing ? 0 : 1);

        draw_hud();
        if (state == 1)
            draw_banner("YOU WIN", "THE HARVEST IS IN");
        else if (state == 2)
            draw_banner("GAME OVER", "THE SLIME GOT HIM");

        arcade1_present();

        p_last = p;
        frame++;
    }
    return 0;
}
