#include "gfx.h"

#define REG_INPUT_PAD (GFX_MMIO_BASE + 0x200u)
#define MMIO_READ(a) (*(volatile unsigned int *)(a))

static unsigned pad(void){ return MMIO_READ(REG_INPUT_PAD); }

int main(void)
{
    int bx=240, by=135, dx=4, dy=3;
    int py=100;
    while (1) {
        unsigned p = pad();
        if (p & 0x1) py -= 4; /* up */
        if (p & 0x2) py += 4; /* down */
        if (py < 0) py = 0;
        if (py > 270-40) py = 270-40;
        bx += dx; by += dy;
        if (bx < 20) { dx = -dx; }
        if (bx > 480-20) { dx = -dx; }
        if (by < 0 || by > 270-8) dy = -dy;
        gfx_clear(0x0000);
        gfx_rect(10, py, 6, 40, 0xFFFF);
        gfx_rect(bx, by, 8, 8, 0xF800);
        GFX_REG_CMD = GFX_CMD_PRESENT;
    }
    return 0;
}
