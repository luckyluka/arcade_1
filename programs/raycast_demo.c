/* ARCADE-1 voxel-experiment P2 guest — first-person heightmap raycast.
 *
 * Research code for VOXEL_EXPERIMENT.md phase P2: the experiment's primary
 * rendering candidate (approach #2, Comanche-style "voxel space") built for
 * real, so its budget stops being an estimate. It answers, with measurements:
 *
 *   - P1's handoff: the height-evaluation cost ALONE inside real code
 *     (phases 1/2 micro-measure it; phase 0 is the subtraction baseline);
 *   - a full 80x60-class first-person raycast frame's guest instruction cost
 *     and whether it stays far under the 1M frame cap (phases 3+);
 *   - whether the "stores nothing" thesis survives first-person rendering:
 *     this guest has NO world storage at all — heights are evaluated
 *     procedurally per ray step, camera pans over an infinite world.
 *
 * ALGORITHM (integer only — RV32IM has no FP, so determinism is structural):
 *
 *   80 columns x 60 internal rows, painted 4x4 screen pixels each (FILL is
 *   device-side; P0 measured one FILL = 10 guest inst regardless of size).
 *   Per column, march z with an adaptive step (1,2,4,8,16 then +8 up to 64):
 *       xw = camx + ((sx-40) * 2 * z) >> 8     fixed-point ray divergence,
 *                                              FOV ~34 degrees
 *       h  = ray_height(xw, z)                  2-octave value noise (NO
 *                                               storage, NO digest)
 *       y  = HORIZON + ((cam_h - h) * SCALE) / z  perspective projection,
 *                       cam_h = height at the camera + EYE — the eye flies
 *                       above the terrain, so near ground projects BELOW
 *                       the screen bottom and the front-to-back march sees
 *                       a proper ground plane instead of a wall at z=1
 *   Paint [y, y_bottom) as one 4-px-wide FILL, then y_bottom = y — classic
 *   front-to-back occlusion: each column is a stack of FILL runs, sky shows
 *   above the highest run (gfx_clear first).
 *
 * PHASES (selected from the frame timer, 30 frames each, like P0):
 *   0  baseline              present only (subtraction base)
 *   1  height3 micro         K x full 3-octave P1-style height (P1 handoff:
 *                            height cost WITHOUT the FNV digest)
 *   2  ray_height micro      K x the 2-octave function the raycast really
 *                            uses (the true per-step budget term)
 *   3+ raycast               full frame, camera pans 1 cell / 4 frames
 *
 * Exposes `phase` (harness buckets per-frame instruction deltas) and `sink`
 * (micro accumulator; must be non-zero so the work can't be optimised away).
 *
 * Operation counts / phase layout are duplicated in tests/test_ray.c.
 */

#include "gfx.h"

#define REG_TIMER (*(volatile unsigned int *)(ARCADE1_MMIO_BASE + 0x00u))

#define FR_PER_PHASE 30u
#define PHASSES      4u
#define PH_RAY       3u

#define RAYS    80       /* columns                                       */
#define ROWS    60       /* internal rows (x CH = 240 screen px)          */
#define CW      4        /* screen px per column (x RAYS = 320)           */
#define CH      4        /* screen px per internal row (x ROWS = 240)     */
#define HORIZON 36       /* internal row of the horizon                   */
#define SCALE   4        /* projection: y = HORIZON + (cam_h-h)*SCALE/z    */
#define EYE     40       /* camera height above the terrain at camx       */
#define ZMAX    64       /* far plane, world cells                        */
#define K_MICRO 600      /* evals per micro frame                         */

#define COL_SKY   0x569Fu
#define COL_WATER 0x1C7Fu
#define COL_SAND  0xEF5Cu
#define COL_GRASS 0x4E65u
#define COL_STONE 0xAD55u
#define COL_SNOW  0xEF7Du

/* --- Generator shared with P1's spec (same lattice/mix, no digest) -------- */

