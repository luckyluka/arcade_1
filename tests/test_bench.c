/* ARCADE-1 — voxel-experiment P0 harness: graphics primitive micro-benchmark.
 *
 * Boots programs/bench_gfx.c as firmware (arcade1_set_firmware) and runs two
 * identical 240-frame passes over its 8 benchmark phases (30 frames each,
 * phase selected by the guest from the frame timer). For every frame it
 * records:
 *
 *   - the GUEST cost: instructions retired (m.cpu.instret delta) — this is
 *     the deterministic, pinned number the experiment's estimates must be
 *     replaced by;
 *   - the HOST time: clock() delta — indicative only (host-dependent, NOT
 *     asserted), but it is what makes device-side pixel work visible:
 *     phases 6/7 paint 7.68M/2.30M pixels per frame in the device while the
 *     guest instruction count stays tiny.
 *
 * The boot frame (crt0 + cell/tile construction) is excluded from all
 * averages. Each frame is also hashed (state_hash) and pass 2 must reproduce
 * pass 1's exact hash sequence plus a byte-identical final framebuffer, so
 * the benchmark itself is deterministic.
 *
 * Reported per phase: avg inst/frame, and the ALL-IN per-op cost
 * (phase avg - baseline avg) / ops — loop overhead included, because that is
 * what a real game pays and what VOXEL_EXPERIMENT.md's "~4-6 inst per
 * guest pixel" estimate claims about.
 *
 * Usage: test_bench <bench_gfx.arc1|bin> <phase-symbol-address>
 */

#include "machine.h"    /* brings in memory/timer/video/audio/exec */
#include "rom.h"        /* M17: .arc1 image parsing */
#include "state_hash.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* --- Configuration (kept in sync with programs/bench_gfx.c by hand) ------ */

#define PHASES           8u
#define FRAMES_PER_PHASE 30u
#define TOTAL_FRAMES     (PHASES * FRAMES_PER_PHASE)   /* 240 per pass */

static const char *labels[PHASES] = {
    "baseline (present only)",
    "SET_PIXEL   x1000 on-screen",
    "SET_PIXEL   x1000 off-screen",
    "FILL 1x1    x1000 on-screen",
    "SPRITE 16x16 x200",
    "TILEMAP 20x15 full-screen x10",
    "CLEAR       full-screen x100",
    "FILL        full-screen x30",
};

static const unsigned ops[PHASES] = {
    0u, 1000u, 1000u, 1000u, 200u, 10u, 100u, 30u,
};

/* --- State ---------------------------------------------------------------- */

static arcade1_machine_t m;
static uint8_t  img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
static size_t   img_size;
static int32_t  sym_phase;

static uint16_t fb1[ARCADE1_FB_PIXELS];   /* pass-1 final framebuffer        */

static uint64_t hist1[1024];              /* pass-1 per-frame hashes         */
static unsigned nhist1, frame_count;
static bool     verify_hashes;

/* Per-pass accumulators (boot frame excluded). */
static uint64_t inst_sum[PHASES];
static uint64_t ticks_sum[PHASES];        /* clock() ticks                   */
static unsigned counts[PHASES];

/* --- Helpers --------------------------------------------------------------- */

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
    memset(inst_sum,  0, sizeof inst_sum);
    memset(ticks_sum, 0, sizeof ticks_sum);
    memset(counts,    0, sizeof counts);
}

/* --- One 240-frame measurement pass ---------------------------------------- */

static void pass(bool verify)
{
    unsigned f;

    verify_hashes = verify;
    boot_machine();

    for (f = 0; f < TOTAL_FRAMES; f++) {
        const uint64_t inst_before = m.cpu.instret;
        const clock_t t0 = clock();
        uint64_t delta, h;
        unsigned ph;
        clock_t t1;

        arcade1_run_frame(&m);
        t1 = clock();

        delta = m.cpu.instret - inst_before;

        /* The guest picks its phase from the frame timer at the top of each
         * iteration; after the frame the symbol must equal f/30. This pins
         * the guest/harness contract, so a bucketing bug can't silently
         * poison the numbers. */
        ph = (unsigned)guest32(sym_phase);
        assert(ph == f / FRAMES_PER_PHASE &&
               "guest phase must match f/FRAMES_PER_PHASE");

        if (f > 0u) {                       /* exclude the boot frame        */
            inst_sum[ph]  += delta;
            ticks_sum[ph] += (uint64_t)(t1 - t0);
            counts[ph]++;
        }

        h = state_hash(&m.cpu, &m.bus);
        if (verify_hashes) {
            assert(frame_count < nhist1 && "replay longer than original run");
            assert(h == hist1[frame_count] && "per-frame hash diverged");
        } else {
            assert(frame_count < sizeof hist1 / sizeof hist1[0]);
            hist1[frame_count] = h;
        }
        frame_count++;
    }

    assert(frame_count == TOTAL_FRAMES);
    assert(m.timer.count == frame_count && "timer == frames run");
    assert(m.video.presents == frame_count && "one present per frame");
    assert(m.bus.stray_accesses == 0 && "no stray accesses");
}

/* --- Report ---------------------------------------------------------------- */

static void report(void)
{
    const double baseline_inst = (double)inst_sum[0] / (double)counts[0];
    unsigned p;

    printf("%-28s %7s %11s %9s %11s %9s\n",
           "phase", "ops/fr", "inst/fr", "inst/op",
           "us/fr", "us/op");
    for (p = 0; p < PHASES; p++) {
        const double avg_inst = (double)inst_sum[p] / (double)counts[p];
        const double avg_us =
            (double)ticks_sum[p] * 1e6 / (double)CLOCKS_PER_SEC /
            (double)counts[p];
        const double per_op = ops[p] ? (avg_inst - baseline_inst) /
                                       (double)ops[p] : 0.0;
        const double per_op_us = ops[p]
            ? (avg_us - (double)ticks_sum[0] * 1e6 /
                        (double)CLOCKS_PER_SEC / (double)counts[0]) /
              (double)ops[p]
            : 0.0;

        printf("%1d %-26s %7u %11.1f %9.2f %11.1f %9.3f\n",
               p, labels[p], ops[p], avg_inst, per_op, avg_us, per_op_us);
    }
    printf("\ninst/op = all-in guest cost per operation (baseline subtracted,\n"
           "         loop overhead included). us/ = host time, indicative\n"
           "         only (this host, this C device), never asserted.\n");
}

int main(int argc, char **argv)
{
    FILE *f;
    long n;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <bench_gfx.arc1|bin> <phase-addr>\n",
                argv[0]);
        return 2;
    }

    f = fopen(argv[1], "rb");
    assert(f && "cannot open guest image");
    n = (long)fread(img, 1, sizeof img, f);
    assert(n > 0 && (size_t)n <= sizeof img && "image must fit buffer");
    fclose(f);
    img_size = (size_t)n;

    sym_phase = (int32_t)strtoul(argv[2], NULL, 16);

    printf("--- pass 1: measurement ---\n");
    pass(false);
    nhist1 = frame_count;
    memcpy(fb1, m.framebuffer, sizeof fb1);

    printf("--- pass 2: reset and replay ---\n");
    pass(true);
    assert(frame_count == nhist1 && "replay frame count diverged");
    assert(memcmp(fb1, m.framebuffer, sizeof fb1) == 0 &&
           "framebuffer diverged after reset");

    report();

    printf("voxel-P0: graphics micro-benchmark ok (%u frames x2, identical)\n",
           nhist1);
    printf("state hash: 0x%016llX\n",
           (unsigned long long)hist1[nhist1 - 1]);
    return 0;
}
