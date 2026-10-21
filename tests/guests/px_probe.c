/* px_probe — draws one .px pixmap (ROADMAP_V4 card 7.3).
 *
 * The round trip the card asks for: a `.px` file -> the strip PNG ->
 * arc1-assetc -> this guest's header -> the machine's framebuffer. The
 * test (tests/test_art_pixmap.sh) counts the painted pixels and compares
 * them with the number of non-transparent characters in the .px file.
 *
 * The heart is drawn at (20, 20) on a black screen, so the painted
 * pixels are exactly the pixmap's opaque pixels.
 */
#include "asset.h"           /* the header names sprite4_ref — include first */
#include "assets.h"          /* the generated header (from the .px samples) */

#define AT_X 20
#define AT_Y 20

int main(void)
{
    for (;;) {
        int i;

        for (i = 0; i < 16; i++)
            arcade1_pal(heart_frames[0].bank * 16 + i, ui16_palette[i]);

        arcade1_clear(0x0000u);
        arcade1_sprite4(AT_X, AT_Y, heart_frames[0].data,
                        heart_frames[0].w, heart_frames[0].h,
                        heart_frames[0].bank, 0);
        arcade1_present();
    }
}
