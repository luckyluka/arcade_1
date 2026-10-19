/* ROADMAP_V3 card A6 probe: the 5x7 text font drawn to pixels a test can
 * check against a hand-written oracle, plus an arcade1_rand(3) histogram.
 *
 * Three 8x8 cells in one row (y=24): 'A' and '0' from text_draw("A0") at
 * x=16 and x=24, and text_u32's digit path drawing '0' at x=32. Background
 * black, glyphs white; camera pinned to 0,0 (SPRITE is world space and a
 * HUD must not scroll).
 *
 * randc0..randc2 count 3,000 arcade1_rand(3) calls made once at start-up;
 * the test reads them by name from the ELF's symbols and checks the
 * distribution against the card's 10-40 % band.
 */
#include "arcade1.h"
#include "text.h"

#define RGB(r, g, b) (unsigned)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31))

int randc0, randc1, randc2;

int main(void)
{
    int t;

    for (t = 0; t < 3000; t++) {
        int r = arcade1_rand(3);
        if (r == 0)      randc0++;
        else if (r == 1) randc1++;
        else             randc2++;
    }

    arcade1_cam(0, 0);
    for (;;) {
        arcade1_clear(RGB(0, 0, 0));
        text_draw(16, 24, "A0", RGB(31, 63, 31));
        text_u32(32, 24, 0u, 1, RGB(31, 63, 31));
        arcade1_present();
    }
}
