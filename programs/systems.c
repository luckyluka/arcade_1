/* ARCADE-1 voxel-experiment P4 guest — game systems.
 *
 * Research code for VOXEL_EXPERIMENT.md phase P4: layers the systems a
 * real sandbox game needs on top of P3's interactive raycast, all in
 * software on the proven pipeline (no new hardware, per the experiment's
 * rule):
 *
 *   - INVENTORY   a dig yields 1 dirt block; a place consumes 1 (a place
 *                 with an empty inventory is REJECTED — no edit recorded);
 *   - CRAFTING    DOWN (edge) converts 3 dirt -> 1 stone (the one recipe);
 *   - ENTITIES    two slimes with 16.16 fixed-point patrol motion relative
 *                 to the camera (they stay in the frustum while it walks),
 *                 bouncing at patrol bounds; drawn as 1-bpp 8x8 sprites
 *                 over the terrain (no per-entity occlusion — documented
 *                 simplification, P4 is about systems, not depth);
 *   - DAY/NIGHT   tod = (timer/30) % 3: day / dusk / night. Dusk and night
 *                 darken the view with ONE full-screen TILEMAP of dither
 *                 tiles (per-cell tint), the no-blend "dithered 1-bpp
 *                 overlay" mechanism from section 5 — a full-screen SPRITE
 *                 would need 9.6 KB of ROM, the tilemap needs 64 B;
 *   - UI/HUD      inventory squares (sand = dirt, snow = stone) + a time
 *                 indicator, drawn last so everything sits on top.
 *
 * Same delta overlay, collision and render-derived edit targets as P3 —
 * but unlike P3, the watched-cell targets can SHIFT as edits change the
 * render's silhouette, so every action RECORDS the exact target it used
 * (act_tx/act_tz/act_ok/act_n) for the harness to replay independently.
 *
 * Exposes to the harness (nm): camx, digs, places, crafts, inv_dirt,
 * inv_stone, delta_count, mode (0 idle / 1 walk / 2 dig / 3 place /
 * 4 craft), py, tdigest, tod, e1x, e2x (absolute entity cells),
 * tgt1_x/tgt1_z/tgt2_x/tgt2_z, act_tx/act_tz/act_ok/act_n arrays.
 * Script and constants are duplicated in tests/test_systems.c.
 */

#include "gfx.h"

#define MMIO_BASE   ARCADE1_MMIO_BASE
#define REG_TIMER   (*(volatile unsigned int *)(MMIO_BASE + 0x00u))
#define REG_PAD     (*(volatile unsigned int *)(MMIO_BASE + 0x200u))

#define PAD_DOWN    0x02u
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

#define DELTA_MAX 16     /* linear-scan overlay table                     */
#define AMT       32     /* dig/place height delta                        */
#define CRAFT_COST 3u    /* dirt blocks per stone                         */
#define ACT_MAX   8      /* recorded actions (dig/place) for the harness  */

#define E1Z 24           /* slime depths: inside the frustum while the  */
#define E2Z 32           /*   camera walks (fan reach at z: ~7 / ~10)      */

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

/* --- The delta overlay ----------------------------------------------------- */

struct delta { int x, z; unsigned int h; };

/* Harness-visible state. */
unsigned camx;          /* world cell of the camera                       */
unsigned digs, places, crafts;    /* action counters                      */
unsigned inv_dirt, inv_stone;     /* inventory                            */
unsigned delta_count;   /* entries in the table                           */
unsigned mode;          /* 0 idle 1 walk 2 dig 3 place 4 craft           */
unsigned py;            /* terrain height at the camera (collision truth) */
unsigned tdigest;       /* FNV-1a 32 over the table, insertion order      */
unsigned tod;           /* 0 day / 1 dusk / 2 night (from the timer)      */
unsigned e1x, e2x;      /* absolute entity cells (camx + patrol offset)   */
unsigned tgt1_x, tgt1_z;  /* nearest visible ground cell of watched column */
unsigned tgt2_x, tgt2_z;  /*   (edited cells live HERE, on the silhouette) */

