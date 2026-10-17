#include "gfx.h"

#define W 480
#define H 270

int main(void)
{
    int x = 10, y = 10, dx = 3, dy = 2;
    while (1) {
        gfx_clear(0x0000);
        gfx_rect(x, y, 20, 20, 0xF800);
        /* present */
        GFX_REG_CMD = GFX_CMD_PRESENT;
        x += dx; y += dy;
        if (x < 0 || x + 20 > W) dx = -dx;
        if (y < 0 || y + 20 > H) dy = -dy;
    }
    return 0;
}
