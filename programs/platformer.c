/* ARCADE-1 platformer — milestone 22's game, as a GUEST program.
 *
 * Runs entirely on the RV32IM interpreter inside the machine. It is the
 * integration milestone: before the architecture review (M23) it must exercise
 * every capability built so far, in one real game:
 *
 *   tilemaps     the terrain is one gfx_tilemap (M20), a 40x16 grid of 16px
 *                cells whose index drives BOTH rendering and collision (the
 *                cell array is the single source of truth, like Tetris);
 *   sprites      the player (animated) and the coins are gfx_sprites (M19);
 *   scrolling    the camera (M21) follows the player, clamped to the world;
 *   collision    axis-separated AABB vs the solid terrain cells;
 *   animation    a 3-frame player (2 walk + 1 airborne) cycled by a timer;
 *   layers       sky -> parallax background (screen-space, half-rate) ->
 *                terrain tilemap (world-space) -> sprites -> HUD;
 *   input        left/right walk, A to jump (INPUT_PAD);
 *   audio        jump / coin / goal beeps (AUDIO_FREQ/CTRL).
 *
 * World: 40x16 tiles of 16px = 640x256 px. The screen is 320x240, so the
 * camera pans 320px horizontally and 16px vertically. The terrain lives in RAM
 * (built once at boot from a deterministic layout) so the whole world fits the
 * 4 KiB budget; the compact tile art lives in ROM.
 *
 * Game states (the test reads `state`):
 *   0 ST_PLAY  running around
 *   1 ST_WIN   reached the goal flag
 *
 * Controls: left/right walk, A jump. Reaching the magenta flag wins.
 *
 * Frame protocol: build once, then loop forever (update, draw, present); the
 * machine runs one iteration per host frame (docs.md M15 present-yield).
 */

#include "gfx.h"

/* --- MMIO registers (fixed map, see docs.md table) ----------------------- */

#define MMIO_BASE       0x4000u
#define REG_TIMER_COUNT (MMIO_BASE + 0x00u)
#define REG_INPUT_PAD   (MMIO_BASE + 0x200u)
#define REG_AUDIO_FREQ  (MMIO_BASE + 0x300u)
#define REG_AUDIO_CTRL  (MMIO_BASE + 0x304u)

#define PAD_LEFT  0x04u
#define PAD_RIGHT 0x08u
#define PAD_A     0x10u

/* --- World / screen geometry ---------------------------------------------- */

#define TILE   16
#define LCOLS  40          /* level width in tiles  -> 640 px                */
#define LROWS  16          /* level height in tiles -> 256 px                */
#define WORLD_W (LCOLS * TILE)   /* 640 */
#define WORLD_H (LROWS * TILE)   /* 256 */
#define VIEW_W  320
#define VIEW_H  240

/* Player: smaller than a tile so it fits through gaps. */
#define PW 12
#define PH 14
#define SPEED   2          /* px / frame while walking                       */
#define GRAV    1          /* px / frame^2                                  */
#define JUMP    11         /* launch speed; rise = J*(J-1)/2 = 55px (~3.4    */
                           /* tiles). The floating platforms sit at rows 11  */
                           /* and 12 (top y=176 / 192) above the ground at   */
                           /* row 14 (y=224): reaching the row-11 platforms  */
                           /* needs >48px of rise, and 55px clears them with */
                           /* a 7px margin so the player can LAND on top.    */
                           /* (JUMP=6 only rose 15px — under one tile — so   */
                           /* no platform or elevated coin was ever reachable.) */
#define MAXFALL 5

/* --- Palette (RGB565) ----------------------------------------------------- */

#define COL_SKY   0x2E7Fu
#define COL_HILL  0x1C06u
#define COL_CLOUD 0xFFFFu
#define COL_GND   0x8260u   /* brown terrain block                           */
#define COL_PLR   0xFD20u   /* orange player (unique, easy to locate)        */
#define COL_COIN  0xFFE0u   /* yellow                                        */
#define COL_HUD   0xFFFFu
#define COL_GOAL  0xF81Fu   /* magenta flag                                  */

/* Tone events (the test reads AUDIO_FREQ after each). */
#define BE_JUMP 660u
#define BE_COIN 880u
#define BE_GOAL 440u
#define BE_TICKS 6

#define ST_PLAY 0
#define ST_WIN  1

