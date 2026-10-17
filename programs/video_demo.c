/* ARCADE-1 milestone 13 guest program — drive the video device.
 *
 * The point of milestone 13: a guest program controls the screen through the
 * machine's video device. This program issues all three commands over MMIO:
 *
 *   CLEAR      — fill the screen with red, wiping whatever was there
 *   SET_PIXEL  — draw a green diagonal, a blue corner pixel, and one
 *                deliberately out-of-range pixel (must be clipped, not crash)
 *   PRESENT    — acknowledge the frame
 *
 * The harness (tests/test_video.c) runs this image on the CPU, then checks the
 * machine's framebuffer (the exact RGB565 buffer Libretro scans out), the
 * device's register readbacks, and the present counter — which is only
 * possible if every access went guest -> bus -> MMIO -> video device.
 *
 * Register offsets are window-relative and must match the pinned register
 * table in docs.md (see src/arcade1/video.h).
 */

#include "arcade1_map.h"

#define MMIO_BASE   ARCADE1_MMIO_BASE
#define VIDEO_BASE  (MMIO_BASE + 0x100u)

#define REG_X      (*(volatile unsigned int *)(VIDEO_BASE + 0x00))
#define REG_Y      (*(volatile unsigned int *)(VIDEO_BASE + 0x04))
#define REG_COLOR  (*(volatile unsigned int *)(VIDEO_BASE + 0x08))
#define REG_CMD    (*(volatile unsigned int *)(VIDEO_BASE + 0x0C))

#define CMD_CLEAR     0x1u
#define CMD_SET_PIXEL 0x2u
#define CMD_PRESENT   0x3u

#define COLOR_RED    0xF800u
#define COLOR_GREEN  0x07E0u
#define COLOR_BLUE   0x001Fu
#define COLOR_WHITE  0xFFFFu

static void plot(unsigned int x, unsigned int y, unsigned int color)
{
    REG_X     = x;
    REG_Y     = y;
    REG_COLOR = color;
    REG_CMD   = CMD_SET_PIXEL;
}

int main(void)
{
    /* CLEAR: wipe the screen to red. */
    REG_COLOR = COLOR_RED;
    REG_CMD   = CMD_CLEAR;

    /* Green diagonal (0,0)..(3,3). */
    for (unsigned int i = 0; i < 4; i++)
        plot(i, i, COLOR_GREEN);

    /* Blue pixel in the far corner. */
    plot(319, 239, COLOR_BLUE);

    /* Out-of-range plot: must be clipped silently by the device. Its
     * register values must still read back afterwards. */
    plot(500, 600, COLOR_WHITE);

    /* PRESENT: acknowledge the frame (counted by the device). */
    REG_CMD = CMD_PRESENT;

    return 0;
}
