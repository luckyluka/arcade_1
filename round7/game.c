/* round7/game.c — THE LIGHTHOUSE KEEPER.
 *
 * A lighthouse keeper on a cliff at dusk. The sea rises; three lamps on the
 * tower have to be lit before the water reaches the top. The world is
 * 480x1024 — four screens tall — and the camera climbs with the keeper.
 *
 * Controls
 *   UP / DOWN   climb the ladder on the tower's face
 *   A           hold at a lamp's height to light it (a hold, not a tap:
 *               the flame takes a moment to catch)
 *   START       begin another attempt
 *
 * The state the tools read is all file-scope and volatile, so `arcade1 run
 * --vars state,score,lives` can see it:
 *   state     0 = climbing, 1 = the lamps are lit (won), 2 = the sea won
 *   score     points: 100 per lamp, 500 for the beacon
 *   lives     attempts left
 *   plus keeper_y, sea_y, lamps_lit, lamp0..lamp2, charge, frame_no
 *
 * One loop iteration is one frame: read the pad, update, draw, present.
 */

#include "asset.h"      /* sprite4_ref, map_ref — before the generated header */
#include "assets.h"     /* the compiled art: sprites, maps, palettes */
#include "text.h"       /* the 5x7 HUD font (opt-in) */

/* --- the world ---------------------------------------------------------- */
#define TILE            16
#define WORLD_W         480
#define WORLD_H         1024
#define SCREEN_H        270

#define LADDER_COL      13      /* the ladder's column in the tower map */
#define KEEPER_X        216     /* the ladder's centre, in world pixels */
#define FEET_BOTTOM     736     /* the cliff's top: where the keeper starts */
#define FEET_TOP        176     /* the top of the ladder, under the gallery */

#define CLIMB_SPEED     4       /* world pixels per frame */
#define SEA_SPEED       2       /* the water's rise, per frame */
#define SEA_START       928     /* the water's line when the attempt starts */
#define CHARGE_FRAMES   48      /* frames of held A to light a lamp */

#define LAMP_X          192     /* the lamps hang on the tower's left face */
#define LAMP_ROWS       39, 31, 23   /* their heights, in tiles */

/* --- the observable state ------------------------------------------------ */
volatile int state = 0;        /* 0 climbing, 1 won, 2 the sea won */
volatile int score = 0;
volatile int lives = 3;
volatile int lamps_lit = 0;
volatile int lamp0 = 0;
volatile int lamp1 = 0;
volatile int lamp2 = 0;
volatile int keeper_y = FEET_BOTTOM;   /* the keeper's feet, world pixels */
volatile int sea_y = SEA_START;         /* the water's line, world pixels */
volatile int charge = 0;                /* frames of held A at this lamp */
volatile int frame_no = 0;

/* --- the game's own state ------------------------------------------------ */
static int cam_x = 0;
static int cam_y = 0;
static int keeper_mode = 0;    /* 0 idle, 1 climbing, 2 lighting a lamp */
static int charge_target = -1;  /* the lamp being lit, -1 for none */
static unsigned prev_pad = 0;

/* the three lamps' rows */
static const int lamp_row[3] = { LAMP_ROWS };

/* the animations the manifest compiled */
static anim_state anim_idle = ANIM(keeper_idle_anim_frames,
                                   KEEPER_IDLE_ANIM_MS, ANIM_LOOP);
static anim_state anim_climb = ANIM(keeper_climb_anim_frames,
                                     KEEPER_CLIMB_ANIM_MS, ANIM_LOOP);
static anim_state anim_carry = ANIM(keeper_carry_anim_frames,
                                     KEEPER_CARRY_ANIM_MS, ANIM_LOOP);
static anim_state anim_lamp0 = ANIM(lamp_on_anim_frames,
                                    LAMP_ON_ANIM_MS, ANIM_LOOP);
static anim_state anim_lamp1 = ANIM(lamp_on_anim_frames,
                                    LAMP_ON_ANIM_MS, ANIM_LOOP);
static anim_state anim_lamp2 = ANIM(lamp_on_anim_frames,
                                    LAMP_ON_ANIM_MS, ANIM_LOOP);

/* the HUD's colours (RGB565 — arcade1_rgb builds the word) */
#define HUD_TEXT  arcade1_rgb(0xF7, 0xE7, 0xB5)
#define HUD_DIM   arcade1_rgb(0x8C, 0x82, 0x73)
#define SEA_COL   arcade1_rgb(0x21, 0x41, 0x6B)
#define NIGHT_COL arcade1_rgb(0x18, 0x18, 0x31)

