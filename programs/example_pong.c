/* ARCADE-1 example game: PONG.
 *
 * Player paddle: UP/DOWN. The opponent is a simple AI that tracks the
 * ball. First to 5 wins; START restarts after game over.
 *
 * All game state is file-scope so the tools can observe it:
 * `arcade1 run ... --vars score,opp_score,state` reads it out of guest
 * RAM by name. One loop iteration = one 60 fps frame.
 */

#include "arcade1.h"

/* Ball position and velocity (pixels, pixels/frame). */
static int bx = 240, by = 135, dx = 4, dy = 3;

/* Player / opponent paddle top y (paddles are 6x40 at x=10 / x=464). */
static int py = 100, oy = 100;

/* Score: player / opponent. First to 5 sets `state` to game over. */
static int score = 0, opp_score = 0;

/* 0 = play, 1 = game over (START restarts). */
static int state = 0;

int main(void)
{
    while (1) {
        unsigned p = arcade1_pad();
        if (state == 0) {
            if (p & BTN_UP) py -= 4;
            if (p & BTN_DOWN) py += 4;
            if (py < 0) py = 0;
            if (py > 230) py = 230;
            /* opponent AI: centre on the ball, but a touch slower than the
             * ball's vertical speed — after a bounce it lags, so a rally
             * can be won */
            if (oy + 20 < by) oy += 2;
            else if (oy + 20 > by) oy -= 2;
            if (oy < 0) oy = 0;
            if (oy > 230) oy = 230;
            bx += dx;
            by += dy;
            if (by < 0 || by > 262) dy = -dy;
            if (bx < 20 && by >= py && by <= py + 40) dx = -dx;
            if (bx > 452 && by >= oy && by <= oy + 40) dx = -dx;
            if (bx < 0) {
                opp_score++;
                bx = 240; by = 135; dx = 4;
            }
            if (bx > 480) {
                score++;
                bx = 240; by = 135; dx = -4;
            }
            if (score >= 5 || opp_score >= 5) state = 1;
        } else {
            if (p & BTN_START) {
                score = 0; opp_score = 0; state = 0;
                bx = 240; by = 135;
            }
        }
        arcade1_clear(0x0000);
        arcade1_rect(10, py, 6, 40, 0xFFFF);
        arcade1_rect(464, oy, 6, 40, 0xFFFF);
        arcade1_rect(bx, by, 8, 8, 0xF800);
        arcade1_present();
    }
    return 0;
}
