/* Coin Runner -- ARCADE-1 (480x270, world 960 wide)
 *
 * Top-down: D-pad moves the player at a fixed speed, the camera follows
 * horizontally so the world scrolls, and the floor is a 4-bit tilemap that
 * fills the whole world. Collect 10 coins (+10 each), avoid 3 bouncing
 * enemies (-1 life, respawn), 3 lives. All coins -> WIN, no lives -> GAME
 * OVER, START restarts. The HUD is drawn in screen space.
 *
 * Observable state (score/lives/state/coins_left/cam_x) is file-scope so the
 * ARCADE-1 tools can read it out of guest RAM by name (CHEATSHEET.md,
 * "Observable state must be a global").
 */
#include "arcade1.h"
#include "assets.h"

/* Optional no-op hook used by tools/mksyms.py to recover global addresses from
 * --inspect. Normal builds expand it to nothing. */
#ifndef ARC1_SYMS_HOOK
#define ARC1_SYMS_HOOK() do { } while (0)
#endif

/* ---- observable globals (declared first, so their addresses are stable) -- */
int score;          /* +10 per coin                                            */
int lives;          /* starts at 3                                             */
int state;          /* STATE_PLAY / STATE_WIN / STATE_OVER                     */
int coins_left;     /* remaining coins; 0 -> WIN                               */
int cam_x;          /* camera x (world scroll); 0 .. WORLD_W-SCREEN_W          */

/* ------------------------------------------------------------------------ */
#define SCREEN_W 480
#define SCREEN_H 270
#define WORLD_W  960
#define WORLD_H  270

#define STATE_PLAY 0
#define STATE_WIN  1
#define STATE_OVER 2

#define PLAYER_W 16
#define PLAYER_H 16
#define PLAYER_SPEED 3
#define PLAYER_START_X 32
#define PLAYER_START_Y ((WORLD_H - PLAYER_H) / 2)   /* 127 */

#define ENEMY_W 12
#define ENEMY_H 12
#define NUM_ENEMIES 3
#define NUM_COINS 10
#define MAX_LIVES 3
#define INVULN_FRAMES 60

#define BG_COLOR 0x0841u

/* ---- internal state ----------------------------------------------------- */
static int player_x, player_y;
static int facing;          /* +1 = right (default art), -1 = left (flipped) */
static int invuln;          /* frames of post-respawn invulnerability         */

static int coin_x[NUM_COINS];
static int coin_y[NUM_COINS];
static int coin_alive[NUM_COINS];

static int enemy_x[NUM_ENEMIES];
static int enemy_y[NUM_ENEMIES];
static int enemy_vx[NUM_ENEMIES];
static int enemy_vy[NUM_ENEMIES];

/* Floor: 60 x 17 cells of 16x16 tiles = 960 x 272 (bottom row clips). */
#define FLOOR_COLS (WORLD_W / 16)
#define FLOOR_ROWS ((WORLD_H + 15) / 16)
static unsigned int floor_cells[FLOOR_COLS * FLOOR_ROWS];