/* --- small helpers ------------------------------------------------------- */

static int lamp_get(int i)
{
    if (i == 0) return lamp0;
    if (i == 1) return lamp1;
    return lamp2;
}

static void lamp_set(int i, int v)
{
    if (i == 0) lamp0 = v;
    else if (i == 1) lamp1 = v;
    else lamp2 = v;
}

/* Is the tower cell (col, row) solid? Outside the map is solid. */
static int solid_at(int col, int row)
{
    return map_solid(&tower_map_map, col, row) != MAP_EMPTY;
}

/* May the keeper's feet stand at `y`? His body spans [y-32, y): the cell
 * under his feet and the cell over his head must both be free. */
static int body_fits(int y)
{
    int foot_row = (y - 1) / TILE;
    int head_row = (y - 32) / TILE;
    if (foot_row < 0 || head_row < 0)
        return 0;
    if (solid_at(LADDER_COL, foot_row))
        return 0;
    if (solid_at(LADDER_COL, head_row))
        return 0;
    return 1;
}

static void reset_attempt(void)
{
    keeper_y = FEET_BOTTOM;
    sea_y = SEA_START;
    lamps_lit = 0;
    lamp0 = 0;
    lamp1 = 0;
    lamp2 = 0;
    charge = 0;
    score = 0;
    keeper_mode = 0;
}

/* --- the update ---------------------------------------------------------- */

static void update(unsigned p)
{
    unsigned press = p & ~prev_pad;
    prev_pad = p;

    if (state != 0) {
        /* The round is over: START begins another attempt. When the last
         * attempt is spent, START begins a fresh round. */
        if (press & BTN_START) {
            if (lives <= 0)
                lives = 3;
            reset_attempt();
            state = 0;
        }
        return;
    }

    /* the climb */
    if (p & BTN_UP) {
        int ny = keeper_y - CLIMB_SPEED;
        if (ny >= FEET_TOP && body_fits(ny))
            keeper_y = ny;
    } else if (p & BTN_DOWN) {
        int ny = keeper_y + CLIMB_SPEED;
        if (ny <= FEET_BOTTOM && body_fits(ny))
            keeper_y = ny;
    }

    /* the lamps: hold A while level with one. The flame needs a moment to
     * catch, so the charge carries over the frames A is held. */
    {
        int target = -1;
        for (int i = 0; i < 3; i++) {
            int ly = lamp_row[i] * TILE;
            if (lamp_get(i))
                continue;
            if (keeper_y >= ly - 24 && keeper_y <= ly + 24) {
                target = i;
                break;
            }
        }
        if (target >= 0 && (p & BTN_A)) {
            if (target != charge_target) {
                charge_target = target;
                charge = 0;
            }
            charge++;
            if (charge >= CHARGE_FRAMES) {
                lamp_set(target, 1);
                lamps_lit++;
                score += 100;
                charge = 0;
                charge_target = -1;
            }
        } else {
            charge = 0;
            charge_target = -1;
        }
    }

    /* the sea */
    sea_y -= SEA_SPEED;

    /* the water takes him: the water's line has reached his feet */
    if (sea_y <= keeper_y) {
        lives--;
        state = 2;
        charge = 0;
        return;
    }

    /* all three lamps lit and the top reached: the beacon burns */
    if (lamps_lit == 3 && keeper_y <= FEET_TOP) {
        score += 500;
        state = 1;
    }

    /* the keeper's pose: lighting a lamp, climbing, or standing */
    if (charge > 0)
        keeper_mode = 2;
    else if ((p & BTN_UP) || (p & BTN_DOWN))
        keeper_mode = 1;
    else
        keeper_mode = 0;
}

/* --- the draw ------------------------------------------------------------ */

static void draw_lamp(int i, unsigned t)
{
    int ly = lamp_row[i] * TILE;
    if (lamp_get(i)) {
        const sprite4_ref *r;
        if (i == 0)      r = anim_tick(&anim_lamp0, t);
        else if (i == 1) r = anim_tick(&anim_lamp1, t);
        else             r = anim_tick(&anim_lamp2, t);
        asset_sprite4(r, LAMP_X, ly, 0);
    } else {
        asset_sprite4(&lamp_off_frames[0], LAMP_X, ly, 0);
    }
}

