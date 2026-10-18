#include "arcade1.h"

/*
 * ARCADE-1 example game: ASTEROID RUN
 *
 * Controls:
 *   LEFT / RIGHT  - move
 *   A             - fire
 *   START         - restart after game over
 *
 * 480x270 RGB565.
 *
 * No assets, libc, floating point, or timing API required.
 * Everything is deterministic from the initial state.
 *
 * Lives in programs/experimental/: NOT built by default and NOT covered
 * by make check. Build it manually with `make experimental`.
 */

#define SCREEN_W 480
#define SCREEN_H 270

/* RGB565 colors */
#define BLACK       0x0000
#define WHITE       0xFFFF
#define RED         0xF800
#define GREEN       0x07E0
#define BLUE        0x001F
#define CYAN        0x07FF
#define YELLOW      0xFFE0
#define ORANGE      0xFD20
#define GRAY        0x8410
#define DARK_GRAY   0x4208
#define PURPLE      0x8010

#define MAX_ASTEROIDS 12
#define MAX_BULLETS    8
#define MAX_STARS     24

typedef struct {
    int x;
    int y;
    int size;
    int speed;
    int active;
} Asteroid;

typedef struct {
    int x;
    int y;
    int speed;
    int active;
} Bullet;

typedef struct {
    int x;
    int y;
    int speed;
} Star;

static Asteroid asteroids[MAX_ASTEROIDS];
static Bullet bullets[MAX_BULLETS];
static Star stars[MAX_STARS];

static int player_x;
static int player_y;

static int score;
static int lives;
static int frame;
static int game_over;

static unsigned int rng_state;

/* ------------------------------------------------------------------------- */
/* Deterministic pseudo-random generator                                     */
/* ------------------------------------------------------------------------- */

