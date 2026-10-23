/* experiments/hd/scene_body.h — H3's scene, shared by both guests.
 *
 * ONE body, two instantiations. scene_480.c and scene_960.c each define
 * SCREEN_W, SCREEN_H, SCENE_SCALE and SCENE_ASSETS and then include this
 * file. Every LAYOUT number below is written in 480x270 scene units and
 * multiplied by SCENE_SCALE; every element's SIZE comes from its asset ref,
 * which is already at that scene's pixel density. So the two captures are
 * the same composition by construction — the only thing that differs is the
 * art's density (HD_EXPERIMENT.md card H3).
 *
 * What the guest sees, mechanically:
 *   scene_480 runs on the default machine (ARCADE1_WIDTH 480) — logical
 *   grid == physical framebuffer == 480x270.
 *   scene_960 runs on build/arcade1-geo960, built with
 *   -DARCADE1_WIDTH=960 -DARCADE1_HEIGHT=540 and ARCADE1_HD_SCALE left at
 *   its default of 1 — logical grid == physical framebuffer == 960x540, so
 *   the block expander is NOT in the path: every draw is one real pixel and
 *   the guest coordinates are simply 0..959 x 0..539. The SDK clips nothing
 *   (programs/gfx.h has no geometry in it); the device clips to its own
 *   ARCADE1_WIDTH/HEIGHT. The two scenes differ only in what the video
 *   device was compiled with and in SCENE_SCALE.
 *
 * Composition (scene units, x SCALE):
 *   sky       a 17-band gradient over the whole screen, 16 px per band
 *   ridge     the parallax layer at 1/4 speed, drawn at y 36 — one strip
 *             wide per screen (240 px at scale 1, 480 at scale 2)
 *   ground    4 terrain cells wide (x SCALE): 4 x 1 row at 480, 8 x 2 at
 *             960 — the same footprint four 32x32 tiles would have covered.
 *             Uniform runs of level1.txt: grass_top over dirt, never a
 *             repeated grass row.
 *   hero      the rig animator's idle frame, feet on the ground
 *   pickup    one coin, to the hero's right
 *   HUD       "SCORE" + a 3-digit number, top left
 *
 * Two elements are deliberately 1x in BOTH scenes, and
 * docs/experiments/hd-art.md says so rather than pretending otherwise:
 *   - the TILES: gen_art.py draws them at absolute per-feature coordinates,
 *     so 32x32 means re-authoring detail, not changing a parameter — over
 *     the card's one-hour escape hatch. The ground's CELL count scales with
 *     SCALE (4x1 at 480, 8x2 at 960) so its footprint is the same share of
 *     the screen; the tiles drawn inside it are the same 16x16 art at both.
 *   - the PICKUP: not in the card's 2x list, so it is the control element —
 *     the same 12x12 coin in both scenes.
 */

#include "arcade1.h"
#include "gfx.h"
#include "asset.h"
#include "text.h"
#include SCENE_ASSETS

/* ------------------------------------------------------------------------ */
/* palettes — identical banks in both scenes (build_scene_art.py pins them), */
/* so both guests make literally the same upload calls                        */
/* ------------------------------------------------------------------------ */

static void upload_palettes(void)
{
    gfx_pal_upload(0,          hero16_palette,  16);   /* the hero  (bank 0) */
    gfx_pal_upload(3 * 16,     terrain_palette, 16);   /* the tiles  (bank 3) */
    gfx_pal_upload(4 * 16,     ridge_palette,   16);   /* the ridge  (bank 4) */
    gfx_pal_upload(8 * 16,     fx_palette,      16);   /* the coin   (bank 8) */
}

/* ------------------------------------------------------------------------ */
/* the sky: petalblade's own 6-stop ramp, lerped across the whole screen    */
/* ------------------------------------------------------------------------ */

static const unsigned sky_bands[6] = {
    ((3u << 11) | (8u << 5) | 6u),     /* deep indigo */
    ((5u << 11) | (6u << 5) | 8u),
    ((9u << 11) | (13u << 5) | 10u),
    ((15u << 11) | (14u << 5) | 10u),
    ((22u << 11) | (20u << 5) | 9u),
    ((29u << 11) | (36u << 5) | 11u),  /* warm gold */
};

