/* ARCADE-1 voxel-experiment P1 guest — deterministic terrain generator.
 *
 * Research code for VOXEL_EXPERIMENT.md phase P1: the entire "Little Digger"
 * concept depends on base terrain being a pure function (seed, x, z) so the
 * world stores NOTHING (only player edits live in RAM). This guest is that
 * function, and it must produce identical results forever — in the guest
 * (RV32IM, -Os), in the harness's independent C model (host), and across
 * hosts. This is the same cross-ISA determinism concern as M34 conformance.
 *
 * ALGORITHM (spec — tests/test_terrain.c mirrors it exactly, kept in sync by
 * hand; the test ALSO pins a golden digest so drift in either copy is caught):
 *
 *   mix(x)      splitmix32-style finalizer (all u32 wraparound arithmetic)
 *   lattice(s)  mix(s ^ x*0x9E3779B9 ^ z*0x85EBCA77 ^ oct*0xC2B2AE35) & 0xFF
 *   value noise at (wx, wz) for octave `oct` with spacing s (16/8/4):
 *       u = (frac * 255) >> log2(s)          fixed-point weight 0..255
 *       bilinear over 4 lattice corners, interpolated with an explicit
 *       non-negative-shift form so signed `>>` on negatives (implementation
 *       defined) is never used — plain C arithmetic only, identical on
 *       any conforming compiler
 *   height      (n0*4 + n1*2 + n2) / 7        3 octaves, 0..255
 *   material    height bands: <64 water, <80 sand, <150 grass, <200 stone,
 *               else snow (5 block types for P1)
 *   digest      FNV-1a 64 over (x, z, height, material), scan order
 *
 * Frame protocol: ONE ROW PER FRAME (64 cells), 64 frames to fill the
 * 64x64 grid, then idle-presents. This mirrors how a real game would
 * generate terrain incrementally instead of stalling a frame — and it keeps
 * each frame (~10k instructions) far under the 1M frame cap.
 *
 * Exposes to the harness (nm): `state` (rows done), `digest_lo/hi`,
 * `sample[64]` (row z=0, for per-cell diagnostics on mismatch).
 */

#include "gfx.h"

#define GRID 64                       /* 64x64 columns checked per pass     */
#define SEED 0x5EED1234u

/* --- The generator spec ---------------------------------------------------- */

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
    unsigned int s = SEED ^ ((unsigned int)cx * 0x9E3779B9u)
                          ^ ((unsigned int)cz * 0x85EBCA77u)
                          ^ (oct * 0xC2B2AE35u);
    return mix(s) & 0xFFu;
}

/* Linear interpolate v0 -> v1 with weight u (0..255), avoiding shifts of
 * negative values (implementation-defined in C). */
static int interp(int v0, int v1, int u)
{
    const int d = v1 - v0;
    if (d >= 0)
        return v0 + ((d * u) >> 8);
    return v0 - (((-d) * u) >> 8);
}

/* Value noise at integer (wx, wz), octave `oct`, spacing s = 16>>oct. */
static int noise(int wx, int wz, unsigned int oct)
{
    const int shift = 4 - (int)oct;              /* s = 16, 8, 4            */
    const int s  = 1 << shift;
    const int x0 = wx / s, z0 = wz / s;
    const int fx = wx % s, fz = wz % s;
    const int u  = (fx * 255) >> shift;
    const int v  = (fz * 255) >> shift;
    const int v00 = (int)lattice(oct, x0,     z0);
    const int v10 = (int)lattice(oct, x0 + 1, z0);
    const int v01 = (int)lattice(oct, x0,     z0 + 1);
    const int v11 = (int)lattice(oct, x0 + 1, z0 + 1);

    return interp(interp(v00, v10, u), interp(v01, v11, u), v);
}

static unsigned int height_at(int wx, int wz)
{
    const int n0 = noise(wx, wz, 0u);
    const int n1 = noise(wx, wz, 1u);
    const int n2 = noise(wx, wz, 2u);
    return (unsigned int)((n0 * 4 + n1 * 2 + n2) / 7);
}

static unsigned int material_at(unsigned int h)
{
    if (h <  64u) return 0u;                      /* water                   */
    if (h <  80u) return 1u;                      /* sand                    */
    if (h < 150u) return 2u;                      /* grass                   */
    if (h < 200u) return 3u;                      /* stone                   */
    return 4u;                                    /* snow                    */
}

/* --- Harness-visible state -------------------------------------------------- */

unsigned state;            /* rows generated, 0..GRID (GRID = done)         */
unsigned digest_lo;        /* FNV-1a 64 result, latched continuously        */
unsigned digest_hi;
unsigned char sample[GRID]; /* row z=0 heights, for per-cell diagnostics     */

/* FNV-1a 64: one 64-bit step over one byte per call. The prime
 * 0x100000001B3 = 2^40 + 2^8 + 0xB3 lets GCC fold h *= prime into inline
 * shifts/adds — no libgcc (the guest links -nostdlib). */
#define FNV_OFFSET_LO 0x84222325u   /* low  32 bits of the 64-bit basis */
#define FNV_OFFSET_HI 0xCBF43926u   /* high 32 bits: 0xcbf4392684222325  */

static unsigned int fnv_lo;
static unsigned int fnv_hi;

static void fnv_byte(unsigned int b)
{
    unsigned long long h = ((unsigned long long)fnv_hi << 32) | fnv_lo;
    h = (h ^ b) * 0x100000001B3ULL;
    fnv_lo = (unsigned int)h;
    fnv_hi = (unsigned int)(h >> 32);
}

/* Generate row `z`. */
static void gen_row(int z)
{
    int x;

    for (x = 0; x < GRID; x++) {
        const unsigned int h  = height_at(x, z);
        const unsigned int mt = material_at(h);

        if (z == 0)
            sample[x] = (unsigned char)h;
        fnv_byte((unsigned int)x);
        fnv_byte((unsigned int)z);
        fnv_byte(h);
        fnv_byte(mt);
    }
    state = (unsigned)z + 1u;
    digest_lo = fnv_lo;
    digest_hi = fnv_hi;
}

int main(void)
{
    state = 0u;
    fnv_lo = FNV_OFFSET_LO;
    fnv_hi = FNV_OFFSET_HI;
    digest_lo = fnv_lo;
    digest_hi = fnv_hi;

    for (;;) {
        if (state < (unsigned)GRID)
            gen_row((int)state);                  /* one row per frame      */
        gfx_present();
    }
    return 0;
}
