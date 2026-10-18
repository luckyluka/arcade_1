/* ARCADE-1 example game: SNAKE.
 *
 * 48x27 grid of 10px cells. UP/DOWN/LEFT/RIGHT steer (no reversing into
 * yourself). Eat the green food to grow; hitting a wall or your own body
 * ends the game. START restarts after game over.
 *
 * All game state is file-scope so the tools can observe it:
 * `arcade1 run ... --vars length,score,state` reads it out of guest RAM
 * by name. One loop iteration = one 60 fps frame. */

#include "arcade1.h"

#define GW 48
#define GH 27

/* Body cells (grid coordinates); `length` cells, head first. */
static int sx[256], sy[256];
static int length = 3;

/* Current direction and the food cell. */
static int dx = 1, dy = 0;
static int fx = 10, fy = 5;

static int score = 0;
static int state = 0;   /* 0 = play, 1 = game over */

int main(void)
{
    sx[0] = 5; sy[0] = 5;
    sx[1] = 4; sy[1] = 5;
    sx[2] = 3; sy[2] = 5;

    while (1) {
        unsigned p = arcade1_pad();
        if (state == 0) {
            if (p & BTN_UP && dy == 0) { dx = 0; dy = -1; }
            if (p & BTN_DOWN && dy == 0) { dx = 0; dy = 1; }
            if (p & BTN_LEFT && dx == 0) { dx = -1; dy = 0; }
            if (p & BTN_RIGHT && dx == 0) { dx = 1; dy = 0; }
            /* move body */
            for (int i = length - 1; i > 0; i--) {
                sx[i] = sx[i - 1];
                sy[i] = sy[i - 1];
            }
            sx[0] += dx;
            sy[0] += dy;
            if (sx[0] < 0 || sx[0] >= GW || sy[0] < 0 || sy[0] >= GH) state = 1;
            for (int i = 1; i < length; i++)
                if (sx[i] == sx[0] && sy[i] == sy[0]) state = 1;
            if (sx[0] == fx && sy[0] == fy) {
                length++;
                if (length > 255) length = 255;
                score++;
                fx = (score * 7 + 3) % GW;
                fy = (score * 13 + 5) % GH;
            }
        } else {
            if (p & BTN_START) {
                length = 3;
                sx[0] = 5; sy[0] = 5;
                sx[1] = 4; sy[1] = 5;
                sx[2] = 3; sy[2] = 5;
                dx = 1; dy = 0;
                score = 0;
                state = 0;
                fx = 10; fy = 5;
            }
        }
        arcade1_clear(0x0000);
        for (int i = 0; i < length; i++)
            arcade1_rect(sx[i] * 10, sy[i] * 10, 10, 10, 0xFFFF);
        arcade1_rect(fx * 10, fy * 10, 10, 10, 0x07E0);
        arcade1_present();
    }
    return 0;
}
