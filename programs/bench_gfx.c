/* ARCADE-1 voxel-experiment P0 guest — graphics primitive micro-benchmark.
 *
 * Research code for VOXEL_EXPERIMENT.md phase P0: the experiment's estimates
 * ("a guest per-pixel loop costs ~4-6 instructions per pixel") must be
 * REPLACED by pinned measurements before any rendering approach is judged.
 * This guest measures the GUEST-SIDE cost (instructions retired) and lets the
 * harness measure host time of each video primitive as the games actually use
 * it — through gfx.h.
 *
 * How it works: one fixed workload per phase, 30 frames per phase, phases
 * selected from the frame timer (1 tick/frame, power-on enabled), 8 phases =
 * 240 frames per pass:
 *
 *   0  baseline        no draw commands (just present) — the subtraction base
 *   1  SET_PIXEL x1000 on-screen      (the path a per-pixel guest loop uses)
 *   2  SET_PIXEL x1000 off-screen     (clipped before any framebuffer write)
 *   3  FILL 1x1 x1000 on-screen       (device-side pixel write per call)
 *   4  SPRITE 16x16 x200              (1-bpp blit round-robin over 4 tiles)
 *   5  TILEMAP 20x15 full-screen x10  (worst case: all 300 cells visible)
 *   6  CLEAR full-screen x100         (device paints 76,800 px per command)
 *   7  FILL full-screen x30           (same pixel count via FILL)
 *
 * Phases 6/7 make the host/device work VISIBLE IN WALL-CLOCK TIME while the
 * guest instruction count stays small — that contrast (guest budget vs device
 * cost) is exactly what the experiment needs to see.
 *
 * The cell array (20x15 x 4 B = 1,200 B of RAM) is built once in the boot
 * frame, which the harness excludes from all averages.
 *
 * Operation counts are duplicated in tests/test_bench.c (kept in sync by
 * hand, as elsewhere in this repo).
 */

#include "gfx.h"

#define REG_TIMER (*(volatile unsigned int *)(0x4000u + 0x00u))

#define FRAMES_PER_PHASE 30u   /* keep in sync with tests/test_bench.c */
#define PHASES           8u

#define N_PIXEL    1000u
#define N_OFFPIX   1000u
#define N_FILL1    1000u
#define N_SPRITE   200u
#define N_TILEMAP  10u
#define N_CLEAR    100u
#define N_FULLFILL 30u

#define MAP_COLS 20
#define MAP_ROWS 15

/* The current phase; the harness reads this symbol out of RAM (via nm) to
 * bucket each frame's instruction delta into the right column. */
unsigned phase;

/* Four 16x16 1-bpp tiles (2 B/row, MSB = leftmost), built once in RAM:
 *   1 solid  2 box  3 checker  4 cross */
static unsigned char tiles[4][32];

/* The tilemap: all cells non-empty so every device fetch happens (worst
 * case). RAM-resident because a C const initializer can't compute it. */
static unsigned int cells[MAP_COLS * MAP_ROWS];

static const unsigned short palette[6] = {
    0xF800u, 0x07E0u, 0x001Fu, 0xFFE0u, 0xF81Fu, 0x07FFu,
};

static void build_data(void)
{
    static const unsigned char box[16] = {
        0xFF,0xFF, 0x80,0x01, 0x80,0x01, 0x80,0x01,
        0x80,0x01, 0x80,0x01, 0x80,0x01, 0xFF,0xFF,
    };
    unsigned i, r;

    for (r = 0; r < 16u; r++) {
        /* 1: solid */
        tiles[0][r * 2u]      = 0xFFu;
        tiles[0][r * 2u + 1u] = 0xFFu;
        /* 2: box (rows 0/15 solid, middles sides) */
        tiles[1][r * 2u]      = box[(r < 2u || r > 13u) ? 0u : 2u];
        tiles[1][r * 2u + 1u] = box[(r < 2u || r > 13u) ? 1u : 3u];
        /* 3: checker */
        tiles[2][r * 2u]      = (r & 1u) ? 0xAAu : 0x55u;
        tiles[2][r * 2u + 1u] = (r & 1u) ? 0xAAu : 0x55u;
        /* 4: cross */
        tiles[3][r * 2u]      = (r & 1u) ? 0x18u : 0x00u;
        tiles[3][r * 2u + 1u] = (r & 1u) ? 0x18u : 0x00u;
    }

    for (i = 0; i < MAP_COLS * MAP_ROWS; i++) {
        unsigned idx = 1u + (i & 3u);
        cells[i] = idx | ((unsigned int)palette[(i >> 2) % 6u] << 16);
    }
}

int main(void)
{
    unsigned i;

    build_data();

    for (;;) {
        phase = (REG_TIMER / FRAMES_PER_PHASE) % PHASES;

        switch (phase) {
        case 0:                              /* baseline: present only */
            break;

        case 1:                              /* SET_PIXEL on-screen      */
            for (i = 0; i < N_PIXEL; i++) {
                GFX_REG_X     = i & 255u;                 /* 0..255   */
                GFX_REG_Y     = (i >> 4) & 127u;          /* 0..127   */
                GFX_REG_COLOR = i & 0xFFFFu;
                GFX_REG_CMD   = GFX_CMD_SET_PIXEL;
            }
            break;

        case 2:                              /* SET_PIXEL off-screen     */
            for (i = 0; i < N_OFFPIX; i++) {
                GFX_REG_X     = 320u + (i & 63u);         /* >= width  */
                GFX_REG_Y     = (i >> 4) & 127u;
                GFX_REG_COLOR = i & 0xFFFFu;
                GFX_REG_CMD   = GFX_CMD_SET_PIXEL;
            }
            break;

        case 3:                              /* FILL 1x1 on-screen       */
            for (i = 0; i < N_FILL1; i++)
                gfx_rect((int)(i & 255u), (int)((i >> 4) & 127u),
                         1, 1, 0xFFFFu);
            break;

        case 4:                              /* SPRITE 16x16             */
            for (i = 0; i < N_SPRITE; i++)
                gfx_sprite((int)(i & 255u), (int)((i >> 2) & 127u),
                           tiles[i & 3u], 16, 16, 0xFFFFu);
            break;

        case 5:                              /* TILEMAP full-screen      */
            for (i = 0; i < N_TILEMAP; i++)
                gfx_tilemap(0, 0, cells, tiles, MAP_COLS, MAP_ROWS, 16, 16);
            break;

        case 6:                              /* CLEAR x100               */
            for (i = 0; i < N_CLEAR; i++)
                gfx_clear(0x001Fu);
            break;

        default: case 7:                     /* FILL full-screen x30     */
            for (i = 0; i < N_FULLFILL; i++)
                gfx_rect(0, 0, 320, 240, 0x001Fu);
            break;
        }

        gfx_present();
    }
    return 0;
}