static unsigned int mix(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static unsigned int lattice(unsigned int oct, int cx, int cz)
{
    const unsigned int s = 0x5EED1234u ^ ((unsigned int)cx * 0x9E3779B9u)
                          ^ ((unsigned int)cz * 0x85EBCA77u)
                          ^ (oct * 0xC2B2AE35u);
    return mix(s) & 0xFFu;
}

static int interp(int v0, int v1, int u)
{
    const int d = v1 - v0;
    if (d >= 0)
        return v0 + ((d * u) >> 8);
    return v0 - (((-d) * u) >> 8);
}

static int noise(int wx, int wz, unsigned int oct)
{
    const int shift = 4 - (int)oct;
    const int s  = 1 << shift;
    const int x0 = wx / s, z0 = wz / s;
    const int fx = wx % s, fz = wz % s;
    const int u  = (fx * 255) >> shift;
    const int v  = (fz * 255) >> shift;

    return interp(interp((int)lattice(oct, x0,     z0),
                         (int)lattice(oct, x0 + 1, z0), u),
                  interp((int)lattice(oct, x0,     z0 + 1),
                         (int)lattice(oct, x0 + 1, z0 + 1), u), v);
}

/* The P1-style full height (3 octaves, no digest): measured in phase 1. */
static unsigned int height3(int wx, int wz)
{
    return (unsigned int)((noise(wx, wz, 0u) * 4 +
                           noise(wx, wz, 1u) * 2 +
                           noise(wx, wz, 2u)) / 7);
}

/* The raycast's per-step function: 2 octaves — cheaper, still smooth. The
 * result is stretched around its mean (128) so all five material bands
 * actually occur in a view; clamp keeps it in 0..255 (pure integer C,
 * truncating division, so still bit-exact on any conforming compiler). */
static unsigned int ray_height(int wx, int wz)
{
    const int raw = (noise(wx, wz, 0u) * 2 + noise(wx, wz, 1u)) / 3;
    int h = 128 + ((raw - 128) * 5) / 3;

    if (h < 0)
        h = 0;
    if (h > 255)
        h = 255;
    return (unsigned int)h;
}

static unsigned int color_for(unsigned int h)
{
    if (h <  64u) return COL_WATER;
    if (h <  80u) return COL_SAND;
    if (h < 150u) return COL_GRASS;
    if (h < 200u) return COL_STONE;
    return COL_SNOW;
}

/* --- Harness-visible state -------------------------------------------------- */

unsigned phase;     /* current phase (timer/30, capped at PH_RAY)            */
unsigned sink;      /* micro accumulator (non-zero proves the work ran)      */

static int camx;    /* world cell; pans during raycast phases                */

static void raycast(int cx)
{
    const int cam_h = (int)ray_height(cx, 0) + EYE;   /* eye above ground */
    int sx;

    gfx_clear(COL_SKY);

    for (sx = 0; sx < RAYS; sx++) {
        const int dx = (sx - (RAYS / 2)) * 2;   /* 256ths of a cell per z   */
        int y_bot = ROWS;
        int z = 1;

        while (z <= ZMAX && y_bot > 0) {
            const int xw = cx + ((dx * z) >> 8);
            const int h  = (int)ray_height(xw, z);
            const int y  = HORIZON + ((cam_h - h) * SCALE) / z;

            if (y < y_bot) {
                gfx_rect(sx * CW, y * CH, CW, (y_bot - y) * CH,
                         color_for((unsigned int)h));
                y_bot = y;
            }
            z = (z < 16) ? z * 2 : z + 8;      /* adaptive: dense near      */
        }
    }
}

int main(void)
{
    phase = 0u;
    sink  = 0u;
    camx  = 0;

    for (;;) {
        unsigned i;
        const unsigned t = REG_TIMER;

        phase = t / FR_PER_PHASE;
        if (phase >= PH_RAY)
            phase = PH_RAY;

        switch (phase) {
        case 0:                              /* baseline                   */
            break;
        case 1:                              /* full 3-octave height micro  */
            for (i = 0; i < K_MICRO; i++)
                sink += height3((int)(i & 63u), (int)((i >> 6) & 63u));
            break;
        case 2:                              /* raycast per-step micro      */
            for (i = 0; i < K_MICRO; i++)
                sink += ray_height((int)(i & 63u), (int)((i >> 6) & 63u));
            break;
        default:                             /* raycast + pan (1 cell/4fr)  */
            if ((t & 3u) == 0u)
                camx++;
            raycast(camx);
            break;
        }

        gfx_present();
    }
    return 0;
}
