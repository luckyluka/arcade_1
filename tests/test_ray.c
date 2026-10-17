/* ARCADE-1 — voxel-experiment P2 harness: first-person heightmap raycast.
 *
 * Boots programs/raycast_demo.c as firmware (arcade1_set_firmware) and runs
 * two identical 210-frame passes over its 4 phases (30 frames each for 0..2,
 * the rest raycast; the guest picks its phase from the frame timer). This is
 * the phase that stops VOXEL_EXPERIMENT.md's central rendering estimates from
 * being estimates:
 *
 *   - phases 1/2 measure the height-evaluation cost ALONE inside real code
 *     (P1's handoff question: phase 1 = full 3-octave height without the FNV
 *     digest, phase 2 = the 2-octave function the raycast actually calls),
 *     baseline-subtracted per eval;
 *   - phase 3+ measures a FULL 80x60-class raycast frame: guest instructions
 *     (deterministic, pinned) and host time (indicative only), proving the
 *     frame stays far under the 1M instruction frame cap;
 *   - framebuffer property checks prove the render is real: sky present,
 *     >=4 of the 6 palette colours used, and terrain variation across a
 *     mid-screen row (a blank/black frame fails);
 *   - reset-and-compare determinism: the per-frame state-hash sequence and
 *     the final framebuffer must be identical across passes.
 *
 * The boot frame (crt0 + init) is excluded from all averages. The guest's
 * `phase` symbol is re-read after every frame and asserted against f/30 so a
 * bucketing bug cannot silently poison the numbers.
 *
 * Usage: test_ray <raycast_demo.arc1|bin> <phase> <sink>
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

/* --- Configuration (kept in sync with programs/raycast_demo.c by hand) ----- */

#define FR_PER_PHASE 30u
#define PH_RAY       3u
#define PHASSES      4u
#define MICRO_EVALS  600u
#define TOTAL_FRAMES (3u * FR_PER_PHASE + 120u)   /* 210 per pass */

#define RAYS 80
#define ROWS 60
#define CH   4

#define COL_SKY   0x569Fu
#define COL_WATER 0x1C7Fu
#define COL_SAND  0xEF5Cu
#define COL_GRASS 0x4E65u
#define COL_STONE 0xAD55u
#define COL_SNOW  0xEF7Du

static const char *labels[PHASSES] = {
    "baseline (present only)",
    "height3 micro (3-octave)",
    "ray_height micro (2-octave)",
    "raycast 80x60 + pan",
};

/* Pinned golden values (observational run first, then pinned): the micro
 * accumulator after all 60 micro frames, the per-frame state hash of the
 * final frame, and the exact raycast average — guest instruction counts are
 * fully deterministic, so pinning them pins the whole workload. Observed at
 * P2: sink 0x003322AA, raycast 406,563 inst/frame, hash 0xF0FB04DA25A0DFE9. */
#define GOLDEN_SINK        0x003322AAu
#define GOLDEN_STATE_HASH  UINT64_C(0xF0FB04DA25A0DFE9)
#define GOLDEN_RAY_INST    406563u

/* --- State ---------------------------------------------------------------- */

static arcade1_machine_t m;
static uint8_t  img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
static size_t   img_size;
static int32_t  sym_phase, sym_sink;

static uint16_t fb1[ARCADE1_FB_PIXELS];
static uint64_t hist1[512];
static unsigned nhist1, frame_count;
static bool     verify_hashes;

static uint64_t inst_sum[PHASSES];
static uint64_t ticks_sum[PHASSES];   /* clock() ticks, indicative only */
static unsigned counts[PHASSES];

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

/* --- One 210-frame measurement pass ---------------------------------------- */

