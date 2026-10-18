/* Breakout-lite — ARCADE-1 guest game (M29 SDK validation), golden game 6.
 *
 * The game the M29 fresh-agent test built out of the shipped CLI + docs.
 * It lives here so it builds and golden-locks like every other guest
 * (tests/roms/m29game.arc1, tests/golden/m29game.txt); the record copy is
 * docs/m29/game.c and the two are kept identical.
 *
 * Paddle (LEFT/RIGHT), ball, 3 rows x 10 bricks, 3 lives, score,
 * game-over state, START to restart.
 *
 * Graphics used (all four 4-bit/paletted paths are exercised on purpose):
 *   - TILEMAP4 : the brick field. One 48x12 4-bit tile, 10x3 cells; every
 *                cell points at that single tile (index 1) and carries its
 *                own palette bank, so the three rows are three colours out
 *                of one tile. Cells are cleared to 0 when a brick is
 *                destroyed, so the field is the live brick state.
 *                Two TILEMAP4 traps found by probing (M29_LOG.md, "DOC/API
 *                WRONG 3"): a cell whose tile index runs past the end of the
 *                tile blob silently draws nothing, and a map smaller than
 *                COLS>=6 x ROWS>=6-ish draws nothing at all (10x3 passes).
 *   - SPRITE4  : the paddle (56x10, bank 4) and the ball (8x8, bank 5).
 *   - palette  : 6 banks x 16 RGB565 entries, uploaded once at power-on.
 *   - plain rects : the HUD text (a 3x5 font) — screen space, never scrolled.
 *
 * Build:  arcade1 build game.c game.arc1
 * Test:   arcade1 run game.arc1 --frames N --input script.txt \
 *             --syms game.syms --vars score,lives,state --capture frame.ppm
 *
 * M29_LOG.md "DOC GAP 4": at test time `arcade1 build` passed no -I, so
 * this file had to include "include/arcade1.h" by directory. The CLI now
 * carries the SDK and passes -I (M29 fix), so the plain include below is
 * what the docs always said — and this is the game `make check` runs as
 * the sixth golden (programs/m29game.c, tests/roms, tests/golden).
 */
#include "arcade1.h"

/* ------------------------------------------------------------------ */
/* RGB565 helper                                                       */
#define RGB(r, g, b) (unsigned)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31))

/* Screen. */
#define SCREEN_W 480
#define SCREEN_H 270

/* Brick field: 10 cols x 3 rows of 48x12 bricks, wall to wall — a corridor
 * beside the field would let the ball loop forever without scoring. */
#define BRICK_COLS 10
#define BRICK_ROWS 3
#define BRICK_W    48
#define BRICK_H    12
#define BRICK_X    0
#define BRICK_Y    28

/* Paddle and ball. */
#define PADDLE_W    56
#define PADDLE_H    10
#define PADDLE_Y    246
#define PADDLE_SPEED 5          /* pixels per frame */
#define BALL_W      8
#define BALL_H      8

/* Palette banks (PALETTE[bank*16 + v], v = 0 is transparent for 4-bit). */
#define BANK_BRICK0 1           /* top brick row    */
#define BANK_BRICK1 2           /* middle brick row */
#define BANK_BRICK2 3           /* bottom brick row */
#define BANK_PADDLE 4
#define BANK_BALL   5

/* Game states. */
#define STATE_PLAY     0
#define STATE_GAMEOVER 1

/* Ball speeds are in 1/16ths of a pixel per frame (fixed point). */
#define SUBPIX     16
#define SERVE_VY   32          /* 2.00 px/frame */
#define SERVE_VX   20          /* 1.25 px/frame */
#define MAX_VY     64          /* 4.00 px/frame */
#define MAX_VX     56          /* 3.50 px/frame */
#define MIN_VX     12          /* 0.75 px/frame — never stall sideways */

/* ------------------------------------------------------------------ */
/* Observable state — the tools read these out of guest RAM by name,   */
/* so every one of them is also read by the game itself (a global that */
/* is only written is dead code and -Os deletes the store).            */
int score;
int lives;
int state;
int paddle_x;
int ball_x, ball_y;            /* ball top-left, pixels                */
int ball_vx, ball_vy;          /* 1/16 px per frame                    */
int bricks_left;
int frame_no;

