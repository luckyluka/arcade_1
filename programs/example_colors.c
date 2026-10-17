#include "gfx.h"

int main(void)
{
    unsigned int c = 0;
    while (1) {
        gfx_clear(c);
        GFX_REG_CMD = GFX_CMD_PRESENT;
        c = (c + 0x0841) & 0xFFFF;
    }
    return 0;
}