#define SKY_BANDS  17
#define SKY_BAND_H (16 * SCENE_SCALE)
#define SKY_STEPS  (SKY_BANDS - 1)

static unsigned lerp565(unsigned a, unsigned b, int num, int den)
{
    int ar = (int)((a >> 11) & 31u), ag = (int)((a >> 5) & 63u),
        ab = (int)(a & 31u);
    int br = (int)((b >> 11) & 31u), bg = (int)((b >> 5) & 63u),
        bb = (int)(b & 31u);
    int r = ar + (br - ar) * num / den;
    int g = ag + (bg - ag) * num / den;
    int bl = ab + (bb - ab) * num / den;
    return (unsigned)((r << 11) | (g << 5) | bl);
}

static void draw_sky(void)
{
    int j;
    for (j = 0; j < SKY_BANDS; j++) {
        int      seg  = j * 5 / SKY_STEPS;
        int      frac = (j * 5) % SKY_STEPS;
        unsigned c    = (seg >= 5) ? sky_bands[5]
                       : lerp565(sky_bands[seg], sky_bands[seg + 1], frac,
                                 SKY_STEPS);
        arcade1_rect(0, j * SKY_BAND_H, SCREEN_W, SKY_BAND_H, c);
    }
}

/* ------------------------------------------------------------------------ */
/* layout, in scene units x SCENE_SCALE                                     */
/* ------------------------------------------------------------------------ */

/* The ground. Tile art is 16x16 at BOTH densities (it cannot be regenerated
 * at 32x32 — see the file header), so the ground is sized as a FOOTPRINT:
 * 4 cells x 1 row of 16x16 at 480 is 64x16 scene px — literally the card's
 * "4 tiles" — and 8 cells x 2 rows at 960 is 128x32, exactly the footprint
 * four 32x32 tiles would have covered, which is what the card's "tiles at
 * twice the pixel size" would have drawn. The footprint is therefore the
 * same 13.3% x 5.9% of the screen at either density (area x4, as H2
 * predicts), and neither scene repeats a tile vertically to fake it: row 0
 * is grass_top, and only the row BELOW it (present at 960 only) is dirt.
 *
 * The cells come from uniform runs of level1.txt rather than one contiguous
 * window, because the level's staircase edge (rows 9..11: "##dd", "###d",
 * "#f##") changes the grass:dirt mix the moment the cell count doubles —
 * that was the first version of this scene, and it is exactly the confound
 * the review calls out. So:
 *   grass_top  row  8, cols 115..   (uniform run of "#")
 *   dirt       row 13, cols 12..     (uniform run of "d") */
#define GROUND_CELLS  (4 * SCENE_SCALE)
#define GROUND_ROWS   (SCENE_SCALE)
#define GROUND_W      (LEVEL1_TILE_W * GROUND_CELLS)
#define GROUND_X      ((SCREEN_W - GROUND_W) / 2)
#define GROUND_Y      (224 * SCENE_SCALE)

#define RIDGE_Y       (36 * SCENE_SCALE)

#define COIN_DX       (30 * SCENE_SCALE)
#define COIN_DY       (64 * SCENE_SCALE)

#define HUD_X         (8 * SCENE_SCALE)
#define HUD_Y         (8 * SCENE_SCALE)
#define HUD_LINE      (12 * SCENE_SCALE)

/* ------------------------------------------------------------------------ */
/* the 2x HUD font                                                          */
/* ------------------------------------------------------------------------ */

/* The font has NO generator — programs/text.h is a hand-authored 5x7 table
 * and a shared SDK header, so it is not touched here. The 2x HUD rasterises
 * the same data as 2x2 blocks at draw time: no new art, no change to the
 * SDK, and the glyph design (and therefore its readability) is identical by
 * construction. Cell advance doubles with it, so two HUD lines stay two
 * HUD lines at either density. */
#if SCENE_SCALE != 1

