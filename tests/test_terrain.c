/* ARCADE-1 — voxel-experiment P1 harness: deterministic terrain generator.
 *
 * Boots programs/terrain_gen.c as firmware (arcade1_set_firmware), runs it
 * until it has generated its 64x64 grid (one row per frame), and checks the
 * result THREE ways — the same cross-ISA determinism the whole "Little
 * Digger" concept rests on (pure function (seed,x,z) -> height,material, so
 * the world stores nothing):
 *
 *   1. independent model  this file re-implements the generator spec (mix /
 *                         lattice / interp / noise / height / material, and
 *                         the FNV-1a-64 digest) as plain host C and compares
 *                         the guest's row-0 heights cell-by-cell AND the
 *                         full 64x64 digest — RV32IM -Os guest vs host
 *                         compiler must agree exactly;
 *   2. pinned golden       row-0 sample + digest also compared against
 *                         hard-coded constants, so drift in BOTH copies
 *                         (an accidental spec edit) is still caught;
 *   3. reset-and-compare   the whole run happens twice; the per-frame state
 *                         hash sequence and the final framebuffer must be
 *                         identical (guest is deterministic frame by frame,
 *                         not just at the end).
 *
 * Also asserts the frame protocol stays honest while generating: one
 * present/timer tick per frame, no stray accesses, and generation finishes
 * within its frame budget (64 rows + boot, well under the 1M cap — this
 * amortises terrain work the way a real game would).
 *
 * Usage: test_terrain <terrain_gen.arc1|bin> <state> <digest_lo>
 *                     <digest_hi> <sample-addr>
 */

#include "machine.h"    /* brings in memory/timer/video/audio/exec */
#include "rom.h"        /* M17: .arc1 image parsing */
#include "state_hash.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* --- Constants mirrored from programs/terrain_gen.c (kept in sync) -------- */

#define GRID 64
#define SEED 0x5EED1234u

/* Pinned golden values (observational run first, then pinned): the row-0
 * height of column 0, and the full 64x64 FNV-1a-64 digest low/high words.
 * These pin the SPEC itself — if someone edits the algorithm in both the
 * guest and the model below, these fail. Observed digest
 * 0x626115CD2B9A0257 (row0[0] = 178) at P1. */
#define GOLDEN_SAMPLE0  178u
#define GOLDEN_DIGEST_LO 0x2B9A0257u
#define GOLDEN_DIGEST_HI 0x626115CDu

/* --- Independent model of the generator spec -------------------------------- */