/* ------------------------------------------------------------------ */
/* Internal state.                                                     */
static int ball_fx, ball_fy;   /* ball position in 1/16 px             */
static unsigned cells[BRICK_ROWS * BRICK_COLS];   /* TILEMAP4 cell data */
static unsigned char brick_tile[BRICK_H * (BRICK_W / 2)];
static unsigned char paddle_img[PADDLE_H * (PADDLE_W / 2)];

/* 8x8 4-bit ball, two pixels per byte, HIGH nibble = left pixel.      */
static const unsigned char ball_img[BALL_H * (BALL_W / 2)] = {
    0x03, 0x33, 0x30, 0x00,
    0x03, 0x11, 0x11, 0x30,
    0x32, 0x11, 0x11, 0x13,
    0x32, 0x11, 0x11, 0x13,
    0x31, 0x11, 0x11, 0x13,
    0x31, 0x11, 0x11, 0x13,
    0x03, 0x11, 0x11, 0x30,
    0x03, 0x33, 0x30, 0x00,
};

static unsigned pad_prev;

/* ------------------------------------------------------------------ */
/* Palette: banks 0..BANK_BALL, 16 RGB565 entries each, index 0 unused. */
static const unsigned short palette[(BANK_BALL + 1) * 16] = {
    /* bank 0 (unused) */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* bank 1 — bricks, top row (red) */
    0,
    (unsigned short)RGB(30,  8,  8),  /* 1 body   */
    (unsigned short)RGB(31, 18, 14),  /* 2 shine  */
    (unsigned short)RGB(16,  3,  4),  /* 3 shade  */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* bank 2 — bricks, middle row (yellow) */
    0,
    (unsigned short)RGB(31, 22,  4),
    (unsigned short)RGB(31, 29, 12),
    (unsigned short)RGB(22, 12,  2),
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* bank 3 — bricks, bottom row (green) */
    0,
    (unsigned short)RGB(10, 28, 14),
    (unsigned short)RGB(18, 31, 22),
    (unsigned short)RGB( 3, 16,  7),
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* bank 4 — paddle (cyan/steel) */
    0,
    (unsigned short)RGB(12, 24, 31),
    (unsigned short)RGB(26, 31, 31),
    (unsigned short)RGB( 3,  9, 20),
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* bank 5 — ball (white/grey) */
    0,
    (unsigned short)RGB(31, 31, 31),
    (unsigned short)RGB(26, 26, 26),
    (unsigned short)RGB(17, 17, 17),
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/* ------------------------------------------------------------------ */
/* Tiny 3x5 font for the HUD / game-over banner, drawn as rects.       */
/* Each glyph is 5 rows; low 3 bits per row, bit 2 = leftmost pixel.   */
static const unsigned char font3x5[] = {
    /* 0 */ 7, 5, 5, 5, 7,
    /* 1 */ 2, 6, 2, 2, 7,
    /* 2 */ 7, 1, 7, 4, 7,
    /* 3 */ 7, 1, 7, 1, 7,
    /* 4 */ 5, 5, 7, 1, 1,
    /* 5 */ 7, 4, 7, 1, 7,
    /* 6 */ 7, 4, 7, 5, 7,
    /* 7 */ 7, 1, 1, 1, 1,
    /* 8 */ 7, 5, 7, 5, 7,
    /* 9 */ 7, 5, 7, 1, 7,
    /* A */ 7, 5, 7, 5, 5,
    /* C */ 7, 4, 4, 4, 7,
    /* E */ 7, 4, 7, 4, 7,
    /* G */ 7, 4, 5, 5, 7,
    /* I */ 7, 2, 2, 2, 7,
    /* L */ 4, 4, 4, 4, 7,
    /* M */ 5, 7, 7, 5, 5,
    /* O */ 7, 5, 5, 5, 7,
    /* R */ 7, 5, 7, 6, 5,
    /* S */ 7, 4, 7, 1, 7,
    /* V */ 5, 5, 5, 5, 7,
};
static const char font_chars[] = "0123456789ACEGILMORSV";
#define FONT_GLYPHS ((int)(sizeof font_chars - 1))

static void draw_text(int x, int y, int scale, const char *s, unsigned color)
{
    for (; *s; s++) {
        int g;

        for (g = 0; g < FONT_GLYPHS && font_chars[g] != *s; g++)
            ;
        if (g < FONT_GLYPHS) {
            const unsigned char *glyph = font3x5 + g * 5;
            int r, c;

            for (r = 0; r < 5; r++)
                for (c = 0; c < 3; c++)
                    if (glyph[r] & (unsigned char)(4 >> c))
                        arcade1_rect(x + c * scale, y + r * scale,
                                     scale, scale, color);
        }
        x += 4 * scale;                     /* 3 px glyph + 1 px gap */
    }
}

/* Draw v in decimal at (x, y). No sprintf in a freestanding guest. */
static void draw_number(int x, int y, int scale, int v, unsigned color)
{
    char buf[12];
    char one[2];
    int n = 0, i;

    if (v < 0)
        v = 0;
    do {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v > 0 && n < (int)sizeof buf);

    one[1] = 0;
    for (i = n - 1; i >= 0; i--) {
        one[0] = buf[i];
        draw_text(x, y, scale, one, color);
        x += 4 * scale;
    }
}

/* ------------------------------------------------------------------ */
/* Asset construction (RAM images: gfx accepts ROM or RAM pointers).   */
static void set4(unsigned char *img, int stride, int x, int y, unsigned v)
{
    unsigned char *p = img + y * stride + (x >> 1);

    if (x & 1)
        *p = (unsigned char)((*p & 0xF0u) | (v & 15u));
    else
        *p = (unsigned char)((*p & 0x0Fu) | ((v & 15u) << 4));
}

static void make_assets(void)
{
    int r, c;

    gfx_pal_upload(0, palette, (int)(sizeof palette / sizeof palette[0]));

    /* One 48x12 brick tile: value 1 body, 2 top/left shine, 3 bottom/right
     * shadow — the three brick rows pick their colour from the cell bank. */
    for (r = 0; r < BRICK_H; r++)
        for (c = 0; c < BRICK_W; c++) {
            unsigned v = 1;
            if (r == 0 || c == 0)
                v = 2;
            else if (r == BRICK_H - 1 || c == BRICK_W - 1)
                v = 3;
            set4(brick_tile, BRICK_W / 2, c, r, v);
        }

    /* 56x10 paddle: light top row, body, dark bottom row, rounded ends. */
    for (r = 0; r < PADDLE_H; r++)
        for (c = 0; c < PADDLE_W; c++) {
            unsigned v = 1;
            if (r == 0)
                v = 2;
            else if (r == PADDLE_H - 1)
                v = 3;
            if ((r == 0 || r == PADDLE_H - 1) &&
                (c == 0 || c == 1 || c == PADDLE_W - 1 || c == PADDLE_W - 2))
                v = 0;
            set4(paddle_img, PADDLE_W / 2, c, r, v);
        }
}

/* ------------------------------------------------------------------ */
/* Game logic.                                                         */
/* Every brick cell references the SAME tile (index 1); the row colour comes
 * from the cell's palette bank. The device resolves tile N to
 * `tiles + (N-1)*(tile_w/2*tile_h)` and silently skips pixels whose source
 * byte lies outside RAM/ROM, so a cell pointing past the end of the tile blob
 * draws nothing at all — one shared tile keeps every cell in range. */
#define BRICK_TILE_INDEX 1u

static void fill_bricks(void)
{
    int r, c;

    for (r = 0; r < BRICK_ROWS; r++)
        for (c = 0; c < BRICK_COLS; c++)
            cells[r * BRICK_COLS + c] =
                BRICK_TILE_INDEX | ((unsigned)(BANK_BRICK0 + r) << 16);
    bricks_left = BRICK_ROWS * BRICK_COLS;
}

/* Serve from just above the paddle, heading away from it — so a parked
 * paddle (or a scripted one) does not immediately catch the ball. */
static void serve_ball(void)
{
    int pc = paddle_x + PADDLE_W / 2;

    ball_fx = (paddle_x + PADDLE_W / 2 - BALL_W / 2) << 4;
    ball_fy = (PADDLE_Y - 18) << 4;
    ball_x = ball_fx >> 4;
    ball_y = ball_fy >> 4;
    ball_vx = (pc <= SCREEN_W / 2) ? SERVE_VX : -SERVE_VX;
    ball_vy = -SERVE_VY;
}

static void reset_game(void)
{
    score = 0;
    lives = 3;
    state = STATE_PLAY;
    paddle_x = (SCREEN_W - PADDLE_W) / 2;
    fill_bricks();
    serve_ball();
}

/* Returns non-zero if a brick was destroyed (and vy was reversed). */
static int ball_vs_bricks(void)
{
    int x = ball_x, y = ball_y;
    int c0, c1, r0, r1, c, r;

    if (x + BALL_W <= BRICK_X || x >= BRICK_X + BRICK_COLS * BRICK_W ||
        y + BALL_H <= BRICK_Y || y >= BRICK_Y + BRICK_ROWS * BRICK_H)
        return 0;

    c0 = (x - BRICK_X) / BRICK_W; if (c0 < 0) c0 = 0;
    c1 = (x + BALL_W - 1 - BRICK_X) / BRICK_W;
    if (c1 > BRICK_COLS - 1) c1 = BRICK_COLS - 1;
    r0 = (y - BRICK_Y) / BRICK_H; if (r0 < 0) r0 = 0;
    r1 = (y + BALL_H - 1 - BRICK_Y) / BRICK_H;
    if (r1 > BRICK_ROWS - 1) r1 = BRICK_ROWS - 1;

    for (r = r0; r <= r1; r++)
        for (c = c0; c <= c1; c++)
            if (cells[r * BRICK_COLS + c]) {
                cells[r * BRICK_COLS + c] = 0;
                bricks_left--;
                score += (BRICK_ROWS - r) * 10;   /* 30 / 20 / 10 */
                ball_vy = -ball_vy;
                /* Push the ball clear of the brick row it just hit. */
                if (ball_vy < 0)
                    ball_y = BRICK_Y + r * BRICK_H - BALL_H;
                else
                    ball_y = BRICK_Y + (r + 1) * BRICK_H;
                ball_fy = ball_y << 4;
                return 1;
            }
    return 0;
}

static void update_play(unsigned pad)
{
    int off, nx;

    /* Paddle. */
    if (pad & BTN_LEFT)
        paddle_x -= PADDLE_SPEED;
    if (pad & BTN_RIGHT)
        paddle_x += PADDLE_SPEED;
    if (paddle_x < 0)
        paddle_x = 0;
    if (paddle_x > SCREEN_W - PADDLE_W)
        paddle_x = SCREEN_W - PADDLE_W;

    /* Ball. */
    ball_fx += ball_vx;
    ball_fy += ball_vy;
    ball_x = ball_fx >> 4;
    ball_y = ball_fy >> 4;

    if (ball_x < 0) {
        ball_x = 0; ball_fx = 0;
        if (ball_vx < 0)
            ball_vx = -ball_vx;
    } else if (ball_x > SCREEN_W - BALL_W) {
        ball_x = SCREEN_W - BALL_W; ball_fx = ball_x << 4;
        if (ball_vx > 0)
            ball_vx = -ball_vx;
    }
    if (ball_y < 0) {
        ball_y = 0; ball_fy = 0;
        if (ball_vy < 0)
            ball_vy = -ball_vy;
    }

    if (ball_vs_bricks())
        goto bottom;

    /* Paddle bounce: english from where the ball lands on it. */
    if (ball_vy > 0 &&
        ball_y + BALL_H >= PADDLE_Y &&
        ball_y <= PADDLE_Y + PADDLE_H - 1 &&
        ball_x + BALL_W > paddle_x &&
        ball_x < paddle_x + PADDLE_W) {
        int speed;

        ball_y = PADDLE_Y - BALL_H;
        ball_fy = ball_y << 4;
        off = (ball_x + BALL_W / 2) - (paddle_x + PADDLE_W / 2);
        nx = off * 2;
        if (nx > MAX_VX)  nx = MAX_VX;
        if (nx < -MAX_VX) nx = -MAX_VX;
        if (nx >= 0 && nx <  MIN_VX) nx =  MIN_VX;
        if (nx <= 0 && nx > -MIN_VX) nx = -MIN_VX;
        ball_vx = nx;
        speed = ball_vy;                 /* still moving DOWN: it is +v */
        if (speed < MAX_VY)
            speed++;
        ball_vy = -speed;                /* ...so head back up */
    }

bottom:
    if (ball_y > SCREEN_H) {
        lives--;
        if (lives <= 0) {
            lives = 0;
            state = STATE_GAMEOVER;
        } else {
            serve_ball();
        }
    }

    /* All bricks gone: rebuild the field and keep playing. */
    if (bricks_left <= 0)
        fill_bricks();
}

/* ------------------------------------------------------------------ */
/* Drawing.                                                            */
static void draw(void)
{
    arcade1_clear(RGB(3, 7, 14));                       /* background   */
    arcade1_tilemap4(BRICK_X, BRICK_Y, cells, brick_tile,
                     BRICK_COLS, BRICK_ROWS, BRICK_W, BRICK_H);
    arcade1_sprite4(paddle_x, PADDLE_Y, paddle_img,
                    PADDLE_W, PADDLE_H, BANK_PADDLE, 0);
    if (state == STATE_PLAY)
        arcade1_sprite4(ball_x, ball_y, ball_img,
                        BALL_W, BALL_H, BANK_BALL, 0);

    /* HUD: score top-left, lives top-right.
     * draw_text advances 4*scale = 8 px per glyph and each glyph is 6 px
     * wide at scale 2, so "LIVES" spans 38 px: SCREEN_W-90 .. SCREEN_W-53.
     * The life boxes start at SCREEN_W-26-i*10, i.e. SCREEN_W-38 (434 on a
     * 480 px screen) — at the old SCREEN_W-74 the label ran under them and
     * the squares were painted over the last two glyphs. */
    draw_text(6, 6, 2, "SCORE", RGB(24, 26, 30));
    draw_number(50, 6, 2, score, RGB(31, 31, 10));
    draw_text(SCREEN_W - 90, 6, 2, "LIVES", RGB(24, 26, 30));
    {
        int i;
        for (i = 0; i < lives; i++)
            arcade1_rect(SCREEN_W - 26 - i * 10, 8, 6, 6, RGB(12, 24, 31));
    }

    if (state == STATE_GAMEOVER) {
        arcade1_rect(96, 100, 288, 76, RGB(1, 2, 6));
        arcade1_rect(96, 100, 288, 2,  RGB(31, 31, 31));
        arcade1_rect(96, 174, 288, 2,  RGB(31, 31, 31));
        draw_text(126, 112, 4, "GAME OVER", RGB(31, 8, 8));
        draw_text(122, 150, 2, "SCORE", RGB(20, 24, 30));
        draw_number(166, 150, 2, score, RGB(31, 31, 10));
    }

    /* The frame ends when the guest writes PRESENT; without this the machine
     * just burns its whole per-frame instruction budget and the game runs
     * hundreds of iterations ahead of the video (M29_LOG attempt 4). */
    arcade1_present();
}

/* ------------------------------------------------------------------ */
int main(void)
{
    make_assets();
    reset_game();

    for (;;) {
        unsigned pad = arcade1_pad();

        /* START = (re)start: rising edge only, so a held START is one game. */
        if ((pad & BTN_START) && !(pad_prev & BTN_START))
            reset_game();
        pad_prev = pad;

        if (state == STATE_PLAY)
            update_play(pad);

        draw();
        frame_no++;
    }
}