static void draw_hud(void)
{
    int top;

    arcade1_cam(0, 0);            /* the HUD is screen space */

    text_draw(8, 8, "LAMPS", HUD_DIM);
    text_u32(64, 8, (unsigned)lamps_lit, 1, HUD_TEXT);
    text_draw(80, 8, "/3", HUD_DIM);
    text_draw(360, 8, "SCORE", HUD_DIM);
    text_u32(416, 8, (unsigned)score, 4, HUD_TEXT);

    /* the water's level, on the right edge */
    top = SCREEN_H - ((sea_y - 128) * (SCREEN_H - 10)) / (WORLD_H - 128);
    if (top < 0) top = 0;
    arcade1_rect(468, top, 6, SCREEN_H - top, SEA_COL);

    /* the flame catching */
    if (charge > 0) {
        int sx = KEEPER_X - 12 - cam_x;
        int sy = keeper_y - 32 - 10 - cam_y;
        arcade1_rect(sx, sy, 24, 4, HUD_DIM);
        arcade1_rect(sx, sy, (24 * charge) / CHARGE_FRAMES, 4, HUD_TEXT);
    }

    if (state == 1) {
        arcade1_rect(140, 104, 200, 34, NIGHT_COL);
        text_draw(152, 110, "THE LIGHT IS LIT", HUD_TEXT);
        text_draw(168, 128, "PRESS START", HUD_DIM);
    } else if (state == 2) {
        arcade1_rect(152, 104, 176, 34, NIGHT_COL);
        text_draw(176, 110, "THE SEA WON", HUD_TEXT);
        text_draw(168, 128, "PRESS START", HUD_DIM);
    }
}

static void draw(void)
{
    unsigned t = arcade1_timer_count();
    const sprite4_ref *kr;

    arcade1_clear(NIGHT_COL);

    /* the camera: the keeper, but the water stays in view while it is near
     * and the tower's top comes into view when he nears the beacon. */
    cam_y = keeper_y - 78;
    if (keeper_y < 276)
        cam_y -= (276 - keeper_y) * 7 / 10;
    if (cam_y > sea_y - 240)
        cam_y = sea_y - 240;
    if (cam_y < 0)
        cam_y = 0;
    if (cam_y > WORLD_H - SCREEN_H)
        cam_y = WORLD_H - SCREEN_H;
    cam_x = 0;
    arcade1_cam(cam_x, cam_y);

    /* the world, layer by layer */
    arcade1_tilemap4(0, 0, sky_map_cells, sky_map_tiles,
                     SKY_MAP_COLS, SKY_MAP_ROWS, TILE, TILE);
    arcade1_tilemap4(0, 0, rock_map_cells, rock_map_tiles,
                     ROCK_MAP_COLS, ROCK_MAP_ROWS, TILE, TILE);
    arcade1_tilemap4(0, 0, tower_map_cells, tower_map_tiles,
                     TOWER_MAP_COLS, TOWER_MAP_ROWS, TILE, TILE);

    /* the sea: the surface band, then the deep water under it */
    arcade1_tilemap4(0, sea_y, sea_map_cells, sea_map_tiles,
                     SEA_MAP_COLS, SEA_MAP_ROWS, TILE, TILE);
    arcade1_tilemap4(0, sea_y + 8 * TILE, sea_deep_map_cells,
                     sea_deep_map_tiles,
                     SEA_DEEP_MAP_COLS, SEA_DEEP_MAP_ROWS, TILE, TILE);

    /* the lamps */
    for (int i = 0; i < 3; i++)
        draw_lamp(i, t);

    /* the beacon in the lantern room, once the round is won */
    if (state == 1)
        asset_sprite4(&lamp_on_frames[0], 13 * TILE, 2 * TILE, 0);

    /* the keeper */
    if (keeper_mode == 2) {
        kr = anim_tick(&anim_carry, t);
    } else if (keeper_mode == 1) {
        kr = anim_tick(&anim_climb, t);
    } else {
        kr = anim_tick(&anim_idle, t);
    }
    asset_sprite4(kr, KEEPER_X - kr->ox, keeper_y - kr->oy, 0);

    draw_hud();
}

int main(void)
{
    /* the seven palette banks the art uses */
    gfx_pal_upload(0, sky16_palette, 16);
    gfx_pal_upload(16, tower16_palette, 16);
    gfx_pal_upload(32, rock16_palette, 16);
    gfx_pal_upload(48, sea16_palette, 16);
    gfx_pal_upload(64, keeper16_palette, 16);
    gfx_pal_upload(80, lamp16_palette, 16);
    gfx_pal_upload(96, ui16_palette, 16);

    reset_attempt();

    for (;;) {
        unsigned p = arcade1_pad();

        update(p);
        draw();
        arcade1_present();
        frame_no++;
    }
    return 0;
}