static void pass(bool verify)
{
    unsigned f;

    verify_hashes = verify;
    boot_machine();

    for (f = 0; f < TOTAL_FRAMES; f++) {
        const uint64_t inst_before = m.cpu.instret;
        const clock_t t0 = clock();
        uint64_t delta, h;
        unsigned ph, expect;
        clock_t t1;

        arcade1_run_frame(&m);
        t1 = clock();

        delta = m.cpu.instret - inst_before;

        ph = (unsigned)guest32(sym_phase);
        expect = f / FR_PER_PHASE;
        if (expect > PH_RAY)
            expect = PH_RAY;
        assert(ph == expect && "guest phase must match f/FR_PER_PHASE");

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
    assert(guest32(sym_sink) != 0u && "micro work must not be optimised away");
}

/* The render must be a real picture, not a blank frame: sky present, at
 * least 4 of the 6 palette colours on screen, and at least 2 distinct
 * colours along a terrain row below the horizon. */
static void check_framebuffer(void)
{
    static const uint16_t pal[6] = {
        COL_SKY, COL_WATER, COL_SAND, COL_GRASS, COL_STONE, COL_SNOW,
    };
    unsigned seen = 0, i, x;
    const int row = (36 + 10) * CH;       /* well below the horizon        */

    for (i = 0; i < 6; i++) {
        unsigned k;
        int found = 0;
        for (k = 0; k < ARCADE1_FB_PIXELS; k++) {
            if (m.framebuffer[k] == pal[i]) {
                found = 1;
                seen++;
                break;
            }
        }
        fprintf(stderr, "pal[%u]=0x%04X: %s\n", i, pal[i],
                found ? "PRESENT" : "absent");
    }
    fprintf(stderr, "palette colours present: %u, sky row0[0]=0x%04X, "
            "mid row[%d][0]=0x%04X [40]=0x%04X [200]=0x%04X\n",
            seen, m.framebuffer[0],
            row, m.framebuffer[row * ARCADE1_WIDTH],
            m.framebuffer[row * ARCADE1_WIDTH + 40],
            m.framebuffer[row * ARCADE1_WIDTH + 200]);
    assert(seen >= 4u && "render must use >=4 palette colours");

    for (x = 1; x < (unsigned)RAYS * CH; x++) {
        if (m.framebuffer[row * ARCADE1_WIDTH + x] !=
            m.framebuffer[row * ARCADE1_WIDTH]) {
            return;                         /* terrain varies across row     */
        }
    }
    assert(!"terrain row must vary across the screen");
}

/* --- Report ---------------------------------------------------------------- */

static void report(void)
{
    const double baseline = (double)inst_sum[0] / (double)counts[0];
    unsigned p;

    printf("%-30s %7s %11s %11s\n", "phase", "frames", "inst/fr", "us/fr");
    for (p = 0; p < PHASSES; p++) {
        const double avg = (double)inst_sum[p] / (double)counts[p];
        const double us =
            (double)ticks_sum[p] * 1e6 / (double)CLOCKS_PER_SEC /
            (double)counts[p];
        printf("%1d %-28s %7u %11.1f %11.1f\n",
               p, labels[p], counts[p], avg, us);
    }
    printf("height eval: height3 = %.1f inst, ray_height = %.1f inst "
           "(baseline-subtracted, /%u evals/frame)\n",
           ((double)inst_sum[1] / (double)counts[1] - baseline) / MICRO_EVALS,
           ((double)inst_sum[2] / (double)counts[2] - baseline) / MICRO_EVALS,
           MICRO_EVALS);
    printf("raycast frame = %.1f inst (%.1f%% of the 1,000,000 cap); "
           "us/ = host time, indicative only, never asserted.\n",
           (double)inst_sum[PH_RAY] / (double)counts[PH_RAY],
           100.0 * ((double)inst_sum[PH_RAY] / (double)counts[PH_RAY]) /
           1000000.0);
}

/* Dump the final framebuffer as a binary PPM (build/ray.ppm) so the render
 * can be inspected visually — property checks assert correctness, but eyes
 * catch "runs but looks wrong" cases no assertion covers. */
static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i;

    assert(f && "cannot open build/ray.ppm");
    fprintf(f, "P6\n%d %d\n255\n", ARCADE1_WIDTH, ARCADE1_HEIGHT);
    for (i = 0; i < ARCADE1_FB_PIXELS; i++) {
        const uint16_t c = m.framebuffer[i];
        const unsigned char rgb[3] = {
            (unsigned char)((c >> 11) * 255 / 31),
            (unsigned char)(((c >> 5) & 63) * 255 / 63),
            (unsigned char)((c & 31) * 255 / 31),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    FILE *f;
    long n;
    unsigned ray_avg;

    if (argc != 4) {
        fprintf(stderr, "usage: %s <image> <phase-addr> <sink-addr>\n",
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
    sym_sink  = (int32_t)strtoul(argv[3], NULL, 16);

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
    fflush(stdout);          /* keep measurements if the assert below aborts */
    check_framebuffer();
    dump_ppm("build/ray.ppm");
    /* print observed pins FIRST so an observational run shows them even if
     * the golden asserts below fail (that is how they get pinned) */
    ray_avg = (unsigned)((double)inst_sum[PH_RAY] /
                         (double)counts[PH_RAY] + 0.5);
    printf("sink=0x%08X raycast_avg=%u state_hash=0x%016llX\n",
           guest32(sym_sink), ray_avg,
           (unsigned long long)hist1[nhist1 - 1]);
    fflush(stdout);

    assert(guest32(sym_sink) == GOLDEN_SINK && "sink golden drifted");
    assert(hist1[nhist1 - 1] == GOLDEN_STATE_HASH &&
           "final state hash golden drifted");
    assert(ray_avg == GOLDEN_RAY_INST && "raycast inst/frame golden drifted");

    printf("voxel-P2: raycast ok (%u frames x2, identical, render checked)\n",
           nhist1);
    return 0;
}
