/* asset_demo — ARCADE-1 typed-accessor experiment (ROADMAP_CREATION_FLOW
 * Wave 2). A 16x16 four-frame hero sheet compiled by tools/arc1_assetc.py
 * from asset_demo_sheet.json + asset_demo_hero.png; the generated
 * asset_demo_sheet.h is committed (regenerate: `make asset-demo-sheet`).
 *
 * Everything the scene draws travels through the emitted per-frame refs
 * (sprite4_ref in programs/asset.h) — pixels, geometry, palette bank and
 * the origin/collision metadata declared in the manifest. No stride
 * arithmetic, no repeated w/h/bank. Findings: docs/experiments/
 * arc1-assetc.md, Wave 2 section.
 *
 * The four globals below are what the test reads back:
 *   arcade1 run asset_demo.arc1 --frames 3 --syms asset_demo.syms \
 *       --vars origin_x,origin_y,coll_w,coll_h --capture scene.ppm
 */
#include "asset.h"             /* sprite4_ref + asset_sprite4 (pulls arcade1.h) */
#include "asset_demo_sheet.h"  /* generated — must follow asset.h */

#define SCREEN_W 480
#define SCREEN_H 270
#define FEET_X   200           /* where the feet origin is pinned to stand */
#define FEET_Y   120
#define BOX_X    300           /* where the collision-box sprite is pinned */
#define BOX_Y    60
#define BLUE     31u           /* RGB565 of the manifest's #0000FF */

/* Metadata read back through the refs at startup — the test greps these to
 * prove assets.json reached the target. */
int origin_x, origin_y;        /* the feet anchor: manifest [8, 16] */
int coll_w, coll_h;            /* the collision box: manifest [14, 14] */

static void upload_palette(void)
{
    int i;

    for (i = 0; i < 16; i++)
        arcade1_pal(hero_frames[0].bank * 16 + i, hero_palette[i]);
}

static void draw(void)
{
    arcade1_clear(0);

    /* 1) a ref draws its own frame: the red block of frame 0 (sheet cols
     * 2..5) lands at screen (42..45, 42..45). */
    asset_sprite4(&hero_frames[0], 40, 40, 0);

    /* 2) flags travel through the wrapper: the same frame mirrored within
     * its 16 px — the red block lands at (110..113, 42..45). */
    asset_sprite4(&hero_frames[0], 100, 40, 1u);   /* bit0 = flip X */

    /* 3) per-frame refs address individual frames: frame 1's green block
     * (sheet cols 10..13) lands at (410..413, 42..45). */
    asset_sprite4(&hero_frames[1], 400, 40, 0);

    /* 4) placement by the ref's own origin: the feet anchor [8, 16] puts
     * frame 2's bottom bar (rows 14..15) directly on the feet line. */
    asset_sprite4(&hero_frames[2], FEET_X - origin_x, FEET_Y - origin_y, 0);

    /* 5) placement by the ref's own collision box: a debug fill behind
     * frame 3, whose white block then sits on top of it. */
    arcade1_rect(BOX_X + hero_frames[3].cx, BOX_Y + hero_frames[3].cy,
                 hero_frames[3].cw, hero_frames[3].ch, BLUE);
    asset_sprite4(&hero_frames[3], BOX_X, BOX_Y, 0);

    arcade1_present();
}

int main(void)
{
    origin_x = hero_frames[0].ox;
    origin_y = hero_frames[0].oy;
    coll_w   = hero_frames[0].cw;
    coll_h   = hero_frames[0].ch;

    upload_palette();

    for (;;)
        draw();
}
