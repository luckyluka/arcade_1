/* ARCADE-1 voxel-experiment P3 guest — interaction: walk / dig / place.
 *
 * Research code for VOXEL_EXPERIMENT.md phase P3: the delta table is the
 * mechanism the whole "Little Digger" design rests on — base terrain is a
 * pure function (seed,x,z) so the world stores NOTHING, and player edits are
 * a small overlay. This guest wires P2's raycast to that overlay and drives
 * it from the pad, so the harness can script a real dig/build playthrough:
 *
 *   - WALK   pad LEFT/RIGHT moves the camera 1 cell/frame; the eye rides
 *            h_at(camx,0)+EYE, so collision is implicit (camera follows the
 *            terrain height, hills rise and fall under it);
 *   - DIG    A (edge-triggered) lowers h_at(target) by DIG_AMT, recording
 *            the edit in the delta table — the hole is VISIBLE because every
 *            ray step consults h_at;
 * - PLACE  B (edge-triggered) raises it the same way;
 *   - target cell = the render's OWN nearest-visible-ground cell of two
 *     watched columns (recorded during raycast, so edits land exactly on
 *     the silhouette and are visible by construction — a fixed (x, z) target
 *     cannot guarantee that: the camera may stand on a bump whose surrounds
 *     fall below the screen, putting any fixed near target geometrically
 *     off-screen, a finding P3 records in section 0.4).
 *
 * RENDERING: identical math to P2 (80 columns, adaptive z-march, 4-px FILL
 * runs, front-to-back occlusion) except every step's height comes from
 * h_at() = delta-table scan, else procedural. That scan is LINEAR — the
 * §1 design's mechanism — and P3 exists to measure what it costs per ray
 * step (the answer prices out the "~128 deltas" figure; see §0.4).
 *
 * Exposes to the harness (nm): camx, digs, places, delta_count, mode,
 * py (height under the camera = collision truth), tdigest (FNV-1a 32 over
 * the table in insertion order).
 *
 * Mode (0 idle / 1 walk / 2 dig / 3 place) is set from the pad each frame;
 * the harness buckets per-frame instruction deltas by it. Script, pad bits
 * and constants are duplicated in tests/test_sandbox.c.
 */

#include "gfx.h"

#define MMIO_BASE   ARCADE1_MMIO_BASE
#define REG_TIMER   (*(volatile unsigned int *)(MMIO_BASE + 0x00u))
#define REG_PAD     (*(volatile unsigned int *)(MMIO_BASE + 0x200u))

#define PAD_LEFT    0x04u
#define PAD_RIGHT   0x08u
#define PAD_A       0x10u
#define PAD_B       0x20u

#define RAYS    80       /* columns                                       */
#define ROWS    60       /* internal rows (x CH = 240 screen px)          */
#define CW      4        /* screen px per column (x RAYS = 320)           */
#define CH      4        /* screen px per internal row (x ROWS = 240)     */
#define HORIZON 36       /* internal row of the horizon                   */
#define SCALE   4        /* projection: y = HORIZON + (cam_h-h)*SCALE/z    */
#define EYE     40       /* camera height above the terrain at camx       */
#define ZMAX    64       /* far plane, world cells                        */

#define DELTA_MAX 16     /* linear-scan overlay table (measured cost)     */
#define AMT       32     /* dig/place height delta                        */

/* --- Generator: same spec as P1/P2 (2 octaves, stretched, no digest) ------ */

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
    if (h <  64u) return 0x1C7Fu;           /* water                     */
    if (h <  80u) return 0xEF5Cu;           /* sand                      */
    if (h < 150u) return 0x4E65u;           /* grass                     */
    if (h < 200u) return 0xAD55u;           /* stone                     */
    return 0xEF7Du;                         /* snow                      */
}

/* --- The delta overlay (P3's subject) -------------------------------------- */

struct delta { int x, z; unsigned int h; };

/* Harness-visible state. */
unsigned camx;          /* world cell of the camera                       */
unsigned digs, places;  /* action counters                                */
unsigned delta_count;   /* entries in the table                           */
unsigned mode;          /* 0 idle 1 walk 2 dig 3 place (harness buckets)  */
unsigned py;            /* terrain height at the camera (collision truth) */
unsigned tdigest;       /* FNV-1a 32 over the table, insertion order      */
unsigned tgt1_x, tgt1_z;  /* nearest visible ground cell of watched column */
unsigned tgt2_x, tgt2_z;  /*   (edited cells live HERE, on the silhouette) */

