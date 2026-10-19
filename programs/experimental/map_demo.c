/* map_demo — ARCADE-1 map-import experiment (ROADMAP_CREATION_FLOW Wave 4).
 *
 * Three levels compiled by tools/arc1_assetc.py from ASCII grids over one
 * four-tile tileset (tools/gen_map_tiles.py's PNG + mapdemo_tiles.json):
 *
 *   - a 4x3 map and a 5x1 map — the retracted "minimum geometry" fixtures.
 *     With the SDK compiler barriers (2d29b79) every geometry draws; this
 *     guest + tests/test_mapdemo.sh (IN make check) are the draw-proof that
 *     replaced the false validator (docs/experiments/
 *     tilemap-minsize-retraction.md).
 *   - a 10x3 field whose cells ALL point at ONE deduped tile (the M29
 *     brick lesson); the field's corner char names a byte-identical fourth
 *     tile, which the importer collapses into the same blob entry.
 *
 * Flips live in the cells ([20]/[21] in gfx.h): small51's 'q' is the same
 * blob tile as an unflipped marker, mirrored by the device at draw time.
 * The test pins exact painted-pixel counts per map region — nothing is
 * read back through globals.
 */
#include "asset.h"           /* the header names sprite4_ref — include first */
#include "mapdemo_tiles.h"

#define SKY ((4u << 11) | (12u << 5) | 28u)   /* dusk blue, (r5,g6,b5) */

int main(void)
{
    int i;

    for (i = 0; i < 16; i++)
        arcade1_pal(tiles_frames[0].bank * 16 + i, mapdemo_palette[i]);

    arcade1_clear(SKY);
    arcade1_tilemap4(16, 16, small43_cells, small43_tiles,
                     SMALL43_COLS, SMALL43_ROWS,
                     SMALL43_TILE_W, SMALL43_TILE_H);
    arcade1_tilemap4(16, 80, small51_cells, small51_tiles,
                     SMALL51_COLS, SMALL51_ROWS,
                     SMALL51_TILE_W, SMALL51_TILE_H);
    arcade1_tilemap4(16, 112, field_cells, field_tiles,
                     FIELD_COLS, FIELD_ROWS, FIELD_TILE_W, FIELD_TILE_H);
    arcade1_present();

    for (;;) { }
}
