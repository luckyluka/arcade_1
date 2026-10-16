/* ARCADE-1 milestone 14 guest program — draw with the graphics API.
 *
 * The point of milestone 14: a program running inside RV32 draws graphics
 * through the guest-facing API (gfx.h) and nothing else — no raw register
 * pokes in this file. The rectangles are chosen so the harness
 * (tests/test_gfx.c) can verify every pixel exactly:
 *
 *   gfx_clear(blue)               background
 *   gfx_rect(20, 30, 40, 10, red)     fully on-screen (400 px)
 *   gfx_rect(50, 35, 40, 10, green)   overlaps the red one (later wins)
 *   gfx_rect(310, 230, 30, 20, white) hangs off right+bottom -> clipped
 *   gfx_rect(-10, -5, 12, 8, black)   hangs off top+left (negative coords)
 *   gfx_present()                     one present, counted by the device
 *
 * Expected visible pixels: red 350 (400 minus the 50 green-covered),
 * green 400, white 100, black 6, blue 75944. Sum = 76800 = full screen.
 */

#include "gfx.h"

#define COLOR_BLUE  0x001Fu
#define COLOR_RED   0xF800u
#define COLOR_GREEN 0x07E0u
#define COLOR_WHITE 0xFFFFu
#define COLOR_BLACK 0x0000u

int main(void)
{
    gfx_clear(COLOR_BLUE);

    gfx_rect(20, 30, 40, 10, COLOR_RED);
    gfx_rect(50, 35, 40, 10, COLOR_GREEN);
    gfx_rect(310, 230, 30, 20, COLOR_WHITE);
    gfx_rect(-10, -5, 12, 8, COLOR_BLACK);

    gfx_present();
    return 0;
}