static unsigned model_mix(unsigned x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static unsigned model_lattice(unsigned oct, int cx, int cz)
{
    const unsigned s = SEED ^ ((unsigned)cx * 0x9E3779B9u)
                             ^ ((unsigned)cz * 0x85EBCA77u)
                             ^ (oct * 0xC2B2AE35u);
    return model_mix(s) & 0xFFu;
}

static int model_interp(int v0, int v1, int u)
{
    const int d = v1 - v0;
    if (d >= 0)
        return v0 + ((d * u) >> 8);
    return v0 - (((-d) * u) >> 8);
}

static int model_noise(int wx, int wz, unsigned oct)
{
    const int shift = 4 - (int)oct;
    const int s  = 1 << shift;
    const int x0 = wx / s, z0 = wz / s;
    const int fx = wx % s, fz = wz % s;
    const int u  = (fx * 255) >> shift;
    const int v  = (fz * 255) >> shift;
    const int v00 = (int)model_lattice(oct, x0,     z0);
    const int v10 = (int)model_lattice(oct, x0 + 1, z0);
    const int v01 = (int)model_lattice(oct, x0,     z0 + 1);
    const int v11 = (int)model_lattice(oct, x0 + 1, z0 + 1);

    return model_interp(model_interp(v00, v10, u),
                        model_interp(v01, v11, u), v);
}

static unsigned model_height(int wx, int wz)
{
    const int n0 = model_noise(wx, wz, 0u);
    const int n1 = model_noise(wx, wz, 1u);
    const int n2 = model_noise(wx, wz, 2u);
    return (unsigned)((n0 * 4 + n1 * 2 + n2) / 7);
}

static unsigned model_material(unsigned h)
{
    if (h <  64u) return 0u;
    if (h <  80u) return 1u;
    if (h < 150u) return 2u;
    if (h < 200u) return 3u;
    return 4u;
}

/* FNV-1a 64 over (x, z, height, material), scan order — mirrors the guest. */
static void model_digest(unsigned char *row0_out,
                         uint64_t *digest_out)
{
    uint64_t h = UINT64_C(0xcbf4392684222325);
    int x, z;

    for (z = 0; z < GRID; z++) {
        for (x = 0; x < GRID; x++) {
            const unsigned ht = model_height(x, z);
            const unsigned mt = model_material(ht);
            if (z == 0)
                row0_out[x] = (unsigned char)ht;
            h = (h ^ (unsigned)x) * UINT64_C(0x100000001B3);
            h = (h ^ (unsigned)z) * UINT64_C(0x100000001B3);
            h = (h ^ ht)        * UINT64_C(0x100000001B3);
            h = (h ^ mt)        * UINT64_C(0x100000001B3);
        }
    }
    *digest_out = h;
}

/* --- Guest state ----------------------------------------------------------- */

static arcade1_machine_t m;
static uint8_t  img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
static size_t   img_size;
static int32_t  sym_state, sym_dlo, sym_dhi, sym_sample;

static uint16_t fb1[ARCADE1_FB_PIXELS];
static uint64_t hist1[512];
static unsigned nhist1, frame_count;
static bool     verify_hashes;
static uint64_t gen_total_inst, gen_max_inst;   /* terrain-gen CPU load     */

static uint32_t guest32(int32_t addr)
{
    return arcade1_read32(&m.bus, (uint32_t)addr);
}

static void boot_machine(void)
{
    const void *rom = img;
    size_t rom_size = img_size;

    if (arcade1_image_recognized(img, img_size)) {
        arcade1_image_t parsed;
        assert(arcade1_image_parse(img, img_size, &parsed) ==
               ARCADE1_IMAGE_OK);
        rom = parsed.payload;
        rom_size = parsed.payload_size;
    }

    arcade1_init(&m);
    arcade1_reset(&m);
    assert(arcade1_load_rom(&m.bus, rom, (uint32_t)rom_size) == rom_size);
    arcade1_set_firmware(&m, true);

    frame_count = 0;
    gen_total_inst = gen_max_inst = 0;
}

/* Run until the guest has generated every row (state == GRID). */
static void run_to_done(void)
{
    const unsigned start = frame_count;

    while (guest32(sym_state) < (uint32_t)GRID) {
        const uint64_t before = m.cpu.instret;
        uint64_t h, delta;

        assert(frame_count - start < GRID + 8u &&
               "terrain must finish within its row budget");

        arcade1_run_frame(&m);

        delta = m.cpu.instret - before;
        gen_total_inst += delta;
        if (delta > gen_max_inst)
            gen_max_inst = delta;

        h = state_hash(&m.cpu, &m.bus);
        if (verify_hashes) {
            assert(frame_count < nhist1 && "replay longer than original");
            assert(h == hist1[frame_count] && "per-frame hash diverged");
        } else {
            assert(frame_count < sizeof hist1 / sizeof hist1[0]);
            hist1[frame_count] = h;
        }
        frame_count++;
    }
}

static void scenario(bool verify)
{
    unsigned char row0[GRID];
    unsigned char model_row0[GRID];
    uint64_t model_d;
    uint64_t guest_d;
    unsigned i;
    int bad = -1;

    verify_hashes = verify;
    boot_machine();
    run_to_done();

    /* 1. per-cell: guest row 0 vs independent model. */
    model_digest(model_row0, &model_d);
    for (i = 0; i < GRID; i++)
        row0[i] = (unsigned char)arcade1_read8(&m.bus,
                                               (uint32_t)sym_sample + i);
    for (i = 0; i < GRID; i++) {
        if (row0[i] != model_row0[i]) {
            bad = (int)i;
            break;
        }
    }
    if (bad >= 0) {
        fprintf(stderr, "FAIL: row0[%d] guest=%u model=%u\n",
                bad, row0[bad], model_row0[bad]);
        assert(!"guest terrain row 0 diverged from independent model");
    }

    /* 2. full 64x64 digest: guest vs model. */
    guest_d = ((uint64_t)guest32(sym_dhi) << 32) | guest32(sym_dlo);
    if (guest_d != model_d) {
        fprintf(stderr, "FAIL: digest guest=%016llX model=%016llX\n",
                (unsigned long long)guest_d,
                (unsigned long long)model_d);
        assert(!"guest 64x64 digest diverged from independent model");
    }

    /* print the observed values FIRST so an observational run shows them
     * even if the golden pins below fail (that is how they get pinned) */
    printf("terrain: 64x64 in %u frames, digest=0x%016llX, row0[0]=%u\n",
           frame_count, (unsigned long long)guest_d, row0[0]);
    printf("terrain cpu load: %llu inst/frame avg, %llu max (%u rows)\n",
           (unsigned long long)(gen_total_inst / frame_count),
           (unsigned long long)gen_max_inst, frame_count);
    fflush(stdout);   /* observe pins even if the golden asserts below abort */

    /* 3. golden pins (the spec itself). */
    assert(row0[0] == GOLDEN_SAMPLE0 && "row0[0] golden drifted (spec edit?)");
    assert(guest32(sym_dlo) == GOLDEN_DIGEST_LO && "digest lo golden drifted");
    assert(guest32(sym_dhi) == GOLDEN_DIGEST_HI && "digest hi golden drifted");

    /* 4. frame protocol honesty during generation. */
    assert(m.timer.count == frame_count && "timer == frames run");
    assert(m.video.presents == frame_count && "one present per frame");
    assert(m.bus.stray_accesses == 0 && "no stray accesses");
}

int main(int argc, char **argv)
{
    FILE *f;
    long n;

    if (argc != 6) {
        fprintf(stderr,
                "usage: %s <image> <state> <digest_lo> <digest_hi> "
                "<sample>\n", argv[0]);
        return 2;
    }

    f = fopen(argv[1], "rb");
    assert(f && "cannot open guest image");
    n = (long)fread(img, 1, sizeof img, f);
    assert(n > 0 && (size_t)n <= sizeof img);
    fclose(f);
    img_size = (size_t)n;

    sym_state  = (int32_t)strtoul(argv[2], NULL, 16);
    sym_dlo    = (int32_t)strtoul(argv[3], NULL, 16);
    sym_dhi    = (int32_t)strtoul(argv[4], NULL, 16);
    sym_sample = (int32_t)strtoul(argv[5], NULL, 16);

    printf("--- pass 1: generate ---\n");
    scenario(false);
    nhist1 = frame_count;
    memcpy(fb1, m.framebuffer, sizeof fb1);

    printf("--- pass 2: reset and regenerate ---\n");
    scenario(true);
    assert(frame_count == nhist1 && "replay frame count diverged");
    assert(memcmp(fb1, m.framebuffer, sizeof fb1) == 0 &&
           "framebuffer diverged after reset");

    printf("voxel-P1: terrain generator ok (%u frames x2, identical)\n",
           nhist1);
    printf("state hash: 0x%016llX\n",
           (unsigned long long)hist1[nhist1 - 1]);
    return 0;
}