/* --- Compact tile art (1-bpp, MSB = leftmost) ----------------------------- *
 * Terrain is one 16x16 block (2 bytes/row). The player is 12x14 (2 bytes/row,
 * 3 frames). The coin is 8x8 (1 byte/row). */

static const unsigned char tile_block[32] = {
    0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
    0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
    0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
    0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
};

/* Three player frames, 12 px wide (2 bytes/row) x 14 rows. */
static const unsigned char plr_idle[28] = {
    0x03,0xC0, 0x07,0xE0, 0x0F,0xF0, 0x0F,0xF0,
    0x07,0xE0, 0x03,0xC0, 0x0F,0xF0, 0x1F,0xF8,
    0x1F,0xF8, 0x0F,0xF0, 0x06,0x60, 0x06,0x60,
    0x0C,0x30, 0x0C,0x30,
};
static const unsigned char plr_walk[28] = {
    0x03,0xC0, 0x07,0xE0, 0x0F,0xF0, 0x0F,0xF0,
    0x07,0xE0, 0x03,0xC0, 0x0F,0xF0, 0x1F,0xF8,
    0x1F,0xF8, 0x0F,0xF0, 0x03,0xC0, 0x09,0x90,
    0x09,0x90, 0x06,0x60,
};
static const unsigned char plr_air[28] = {
    0x03,0xC0, 0x0F,0xF0, 0x1F,0xF8, 0x1F,0xF8,
    0x0F,0xF0, 0x03,0xC0, 0x1F,0xF8, 0x3F,0xFC,
    0x3F,0xFC, 0x1F,0xF8, 0x0C,0x30, 0x0C,0x30,
    0x18,0x18, 0x18,0x18,
};

static const unsigned char coin_img[8] = {
    0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C,
};

/* The one tile set handed to gfx_tilemap: index 1 = the terrain block. */
static const unsigned char *const tiles[1] = { tile_block };

/* --- Level --------------------------------------------------------------- *
 * cells[row*LCOLS + col]: low 16 bits = tile index (0 empty, 1 solid block),
 * high 16 bits = colour. Collision reads the index; rendering draws it. */

int cells[LROWS * LCOLS];        /* the test reads this out of RAM           */

static int px, py;               /* player top-left, world pixels            */
static int vx, vy;
static int onground;
static int score;
int state;
int cam_x, cam_y;                /* camera top-left, world pixels            */
static int anim_t;

/* Coins: world pixel positions with a collected flag. */
#define NCOIN 6
static int coin_x[NCOIN], coin_y[NCOIN], coin_got[NCOIN];

/* Goal flag, world pixels (a pole standing on the ground at column 37). */
#define GOAL_X (37 * TILE)
#define GOAL_Y (12 * TILE)

/* --- Beep ---------------------------------------------------------------- */
static unsigned beep_until;
static int      beep_on;

static unsigned mmio_read(unsigned a)  { return *(volatile unsigned *)a; }
static void     mmio_write(unsigned a, unsigned v)
{ *(volatile unsigned *)a = v; }

static void event_beep(unsigned hz)
{
    mmio_write(REG_AUDIO_FREQ, hz);
    mmio_write(REG_AUDIO_CTRL, 1u);
    beep_until = mmio_read(REG_TIMER_COUNT) + BE_TICKS;
    beep_on = 1;
}

/* --- Level construction --------------------------------------------------- */

static void set_cell(int col, int row, int solid)
{
    if (col < 0 || col >= LCOLS || row < 0 || row >= LROWS)
        return;
    cells[row * LCOLS + col] = solid ? (1 | (COL_GND << 16)) : 0;
}

/* Solid at a world pixel (walls at the sides, floor below the level). */
static int solid_px(int wx, int wy)
{
    int c, r;
    if (wx < 0 || wx >= WORLD_W)
        return 1;                       /* side walls keep the player in    */
    if (wy < 0)
        return 0;                       /* open sky above                   */
    if (wy >= WORLD_H)
        return 1;                       /* floor below the level            */
    c = wx / TILE;
    r = wy / TILE;
    return (cells[r * LCOLS + c] & 0xFFFF) != 0;
}

