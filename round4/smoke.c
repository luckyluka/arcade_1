/* smoke test: is the toolchain + capture pipeline alive? */
#include "arcade1.h"
#include "text.h"

static int x = 40;

int main(void)
{
    for (;;) {
        unsigned p = arcade1_pad();
        if (p & BTN_RIGHT)
            x += 3;
        arcade1_clear(0x1088);
        arcade1_rect(x, 120, 40, 40, 0xF800);
        arcade1_cam(0, 0);
        text_draw(8, 8, "SMOKE OK", 0xFFFF);
        arcade1_present();
    }
    return 0;
}