#define HUD_CELL (8 * SCENE_SCALE)

static void hud_glyph(int x, int y, unsigned ch, unsigned color)
{
    const unsigned char *g = &arcade1_text_font[(ch - 0x20u) * 8u];
    int                  row, col;
    for (row = 0; row < 7; row++) {
        unsigned bits = g[row];
        for (col = 0; col < 5; col++)
            if (bits & (0x80u >> col))
                arcade1_rect(x + col * (HUD_CELL / 8),
                             y + row * (HUD_CELL / 8),
                             HUD_CELL / 8, HUD_CELL / 8, color);
    }
}

static void hud_str(int x, int y, const char *s, unsigned color)
{
    for (; *s; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch >= 0x20u && ch <= 0x5Fu)
            hud_glyph(x, y, ch, color);
        x += HUD_CELL;
    }
}

static void hud_num(int x, int y, unsigned value, int digits, unsigned color)
{
    char buf[2];
    buf[1] = '\0';
    for (int i = digits - 1; i >= 0; i--) {
        buf[0] = (char)('0' + (int)(value % 10u));
        hud_str(x + i * HUD_CELL, y, buf, color);
        value /= 10u;
    }
}

#else /* SCENE_SCALE == 1: the SDK's own 5x7 at its native size */

#define HUD_CELL 8

static void hud_str(int x, int y, const char *s, unsigned color)
{
    text_draw(x, y, s, color);
}

static void hud_num(int x, int y, unsigned value, int digits, unsigned color)
{
    text_u32(x, y, value, digits, color);
}

#endif

/* ------------------------------------------------------------------------ */
/* the layers                                                               */
/* ------------------------------------------------------------------------ */

/* One seamless strip tiled across the screen with petalblade's parallax
 * factor (num/den = 1/4). The scene is static, so the camera never moves
 * and the factor never bites; the call is kept exactly as the game makes it
 * so the element really is "one parallax ridge", not a pasted picture. */
static void draw_layer(const sprite4_ref *fr, int num, int den, int yoff)
{
    int W = fr->w;
    int L = 0;                      /* the camera, fixed: this is a still   */
    int start = L * num / den;
    int sx;
    if (start < 0)
        start += W;
    sx = -start;
    while (sx < SCREEN_W) {
        asset_sprite4(fr, sx, yoff, 0);
        sx += W;
    }
}

static void draw_ground(void)
{
    int i;

    for (i = 0; i < GROUND_ROWS; i++) {
        int grass = (i == 0);        /* row 0 is grass_top, never repeated */
        int row   = grass ? 8 : 13;
        int col   = grass ? 115 : 12;
        int off   = row * LEVEL1_COLS + col;
        arcade1_tilemap4(GROUND_X, GROUND_Y + i * LEVEL1_TILE_H,
                         &level1_cells[off], level1_tiles, GROUND_CELLS, 1,
                         LEVEL1_TILE_W, LEVEL1_TILE_H);
    }
}

static void draw_scene(void)
{
    int hw = samurai_idle_frames[0].w;
    int hh = samurai_idle_frames[0].h;

    arcade1_cam(0, 0);              /* the camera register is per-frame     */
    arcade1_clear(sky_bands[0]);
    draw_sky();
    draw_layer(&ridge_frames[0], 1, 4, RIDGE_Y);
    draw_ground();

    /* feet on the ground, centred on it */
    asset_sprite4(&samurai_idle_frames[0],
                  GROUND_X + (GROUND_W - hw) / 2, GROUND_Y - hh, 0);

    /* the pickup: 12x12 in BOTH scenes — the control element */
    asset_sprite4(&coin_frames[0], GROUND_X + GROUND_W + COIN_DX,
                  GROUND_Y - COIN_DY, 0);

    hud_str(HUD_X, HUD_Y, "SCORE", 0xFFFFu);
    hud_num(HUD_X, HUD_Y + HUD_LINE, 123u, 3, 0xFFFFu);
    arcade1_present();
}

int main(void)
{
    upload_palettes();
    for (;;)
        draw_scene();
    return 0;
}