static unsigned int random_u32(void)
{
    /*
     * xorshift32.
     *
     * The initial seed is fixed, so the game is deterministic.
     */
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
/* Collision                                                                  */
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

/* ------------------------------------------------------------------------- */
/* Game initialization                                                        */
/* ------------------------------------------------------------------------- */

static void reset_game(void)
{
    int i;

    rng_state = 0x1A2B3C4Du;

    player_x = SCREEN_W / 2 - 8;
    player_y = SCREEN_H - 32;

    score = 0;
    lives = 3;
    frame = 0;
    game_over = 0;

    for (i = 0; i < MAX_BULLETS; i++)
        bullets[i].active = 0;

    /*
     * Stars are generated once from the deterministic seed.
     */
    for (i = 0; i < MAX_STARS; i++) {
        stars[i].x = random_range(SCREEN_W);
        stars[i].y = random_range(SCREEN_H);
        stars[i].speed = 1 + random_range(2);
    }

    /*
     * Stagger the initial asteroids.
     */
    for (i = 0; i < MAX_ASTEROIDS; i++) {
        asteroids[i].size = 10 + random_range(15);
        asteroids[i].x = random_range(SCREEN_W - asteroids[i].size);
        asteroids[i].y = -random_range(SCREEN_H);
        asteroids[i].speed = 1 + random_range(2);
        asteroids[i].active = 1;
    }
}

/* ------------------------------------------------------------------------- */
/* Bullets                                                                    */
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
                    a->x,
                    a->y,
                    a->size,
                    a->size)) {

                bullets[i].active = 0;

                /*
                 * Larger asteroids are worth more.
                 */
                score += a->size;

                /*
                 * Respawn the asteroid above the screen.
                 */
                a->size = 10 + random_range(15);
                a->x = random_range(SCREEN_W - a->size);
                a->y = -a->size - random_range(80);
                a->speed = 1 + random_range(3);

                break;
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Asteroids                                                                  */
/* ------------------------------------------------------------------------- */

static void update_asteroids(void)
{
    int i;

    for (i = 0; i < MAX_ASTEROIDS; i++) {
        Asteroid *a = &asteroids[i];

        if (!a->active)
            continue;

        a->y += a->speed;

        /*
         * Missed asteroid: recycle it above the screen.
         */
        if (a->y > SCREEN_H) {
            a->size = 10 + random_range(15);
            a->x = random_range(SCREEN_W - a->size);
            a->y = -a->size;
            a->speed = 1 + random_range(3);
            continue;
        }

        /*
         * Player collision.
         */
        if (overlap(
                player_x,
                player_y,
                16,
                14,
                a->x,
                a->y,
                a->size,
                a->size)) {

            lives--;

            a->size = 10 + random_range(15);
            a->x = random_range(SCREEN_W - a->size);
            a->y = -a->size - random_range(80);
            a->speed = 1 + random_range(3);

            if (lives <= 0)
                game_over = 1;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Stars                                                                      */
/* ------------------------------------------------------------------------- */

static void update_stars(void)
{
    int i;

    for (i = 0; i < MAX_STARS; i++) {
        stars[i].y += stars[i].speed;

        if (stars[i].y >= SCREEN_H) {
            stars[i].y = 0;
            stars[i].x = random_range(SCREEN_W);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Player                                                                     */
/* ------------------------------------------------------------------------- */

static void update_player(unsigned int pad)
{
    if (pad & BTN_LEFT)
        player_x -= 4;

    if (pad & BTN_RIGHT)
        player_x += 4;

    if (player_x < 4)
        player_x = 4;

    if (player_x > SCREEN_W - 20)
        player_x = SCREEN_W - 20;

    /*
     * Fire on a simple frame cadence while A is held.
     */
    if ((pad & BTN_A) && ((frame & 7) == 0))
        fire_bullet();
}

/* ------------------------------------------------------------------------- */
/* Rendering                                                                  */
/* ------------------------------------------------------------------------- */

static void draw_stars(void)
{
    int i;

    for (i = 0; i < MAX_STARS; i++) {
        int brightness = (i & 3);

        if (brightness == 0)
            gfx_rect(stars[i].x, stars[i].y, 1, 1, DARK_GRAY);
        else if (brightness == 1)
            gfx_rect(stars[i].x, stars[i].y, 1, 1, GRAY);
        else
            gfx_rect(stars[i].x, stars[i].y, 1, 1, WHITE);
    }
}

static void draw_player(void)
{
    /*
     * Small triangular-looking ship made from rectangles.
     * No sprite asset required.
     */
    gfx_rect(player_x + 6, player_y, 4, 3, WHITE);
    gfx_rect(player_x + 4, player_y + 3, 8, 3, CYAN);
    gfx_rect(player_x + 2, player_y + 6, 12, 4, CYAN);
    gfx_rect(player_x,     player_y + 10, 16, 4, BLUE);

    /*
     * Engine flame.
     */
    if ((frame & 3) != 0)
        gfx_rect(player_x + 5, player_y + 14, 6, 4, ORANGE);
    else
        gfx_rect(player_x + 6, player_y + 14, 4, 6, YELLOW);
}

static void draw_asteroids(void)
{
    int i;

    for (i = 0; i < MAX_ASTEROIDS; i++) {
        Asteroid *a = &asteroids[i];

        if (!a->active)
            continue;

        /*
         * Blocky asteroid.
         */
        gfx_rect(
            a->x + 3,
            a->y,
            a->size - 6,
            a->size,
            ORANGE);

        gfx_rect(
            a->x,
            a->y + 3,
            a->size,
            a->size - 6,
            ORANGE);

        /*
         * Dark "craters".
         */
        if (a->size >= 14) {
            gfx_rect(
                a->x + a->size / 3,
                a->y + a->size / 3,
                3,
                3,
                RED);
        }
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

/*
 * No text API exists yet, so the HUD uses graphical indicators:
 *
 *   top-left: lives
 *   top-right: score progress
 *
 * This is deliberately an example of what can already be done
 * without requiring a font system.
 */
static void draw_hud(void)
{
    int i;
    int score_width;

    /*
     * Lives.
     */
    for (i = 0; i < lives; i++) {
        gfx_rect(
            8 + i * 14,
            8,
            10,
            5,
            GREEN);
    }

    /*
     * Score bar.
     *
     * It wraps visually rather than requiring text.
     */
    score_width = score;

    if (score_width > SCREEN_W - 20)
        score_width = SCREEN_W - 20;

    gfx_rect(
        10,
        SCREEN_H - 8,
        score_width,
        3,
        YELLOW);
}

static void draw_game_over(void)
{
    /*
     * Large central "GAME OVER" style panel.
     *
     * Without fonts we use a strong visual indicator.
     */
    gfx_rect(150, 100, 180, 70, BLACK);
    gfx_rect(150, 100, 180, 4, RED);
    gfx_rect(150, 166, 180, 4, RED);

    gfx_rect(190, 120, 100, 8, RED);
    gfx_rect(180, 135, 120, 5, ORANGE);

    /*
     * Pulsing restart indicator.
     */
    if ((frame & 31) < 20)
        gfx_rect(210, 150, 60, 6, WHITE);
}

static void render(void)
{
    gfx_clear(BLACK);

    draw_stars();
    draw_asteroids();
    draw_bullets();
    draw_player();
    draw_hud();

    if (game_over)
        draw_game_over();

    gfx_present();
}

/* ------------------------------------------------------------------------- */
/* Main                                                                       */
/* ------------------------------------------------------------------------- */

int main(void)
{
    unsigned int previous_pad = 0;

    reset_game();

    while (1) {
        unsigned int pad = arcade1_pad();

        /*
         * Restart on START after game over.
         */
        if (game_over) {
            if ((pad & BTN_START) && !(previous_pad & BTN_START))
                reset_game();

            update_stars();
            render();

            previous_pad = pad;
            frame++;
            continue;
        }

        update_player(pad);
        update_bullets();
        update_asteroids();
        update_stars();

        render();

        previous_pad = pad;
        frame++;
    }

    return 0;
}