/* Per-action record for the harness: target actually used and whether the
 * place/dig was accepted (a rejected place records ok=0, no table edit).
 * Targets shift as edits move the silhouette, so replaying from these is
 * the only exact history. */
unsigned act_tx[ACT_MAX], act_tz[ACT_MAX], act_ok[ACT_MAX], act_n;

static struct delta deltas[DELTA_MAX];
static unsigned int prev_pad;
static unsigned int action;    /* alternates edited cells between tgt1/tgt2 */

/* Patrol state: 16.16 fixed-point offsets from the camera + direction. */
static long off1 = 2l << 16, off2 = 3l << 16;
static int  dir1 = 1, dir2 = 1;

static unsigned int h_at(int x, int z)
{
    unsigned int i;

    for (i = 0; i < delta_count; i++)
        if (deltas[i].x == x && deltas[i].z == z)
            return deltas[i].h;
    return ray_height(x, z);
}

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

static int cam_h_at(int cx) { return (int)h_at(cx, 0) + EYE; }

static void raycast(int cx)
{
    const int cam_h = cam_h_at(cx); /* eye above ground    */
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

/* --- Entities: two patrolling slimes (1-bpp sprites over the terrain) ----- */

static const unsigned char slime[16] = {
    0x3C, 0x00, 0x7E, 0x00, 0xFF, 0x00, 0xDB, 0x00,
    0xFF, 0x00, 0x7E, 0x00, 0x3C, 0x00, 0x00, 0x00,
};

/* Advance one patrol: +/-1/4 cell per frame, bounce at [lo, hi] (16.16). */
static void patrol(long *off, int *dir, long lo, long hi)
{
    *off += (long)*dir * 16384l;
    if (*off >= hi) { *off = hi; *dir = -1; }
    if (*off <= lo) { *off = lo; *dir =  1; }
}

/* Project an entity cell to screen and draw it. No per-entity occlusion:
 * P4 is about systems, not depth (documented in section 0.5). */
static void draw_slime(int cx, int ex, int ez, unsigned short color)
{
    const int sx = (RAYS / 2) + (((ex - cx) << 8) / (2 * ez));
    const int gh = (int)h_at(ex, ez);
    const int y  = HORIZON + ((cam_h_at(cx) - gh) * SCALE) / ez;

    gfx_sprite(sx * CW - 4, y * CH - 8, slime, 8, 8, color);
}

static void entities_frame(int cx)
{
    patrol(&off1, &dir1, 2l << 16, 6l << 16);
    patrol(&off2, &dir2, 3l << 16, 9l << 16);
    e1x = (unsigned)(cx + (int)(off1 >> 16));
    e2x = (unsigned)(cx + (int)(off2 >> 16));
    draw_slime(cx, (int)e1x, E1Z, 0xF81Fu);        /* magenta            */
    draw_slime(cx, (int)e2x, E2Z, 0xFD20u);        /* orange             */
}

/* --- Day/night: dithered full-screen TILEMAP overlay ----------------------- */

static unsigned int cells[300];                     /* 20x15 map, RAM    */
static const unsigned char dtiles[64] = {           /* 16x16 1-bpp:      */
    /* tile 1 (cell index 1): sparse checker = dusk */
    0xAA,0x55, 0x55,0xAA, 0xAA,0x55, 0x55,0xAA,
    0xAA,0x55, 0x55,0xAA, 0xAA,0x55, 0x55,0xAA,
    0xAA,0x55, 0x55,0xAA, 0xAA,0x55, 0x55,0xAA,
    0xAA,0x55, 0x55,0xAA, 0xAA,0x55, 0x55,0xAA,
    /* tile 2 (cell index 2): heavy dither = night */
    0xFF,0x55, 0xFF,0x55, 0x55,0xFF, 0x55,0xFF,
    0xFF,0x55, 0xFF,0x55, 0x55,0xFF, 0x55,0xFF,
    0xFF,0x55, 0xFF,0x55, 0x55,0xFF, 0x55,0xFF,
    0xFF,0x55, 0xFF,0x55, 0x55,0xFF, 0x55,0xFF,
};

static int last_tod = -1;

static void night_frame(void)
{
    unsigned int i;

    if ((int)tod == last_tod) {
        gfx_tilemap(0, 0, cells, dtiles, 20, 15, 16, 16);  /* redraw */
        return;
    }
    last_tod = (int)tod;
    {
        const unsigned int tint = (tod == 1u) ? 0x29C7u : 0x08C3u;
        const unsigned int idx  = (tod == 1u) ? 1u : 2u;
        for (i = 0; i < 300u; i++)
            cells[i] = idx | (tint << 16);
    }
    gfx_tilemap(0, 0, cells, dtiles, 20, 15, 16, 16);
}

/* --- HUD: inventory squares + time-of-day indicator ------------------------ */

static void draw_hud(void)
{
    static const unsigned short tod_color[3] = {
        0xFFE0u /* yellow day */, 0xFD20u /* orange dusk */,
        0x10C4u /* dark night */,
    };
    unsigned int i;

    for (i = 0; i < inv_dirt; i++)
        gfx_rect(4 + (int)(i * 9u), 4, 7, 7, 0xEF5Cu);   /* sand = dirt  */
    for (i = 0; i < inv_stone; i++)
        gfx_rect(64 + (int)(i * 9u), 4, 7, 7, 0xEF7Du);  /* snow = stone */
    gfx_rect(308, 4, 8, 8, tod_color[tod]);
}

/* --- Main ------------------------------------------------------------------- */

int main(void)
{
    camx = 0u;
    tdigest = 2166136261u;

    for (;;) {
        const unsigned int pad = REG_PAD;
        const unsigned int press_a = (pad & PAD_A) && !(prev_pad & PAD_A);
        const unsigned int press_b = (pad & PAD_B) && !(prev_pad & PAD_B);
        const unsigned int press_down = (pad & PAD_DOWN) &&
                                        !(prev_pad & PAD_DOWN);

        if (pad & (PAD_LEFT | PAD_RIGHT)) {
            mode = 1u;
            if (pad & PAD_LEFT) {
                if (camx > 0u)
                    camx--;
            } else {
                camx++;
            }
        } else if (press_a || press_b) {
            const int tx = (action & 1u) ? (int)tgt2_x : (int)tgt1_x;
            const int tz = (action & 1u) ? (int)tgt2_z : (int)tgt1_z;

            if (press_a) {
                mode = 2u;
                digs++;
                inv_dirt++;                        /* a dig yields dirt    */
                edit(tx, tz, -(int)AMT);
                if (act_n < ACT_MAX) {
                    act_tx[act_n] = (unsigned)tx;
                    act_tz[act_n] = (unsigned)tz;
                    act_ok[act_n] = 1u;
                    act_n++;
                }
            } else {
                mode = 3u;
                places++;
                if (inv_dirt > 0u) {               /* place consumes dirt  */
                    inv_dirt--;
                    edit(tx, tz, (int)AMT);
                    if (act_n < ACT_MAX) {
                        act_tx[act_n] = (unsigned)tx;
                        act_tz[act_n] = (unsigned)tz;
                        act_ok[act_n] = 1u;
                        act_n++;
                    }
                } else {                           /* REJECTED: no edit    */
                    if (act_n < ACT_MAX) {
                        act_tx[act_n] = (unsigned)tx;
                        act_tz[act_n] = (unsigned)tz;
                        act_ok[act_n] = 0u;
                        act_n++;
                    }
                }
            }
            action++;
            update_digest();
        } else if (press_down) {
            mode = 4u;
            if (inv_dirt >= CRAFT_COST) {          /* 3 dirt -> 1 stone    */
                inv_dirt -= CRAFT_COST;
                inv_stone++;
                crafts++;
            }
        } else {
            mode = 0u;
        }
        prev_pad = pad;

        tod = (REG_TIMER / 30u) % 3u;              /* 30 frames per stage */
        py = h_at((int)camx, 0);
        raycast((int)camx);
        entities_frame((int)camx);
        if (tod != 0u)
            night_frame();
        draw_hud();
        gfx_present();
    }
    return 0;
}