static void build_level(void)
{
    int c, r;

    for (r = 0; r < LROWS; r++)
        for (c = 0; c < LCOLS; c++)
            set_cell(c, r, 0);

    /* Ground: the bottom two rows are solid across the whole level, so the
     * main path is walkable end to end (a scripted "hold right" reaches the
     * goal). The vertical interest is in the floating platforms above. */
    for (c = 0; c < LCOLS; c++) {
        set_cell(c, 14, 1);
        set_cell(c, 15, 1);
    }

    /* Floating platforms (two-three tiles above the ground) — landing
     * surfaces for jump collision. Rows 11 and 12 put their tops at y=176 and
     * y=192, i.e. 48px and 32px above the ground surface (y=224). They must
     * stay at row 12 or higher: row 13 would sit in the walking lane (a 14px
     * player on the ground occupies y[210,224), overlapping row 13's
     * [208,224)) and block the hold-right path to the goal. JUMP=11 (rise
     * 55px) clears the higher row-11 platforms so the player can land on top. */
    for (c = 6;  c <= 9;  c++) set_cell(c, 11, 1);
    for (c = 15; c <= 18; c++) set_cell(c, 12, 1);
    for (c = 25; c <= 28; c++) set_cell(c, 11, 1);

    /* Coins (world pixels). The first sits on the ground path near the spawn
     * (collected by simply walking right); the rest sit above platforms. */
    coin_x[0] = 4 * TILE + 4;  coin_y[0] = 13 * TILE;   /* on the ground path */
    coin_x[1] = 9 * TILE + 4;  coin_y[1] = 10 * TILE;   /* above a platform  */
    coin_x[2] = 16 * TILE + 4; coin_y[2] = 11 * TILE;
    coin_x[3] = 26 * TILE + 4; coin_y[3] = 10 * TILE;
    coin_x[4] = 22 * TILE + 4; coin_y[4] = 13 * TILE;   /* on the ground path */
    coin_x[5] = 34 * TILE + 4; coin_y[5] = 13 * TILE;   /* on the ground path */
    for (c = 0; c < NCOIN; c++)
        coin_got[c] = 0;
}

/* --- Collision ------------------------------------------------------------ */

/* Move horizontally by vx and resolve against solid tiles. */
static void move_x(void)
{
    int i, edge;
    px += vx;
    if (vx > 0) {
        edge = px + PW - 1;
        for (i = 0; i < PH; i++)
            if (solid_px(edge, py + i)) { px = (edge / TILE) * TILE - PW;
                                          vx = 0; return; }
    } else if (vx < 0) {
        edge = px;
        for (i = 0; i < PH; i++)
            if (solid_px(edge, py + i)) { px = (edge / TILE + 1) * TILE;
                                          vx = 0; return; }
    }
}

/* Move vertically by vy and resolve; update onground. */
static void move_y(void)
{
    int i, edge;
    py += vy;
    onground = 0;
    if (vy > 0) {
        edge = py + PH - 1;
        for (i = 0; i < PW; i++)
            if (solid_px(px + i, edge)) { py = (edge / TILE) * TILE - PH;
                                          vy = 0; onground = 1; return; }
    } else if (vy < 0) {
        edge = py;
        for (i = 0; i < PW; i++)
            if (solid_px(px + i, edge)) { py = (edge / TILE + 1) * TILE;
                                          vy = 0; return; }
    } else {
        /* Standing: check just below the feet. */
        for (i = 0; i < PW; i++)
            if (solid_px(px + i, py + PH)) { onground = 1; return; }
    }
}

/* --- Coins ---------------------------------------------------------------- */

static void collect_coins(void)
{
    int i;
    for (i = 0; i < NCOIN; i++) {
        if (coin_got[i])
            continue;
        if (px < coin_x[i] + 8 && px + PW > coin_x[i] &&
            py < coin_y[i] + 8 && py + PH > coin_y[i]) {
            coin_got[i] = 1;
            score++;
            event_beep(BE_COIN);
        }
    }
}

/* --- Frame update --------------------------------------------------------- */