static struct delta deltas[DELTA_MAX];
static unsigned int prev_pad;
static unsigned int action;

/* Overlay lookup: linear scan of the table, else procedural. This is called
 * once per ray step (~880x/frame) — its cost is what P3 measures. */
static unsigned int h_at(int x, int z)
{
    unsigned int i;

    for (i = 0; i < delta_count; i++)
        if (deltas[i].x == x && deltas[i].z == z)
            return deltas[i].h;
    return ray_height(x, z);
}

/* Record (or update) one edit; adds DELTAs at the end, updates in place if
 * the cell was already edited. Table full = edit dropped (16 is a demo cap,
 * and P3's measurement prices out what a real cap can be). */
static void edit(int x, int z, int delta)
{
    unsigned int i;
    int h = (int)h_at(x, z) + delta;

    if (h < 0)
        h = 0;
    if (h > 255)
        h = 255;
    for (i = 0; i < delta_count; i++) {
        if (deltas[i].x == x && deltas[i].z == z) {
            deltas[i].h = (unsigned int)h;
            return;
        }
    }
    if (delta_count < DELTA_MAX) {
        deltas[delta_count].x = x;
        deltas[delta_count].z = z;
        deltas[delta_count].h = (unsigned int)h;
        delta_count++;
    }
}

static void update_digest(void)
{
    unsigned int i;
    unsigned int h = 2166136261u;                  /* FNV-1a 32 offset */

    for (i = 0; i < delta_count; i++) {
        h = (h ^ (unsigned int)deltas[i].x) * 16777619u;
        h = (h ^ (unsigned int)deltas[i].z) * 16777619u;
        h = (h ^ deltas[i].h) * 16777619u;
    }
    tdigest = h;
}

/* --- Renderer: P2's raycast with h_at() heights ---------------------------- */

#define TGT_COL  60                 /* watched columns: their first-paint  */
#define TGT_COL2 68                 /*   cells become the edit targets     */

static void raycast(int cx)
{
    const int cam_h = (int)h_at(cx, 0) + EYE;      /* eye above ground    */
    int sx;

    gfx_clear(0x569Fu);                            /* sky                 */

    for (sx = 0; sx < RAYS; sx++) {
        const int dx = (sx - (RAYS / 2)) * 2;      /* 256ths of a cell/z  */
        int y_bot = ROWS;
        int z = 1;

        while (z <= ZMAX && y_bot > 0) {
            const int xw = cx + ((dx * z) >> 8);
            const int h  = (int)h_at(xw, z);
            const int y  = HORIZON + ((cam_h - h) * SCALE) / z;

            if (y < y_bot) {
                if (y_bot == ROWS && sx == TGT_COL) {   /* first paint    */
                    tgt1_x = (unsigned)xw;
                    tgt1_z = (unsigned)z;
                }
                if (y_bot == ROWS && sx == TGT_COL2) {
                    tgt2_x = (unsigned)xw;
                    tgt2_z = (unsigned)z;
                }
                gfx_rect(sx * CW, y * CH, CW, (y_bot - y) * CH,
                         color_for((unsigned int)h));
                y_bot = y;
            }
            z = (z < 16) ? z * 2 : z + 8;          /* adaptive: dense near */
        }
    }
}

int main(void)
{
    camx = 0u;
    tdigest = 2166136261u;

    for (;;) {
        const unsigned int pad = REG_PAD;
        const unsigned int press_a = (pad & PAD_A) && !(prev_pad & PAD_A);
        const unsigned int press_b = (pad & PAD_B) && !(prev_pad & PAD_B);
        int tx, tz;

        if (pad & (PAD_LEFT | PAD_RIGHT)) {
            mode = 1u;
            if (pad & PAD_LEFT) {
                if (camx > 0u)
                    camx--;
            } else {
                camx++;
            }
        } else if (press_a || press_b) {
            mode = press_a ? 2u : 3u;
            if (action & 1u) {                  /* alternate the two cells */
                tx = (int)tgt2_x;
                tz = (int)tgt2_z;
            } else {
                tx = (int)tgt1_x;
                tz = (int)tgt1_z;
            }
            if (press_a) {
                digs++;
                edit(tx, tz, -(int)AMT);
            } else {
                places++;
                edit(tx, tz, (int)AMT);
            }
            action++;
            update_digest();
        } else {
            mode = 0u;
        }
        prev_pad = pad;

        py = h_at((int)camx, 0);
        raycast((int)camx);
        gfx_present();
    }
    return 0;
}