/* ---------------------------------------------------------------- helpers */
static int overlap(int ax, int ay, int aw, int ah,
                   int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void reset_game(void)
{
    static const int cx[NUM_COINS] = { 96, 180, 264, 348, 432,
                                       516, 600, 684, 768, 852 };
    static const int ex[NUM_ENEMIES] = { 32, 120, 640 };
    static const int ey[NUM_ENEMIES] = { 0, 200, 240 };
    static const int evx[NUM_ENEMIES] = { 0, 6, -5 };
    static const int evy[NUM_ENEMIES] = { 8, 0, 0 };

    score = 0;
    lives = MAX_LIVES;
    state = STATE_PLAY;
    coins_left = NUM_COINS;
    cam_x = 0;

    player_x = PLAYER_START_X;
    player_y = PLAYER_START_Y;
    facing = 1;
    invuln = 0;

    for (int i = 0; i < NUM_COINS; i++) {
        coin_x[i] = cx[i];
        coin_y[i] = 132;
        coin_alive[i] = 1;
    }
    for (int i = 0; i < NUM_ENEMIES; i++) {
        enemy_x[i] = ex[i];
        enemy_y[i] = ey[i];
        enemy_vx[i] = evx[i];
        enemy_vy[i] = evy[i];
    }
}

static void update_play(unsigned pad)
{
    int dx = 0, dy = 0;

    if (pad & BTN_LEFT)  dx = -1;
    if (pad & BTN_RIGHT) dx = 1;
    if (pad & BTN_UP)    dy = -1;
    if (pad & BTN_DOWN)  dy = 1;

    player_x += dx * PLAYER_SPEED;
    player_y += dy * PLAYER_SPEED;
    if (dx < 0) facing = -1;
    else if (dx > 0) facing = 1;

    if (player_x < 0) player_x = 0;
    if (player_x > WORLD_W - PLAYER_W) player_x = WORLD_W - PLAYER_W;
    if (player_y < 0) player_y = 0;
    if (player_y > WORLD_H - PLAYER_H) player_y = WORLD_H - PLAYER_H;

    /* enemies bounce inside the world */
    for (int i = 0; i < NUM_ENEMIES; i++) {
        enemy_x[i] += enemy_vx[i];
        enemy_y[i] += enemy_vy[i];
        if (enemy_x[i] < 0) { enemy_x[i] = 0; enemy_vx[i] = -enemy_vx[i]; }
        if (enemy_x[i] > WORLD_W - ENEMY_W) {
            enemy_x[i] = WORLD_W - ENEMY_W; enemy_vx[i] = -enemy_vx[i];
        }
        if (enemy_y[i] < 0) { enemy_y[i] = 0; enemy_vy[i] = -enemy_vy[i]; }
        if (enemy_y[i] > WORLD_H - ENEMY_H) {
            enemy_y[i] = WORLD_H - ENEMY_H; enemy_vy[i] = -enemy_vy[i];
        }
    }

    /* coin pickup */
    for (int i = 0; i < NUM_COINS; i++) {
        if (!coin_alive[i]) continue;
        if (overlap(player_x, player_y, PLAYER_W, PLAYER_H,
                    coin_x[i], coin_y[i], 8, 8)) {
            coin_alive[i] = 0;
            score += 10;
            coins_left--;
        }
    }
    if (coins_left <= 0) {
        state = STATE_WIN;
        return;
    }

    /* enemy hit */
    if (invuln > 0) {
        invuln--;
    } else {
        for (int i = 0; i < NUM_ENEMIES; i++) {
            if (overlap(player_x, player_y, PLAYER_W, PLAYER_H,
                        enemy_x[i], enemy_y[i], ENEMY_W, ENEMY_H)) {
                lives--;
                if (lives <= 0) {
                    lives = 0;
                    state = STATE_OVER;
                } else {
                    player_x = PLAYER_START_X;
                    player_y = PLAYER_START_Y;
                    invuln = INVULN_FRAMES;
                }
                break;
            }
        }
    }

    /* camera follows horizontally, clamped to the world */
    cam_x = player_x + PLAYER_W / 2 - SCREEN_W / 2;
    if (cam_x < 0) cam_x = 0;
    if (cam_x > WORLD_W - SCREEN_W) cam_x = WORLD_W - SCREEN_W;
}

/* ---------------------------------------------------------------- drawing */
static void draw_word(const unsigned char *const *w, int n, int x, int y)
{
    for (int i = 0; i < n; i++)
        arcade1_sprite4(x + i * 7, y, w[i], 6, 8, BANK_HUD, 0);
}

static void draw_score(int x, int y, int value)
{
    int d0 = (value / 100) % 10;
    int d1 = (value / 10) % 10;
    int d2 = value % 10;
    arcade1_sprite4(x,      y, digit_img[d0], 6, 8, BANK_HUD, 0);
    arcade1_sprite4(x + 7,  y, digit_img[d1], 6, 8, BANK_HUD, 0);
    arcade1_sprite4(x + 14, y, digit_img[d2], 6, 8, BANK_HUD, 0);
}

static void draw_hud(void)
{
    /* screen-space: camera is pinned to (0,0) so sprite4 lands on screen */
    arcade1_cam(0, 0);
    arcade1_rect(0, 0, SCREEN_W, 18, 0x0000u);

    /* lives as hearts, top-left */
    for (int i = 0; i < lives; i++)
        arcade1_sprite4(6 + i * 11, 5, heart_img, 8, 8, BANK_HUD, 0);

    /* coins left: an icon then two digits, centred */
    arcade1_sprite4(SCREEN_W / 2 - 16, 5, coin_img, 8, 8, BANK_COIN, 0);
    arcade1_sprite4(SCREEN_W / 2 - 6,  5, digit_img[(coins_left / 10) % 10], 6, 8, BANK_HUD, 0);
    arcade1_sprite4(SCREEN_W / 2 + 1,  5, digit_img[coins_left % 10], 6, 8, BANK_HUD, 0);

    /* score, top-right */
    draw_score(SCREEN_W - 6 - 21, 5, score);

    /* end-state banners */
    if (state == STATE_WIN) {
        arcade1_rect(150, 100, 180, 70, 0x0120u);
        arcade1_rect(150, 100, 180, 3, 0x07E0u);
        arcade1_rect(150, 167, 180, 3, 0x07E0u);
        draw_word(word_win, 3, SCREEN_W / 2 - 10, 128);
    } else if (state == STATE_OVER) {
        arcade1_rect(150, 100, 180, 70, 0x3000u);
        arcade1_rect(150, 100, 180, 3, 0xF800u);
        arcade1_rect(150, 167, 180, 3, 0xF800u);
        draw_word(word_over, 9, SCREEN_W / 2 - 31, 128);
    }
}

static void draw_world(void)
{
    arcade1_cam(cam_x, 0);

    arcade1_tilemap4(0, 0, floor_cells, floor_tiles,
                     FLOOR_COLS, FLOOR_ROWS, 16, 16);

    for (int i = 0; i < NUM_COINS; i++)
        if (coin_alive[i])
            arcade1_sprite4(coin_x[i], coin_y[i], coin_img, 8, 8, BANK_COIN, 0);

    for (int i = 0; i < NUM_ENEMIES; i++)
        arcade1_sprite4(enemy_x[i], enemy_y[i], enemy_img, 12, 12, BANK_ENEMY, 0);

    arcade1_sprite4(player_x, player_y, player_img, 16, 16,
                    BANK_PLAYER, facing < 0 ? 1u : 0u);
}

/* ------------------------------------------------------------------- main */
int main(void)
{
    /* one-time setup */
    for (int i = 0; i < PAL_N; i++)
        arcade1_pal(i, palette[i]);
    for (int i = 0; i < FLOOR_COLS * FLOOR_ROWS; i++)
        floor_cells[i] = (unsigned int)((i & 1) ? 2 : 1) | (BANK_FLOOR << 16);

    reset_game();

    for (;;) {                                  /* one iteration = one frame */
        unsigned pad = arcade1_pad();

        if (state == STATE_PLAY) {
            update_play(pad);
        } else if (pad & BTN_START) {           /* restart from WIN/GAME OVER */
            reset_game();
        }

        arcade1_clear(BG_COLOR);
        draw_world();
        draw_hud();
        ARC1_SYMS_HOOK();                        /* no-op unless symbols asked */
        arcade1_present();                       /* ends the frame */
    }
}