static void update(void)
{
    unsigned pad = mmio_read(REG_INPUT_PAD);
    int i;

    if (beep_on && mmio_read(REG_TIMER_COUNT) >= beep_until) {
        mmio_write(REG_AUDIO_CTRL, 0u);
        beep_on = 0;
    }

    if (state == ST_WIN) {
        if (pad & PAD_A) {              /* A restarts from the spawn        */
            build_level();
            px = 2 * TILE; py = 14 * TILE - PH;
            vx = vy = 0; score = 0; state = ST_PLAY;
        }
    } else {
        vx = 0;
        if (pad & PAD_LEFT)  vx = -SPEED;
        if (pad & PAD_RIGHT) vx =  SPEED;
        if ((pad & PAD_A) && onground) { vy = -JUMP; onground = 0;
                                         event_beep(BE_JUMP); }

        vy += GRAV;
        if (vy > MAXFALL) vy = MAXFALL;

        move_x();
        move_y();
        collect_coins();

        /* Win: overlap the goal flag. */
        if (px < GOAL_X + 8 && px + PW > GOAL_X &&
            py < GOAL_Y + 24 && py + PH > GOAL_Y) {
            state = ST_WIN;
            event_beep(BE_GOAL);
        }

        if (vx != 0)
            anim_t++;
    }

    /* Camera follows the player, clamped to the world. */
    cam_x = px + PW / 2 - VIEW_W / 2;
    if (cam_x < 0) cam_x = 0;
    if (cam_x > WORLD_W - VIEW_W) cam_x = WORLD_W - VIEW_W;
    cam_y = py + PH / 2 - VIEW_H / 2;
    if (cam_y < 0) cam_y = 0;
    if (cam_y > WORLD_H - VIEW_H) cam_y = WORLD_H - VIEW_H;

    (void)i;
}

/* --- Drawing -------------------------------------------------------------- */

/* Parallax background: screen-space rects offset by half the camera. This is
 * the far layer; it scrolls slower than the terrain (which uses the camera). */
static void draw_background(void)
{
    int bx, by;
    for (bx = -60; bx < WORLD_W; bx += 120) {
        int sx = bx - cam_x / 2;
        if (sx > VIEW_W || sx < -60) continue;
        gfx_rect(sx, 150, 90, 90, COL_HILL);      /* a distant hill         */
    }
    /* A couple of clouds, drifting at an even slower rate. */
    gfx_rect(40  - cam_x / 4, 30, 40, 10, COL_CLOUD);
    gfx_rect(180 - cam_x / 4, 50, 50, 10, COL_CLOUD);
    gfx_rect(300 - cam_x / 4, 24, 44, 10, COL_CLOUD);
    (void)by;
}

static void draw(void)
{
    int i;
    const unsigned char *frame;

    gfx_clear(COL_SKY);
    draw_background();

    /* World-space layers use the camera (M21). */
    gfx_camera(cam_x, cam_y);

    /* Terrain tilemap (M20): one command for the whole grid. */
    gfx_tilemap(0, 0, cells, tiles, LCOLS, LROWS, TILE, TILE);

    /* Goal flag (screen-space primitive placed in world coords). */
    if (state != ST_WIN)
        gfx_rect(GOAL_X + 2, GOAL_Y, 4, 24, COL_GOAL);

    /* Coins (sprites). */
    for (i = 0; i < NCOIN; i++)
        if (!coin_got[i])
            gfx_sprite(coin_x[i], coin_y[i], coin_img, 8, 8, COL_COIN);

    /* Player (animated sprite): airborne pose, else 2-frame walk, else idle. */
    if (!onground || state == ST_WIN)      frame = plr_air;
    else if (vx != 0 && (anim_t & 8))      frame = plr_walk;
    else if (vx != 0)                      frame = plr_idle;
    else                                   frame = plr_idle;
    gfx_sprite(px, py, frame, PW, PH, COL_PLR);

    /* HUD (screen-space, never scrolled): score pips. */
    for (i = 0; i < score && i < 10; i++)
        gfx_rect(4 + i * 6, 4, 4, 6, COL_HUD);
    if (state == ST_WIN)
        gfx_rect(VIEW_W / 2 - 20, 20, 40, 8, COL_HUD);
    gfx_present();
}

/* --- Entry point ---------------------------------------------------------- */

int main(void)
{
    mmio_write(REG_AUDIO_CTRL, 0u);   /* silent boot                         */
    beep_on = 0;
    build_level();
    /* Spawn a little above the ground so the first frames fall + land. */
    px = 2 * TILE; py = 14 * TILE - PH - 24;
    vx = vy = 0; onground = 0; score = 0; state = ST_PLAY; anim_t = 0;

    for (;;) {
        update();
        draw();
    }
    return 0;
}
