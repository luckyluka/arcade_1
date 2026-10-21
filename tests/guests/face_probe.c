/* face_probe — a rig body with a .px face on it (ROADMAP_V4, the library).
 *
 * The rig's shapes are ellipses, capsules and polygons, so it CANNOT draw
 * a face: the rig's head is a blank ellipse. The fix is composition — the
 * rig gives the body, a `.px` pixmap gives the face, and the game draws
 * one over the other.
 *
 * This guest draws two rigged characters with a face on each: the
 * samurai and the skeleton. The test checks that the faces' EYES are on
 * the heads (the rig's head alone has none).
 */

#include "asset.h"           /* the header names sprite4_ref — include first */
#include "assets.h"

/* Where each face goes. The rig does not publish its head's position, so
 * these are the numbers the style sheet records:
 *
 *   the samurai: the head ellipse is 11x12 centred at (14, 9), and the
 *                kabuto covers its top, so the visible skin is rows
 *                8..15 and the 8x8 face goes at (10, 8);
 *   the skeleton: the head ellipse is 9x9 centred at (12, 7), so the
 *                face goes at (8, 3).
 */
#define SAMURAI_X 0
#define SAMURAI_FACE_X 10
#define SAMURAI_FACE_Y 8

#define SKELETON_X 60
#define SKELETON_FACE_X 8
#define SKELETON_FACE_Y 3

int main(void)
{
    for (;;) {
        int i;

        /* the palettes: each sprite carries the bank of its own palette */
        for (i = 0; i < 16; i++)
            arcade1_pal(samurai_walk_frames[0].bank * 16 + i,
                        hero16_palette[i]);
        for (i = 0; i < 16; i++)
            arcade1_pal(skeleton_walk_frames[0].bank * 16 + i,
                        enemy16_palette[i]);

        arcade1_clear(0x0000u);
        /* the bodies: the rigs' walk cycles */
        asset_sprite4(&samurai_walk_frames[0], SAMURAI_X, 0, 0);
        asset_sprite4(&skeleton_walk_frames[0], SKELETON_X, 0, 0);
        /* the faces: the pixmaps, over the heads */
        asset_sprite4(&face_samurai_frames[0],
                      SAMURAI_X + SAMURAI_FACE_X, SAMURAI_FACE_Y, 0);
        asset_sprite4(&face_skeleton_frames[0],
                      SKELETON_X + SKELETON_FACE_X, SKELETON_FACE_Y, 0);
        arcade1_present();
    }
    return 0;
}
