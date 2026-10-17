/* ARCADE-1 — voxel-experiment P4 harness: game systems scenario.
 *
 * Boots programs/systems.c as firmware and drives a scripted pad for two
 * identical 270-frame passes, exercising the full system layer:
 *
 *   frames  0..29  idle (day)
 *   frames 30..59  hold RIGHT — camera walks 30 cells
 *   frame 60   A dig (yields dirt 1, edits target 1)
 *   frame 70   B place  (CONSUMES dirt -> accepted, edits target 2)
 *   frame 80   A dig    (dirt 1)
 *   frame 90   A dig    (dirt 2)
 *   frame 102  A dig    (dirt 3)
 *   frame 110  DOWN craft (3 dirt -> 1 stone; dirt 0, stone 1)
 *   frame 122  B place  REJECTED (empty inventory: act_ok=0, no edit)
 *   frames123..269 idle (night falls, entities roam)
 *
 * Assertions:
 *   - mode matches the scripted phase every frame (5 modes incl. craft);
 *   - counters: camx 30, digs 4, places 2 (attempts), crafts 1, act_n 6;
 *   - inventory truth: dirt 0, stone 1 (craft consumed exactly 3, the
 *     rejected place consumed nothing);
 *   - delta truth: the guest recorded each action's ACTUAL target (targets
 *     shift as edits move the silhouette), so the harness replays the
 *     accepted edits against its own ray_height model (stepwise +-32 with
 *     clamping, insertion order) and requires delta_count AND the table
 *     digest to match exactly — pins x, z and final h of every cell;
 *   - day/night truth: tod == (frame/30) % 3 read at frames 15/45/75;
 *   - entity truth: patrol speed pinned (e1x == 4 at frame 10), both
 *     entities inside their bands at the end, and both entity colours
 *     PRESENT in the final framebuffer (pixel scan);
 *   - collision truth py vs the independent ray_height model;
 *   - reset-and-compare determinism + frame protocol honesty;
 *   - per-mode inst/frame reported; idle-with-systems golden-pinned.
 *
 * The final framebuffer is dumped to build/systems.ppm for eyeballing.
 *
 * Usage: test_systems <systems.arc1|bin> <camx> <digs> <places> <crafts>
 *   <inv_dirt> <inv_stone> <delta_count> <mode> <py> <tdigest> <tod>
 *   <e1x> <e2x> <act_tx> <act_tz> <act_ok> <act_n>
 */

#include "machine.h"    /* brings in memory/timer/video/audio/exec */
#include "rom.h"        /* M17: .arc1 image parsing */
#include "state_hash.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Self-diagnosing check: prints name, guest value and expectation before
 * failing, so no run ever dies with a bare "assertion failed". */
#define CHECK(got, want, what) do {                                 \
    const unsigned long long g_ = (unsigned long long)(got);        \
    const unsigned long long w_ = (unsigned long long)(want);       \
    if (g_ != w_) {                                                 \
        fprintf(stderr, "FAIL %s: guest=%llu expected=%llu\n",      \
                what, g_, w_);                                      \
        assert(!what);                                              \
    }                                                               \
} while (0)

/* --- Script (kept in sync with programs/systems.c by hand) ----------------- */

#define TOTAL_FRAMES 270u
#define WALK_FIRST   30u
#define WALK_LAST    59u
#define ACTIONS      7u
#define PAD_DOWN     0x02u
#define PAD_RIGHT    0x08u
#define PAD_A        0x10u        /* dig   */
#define PAD_B        0x20u        /* place */

static const unsigned act_f[ACTIONS]   = {
    60u, 70u, 80u, 90u, 102u, 110u, 122u,
};
static const unsigned act_pad[ACTIONS] = {
    PAD_A, PAD_B, PAD_A, PAD_A, PAD_A, PAD_DOWN, PAD_B,
};

#define EXP_CAMX   30u
#define EXP_DIGS   4u
#define EXP_PLACES 2u             /* attempts; 1 accepted, 1 rejected */
#define EXP_CRAFTS 1u
#define EXP_DIRT   0u
#define EXP_STONE  1u
#define EXP_ACTS   6u             /* recorded actions (craft not one) */
#define AMT        32

static unsigned frame_pad(unsigned f)
{
    unsigned i;

    if (f >= WALK_FIRST && f <= WALK_LAST)
        return PAD_RIGHT;
    for (i = 0; i < ACTIONS; i++)
        if (f == act_f[i])
            return act_pad[i];
    return 0u;
}

static unsigned expect_mode(unsigned f)
{
    const unsigned pad = frame_pad(f);

    if (pad == PAD_RIGHT) return 1u;
    if (pad == PAD_A)     return 2u;
    if (pad == PAD_B)     return 3u;
    if (pad == PAD_DOWN)  return 4u;
    return 0u;
}

/* Pinned golden values (observational run first, then pinned). Observed at
 * P4: tdigest 0x2AA3A3F1 (5 accepted edits collapsed onto 2 cells by the
 * shifting-silhouette targets, replayed exactly by the model), idle 431,559
 * inst/frame, hash 0x26E54043DD80BE92. */
#define GOLDEN_TDIGEST   0x2AA3A3F1u
#define GOLDEN_IDLE_INST 431559u
#define GOLDEN_STATE_HASH UINT64_C(0x0686D9110C654632)

#define MODES 5u

static const char *mode_labels[MODES] = {
    "idle (view + systems)", "walk", "dig", "place", "craft",
};

/* --- Independent model of ray_height --------------------------------------- */

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
    const unsigned s = 0x5EED1234u ^ ((unsigned)cx * 0x9E3779B9u)
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

    return model_interp(model_interp((int)model_lattice(oct, x0,     z0),
                                     (int)model_lattice(oct, x0 + 1, z0), u),
                        model_interp((int)model_lattice(oct, x0,     z0 + 1),
                                     (int)model_lattice(oct, x0 + 1, z0 + 1),
                                     u), v);
}

static unsigned model_ray_height(int wx, int wz)
{
    const int raw = (model_noise(wx, wz, 0u) * 2 + model_noise(wx, wz, 1u)) / 3;
    int h = 128 + ((raw - 128) * 5) / 3;

    if (h < 0)
        h = 0;
    if (h > 255)
        h = 255;
    return (unsigned)h;
}

static int clamp_h(int h)
{
    if (h < 0)
        return 0;
    if (h > 255)
        return 255;
    return h;
}

/* --- State ----------------------------------------------------------------- */

static arcade1_machine_t m;
static uint8_t  img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
static size_t   img_size;
static int32_t  sym_camx, sym_digs, sym_places, sym_crafts, sym_dirt,
                sym_stone, sym_dcount, sym_mode, sym_py, sym_tdigest,
                sym_tod, sym_e1x, sym_e2x, sym_atx, sym_atz, sym_aok,
                sym_an;

static uint16_t fb1[ARCADE1_FB_PIXELS];
static uint64_t hist1[640];
static unsigned nhist1, frame_count;
static bool     verify_hashes;

static uint64_t inst_sum[MODES];
static unsigned counts[MODES];
static unsigned tod_seen[3];      /* tod value observed at frames 15/45/75 */
static unsigned e1x_at10;
static bool     magenta_seen, orange_seen;

/* --- Helpers --------------------------------------------------------------- */

static uint32_t guest32(int32_t addr)
{
    return arcade1_read32(&m.bus, (uint32_t)addr);
}

static void set_pad(unsigned mask)
{
    arcade1_input_t in;

    memset(&in, 0, sizeof in);
    in.down  = (mask & PAD_DOWN) != 0;
    in.left  = (mask & 0x04u) != 0;
    in.right = (mask & PAD_RIGHT) != 0;
    in.a     = (mask & PAD_A) != 0;
    in.b     = (mask & PAD_B) != 0;
    arcade1_set_input(&m, &in);
}

static void scan_entities(void)
{
    int i;

    for (i = 0; i < ARCADE1_FB_PIXELS; i++) {
        if (m.framebuffer[i] == 0xF81Fu)
            magenta_seen = true;
        if (m.framebuffer[i] == 0xFD20u)
            orange_seen = true;
    }
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
    memset(inst_sum, 0, sizeof inst_sum);
    memset(counts,   0, sizeof counts);
    memset(tod_seen, 0, sizeof tod_seen);
    e1x_at10 = 0;
    magenta_seen = orange_seen = false;
}

/* --- One 270-frame scripted pass ------------------------------------------- */

static void pass(bool verify)
{
    unsigned f;

    verify_hashes = verify;
    boot_machine();

    for (f = 0; f < TOTAL_FRAMES; f++) {
        const uint64_t inst_before = m.cpu.instret;
        const unsigned pad = frame_pad(f);
        unsigned ph;
        uint64_t h;

        set_pad(pad);
        arcade1_run_frame(&m);

        ph = (unsigned)guest32(sym_mode);
        if (ph != expect_mode(f)) {
            fprintf(stderr, "FAIL mode at frame %u: guest=%u pad=0x%02X\n",
                    f, ph, pad);
            assert(!"guest mode must match the scripted phase");
        }

        if (f > 0u) {                       /* exclude the boot frame     */
            inst_sum[ph]  += m.cpu.instret - inst_before;
            counts[ph]++;
        }

        if (f == 10u)
            e1x_at10 = (unsigned)guest32(sym_e1x);
        if (f == 15u) tod_seen[0] = (unsigned)guest32(sym_tod);
        if (f == 45u) tod_seen[1] = (unsigned)guest32(sym_tod);
        if (f == 75u) tod_seen[2] = (unsigned)guest32(sym_tod);

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

    CHECK(frame_count, TOTAL_FRAMES, "frame count");
    CHECK(m.timer.count, frame_count, "timer == frames run");
    CHECK(m.video.presents, frame_count, "one present per frame");
    CHECK(m.bus.stray_accesses, 0u, "no stray accesses");

    /* counters + inventory + crafting truth */
    CHECK(guest32(sym_camx), EXP_CAMX, "walk must end at camx 30");
    CHECK(guest32(sym_digs), EXP_DIGS, "4 scripted digs");
    CHECK(guest32(sym_places), EXP_PLACES, "2 place attempts");
    CHECK(guest32(sym_crafts), EXP_CRAFTS, "1 successful craft");
    CHECK(guest32(sym_dirt), EXP_DIRT, "inventory dirt must be 0");
    CHECK(guest32(sym_stone), EXP_STONE, "craft yielded 1 stone");
    CHECK(guest32(sym_an), EXP_ACTS, "6 recorded actions");

    /* day/night timing: tod == (frame/30) % 3 */
    CHECK(tod_seen[0], 0u, "frame 15 must be day");
    CHECK(tod_seen[1], 1u, "frame 45 must be dusk");
    CHECK(tod_seen[2], 2u, "frame 75 must be night");

    /* entities move at their pinned speed and stay in their patrol bands:
     * e1 starts at offset 2.0 cells and advances 1/4 cell per frame, so at
     * frame 10 (11 patrol ticks) it reads exactly 4 — pins motion AND speed */
    {
        const unsigned e1 = (unsigned)guest32(sym_e1x);
        const unsigned e2 = (unsigned)guest32(sym_e2x);
        CHECK(e1x_at10, 4u, "entity 1 patrol speed is 1/4 cell/frame");
        if (e1 < EXP_CAMX + 2u || e1 > EXP_CAMX + 6u) {
            fprintf(stderr, "FAIL entity1 band: e1=%u camx=%u\n", e1,
                    EXP_CAMX);
            assert(!"entity 1 outside patrol band [camx+2, camx+6]");
        }
        if (e2 < EXP_CAMX + 3u || e2 > EXP_CAMX + 9u) {
            fprintf(stderr, "FAIL entity2 band: e2=%u camx=%u\n", e2,
                    EXP_CAMX);
            assert(!"entity 2 outside patrol band [camx+3, camx+9]");
        }
    }

    /* collision truth vs independent model (no delta at (camx, 0)) */
    {
        const unsigned model = model_ray_height((int)EXP_CAMX, 0);
        if ((unsigned)guest32(sym_py) != model) {
            fprintf(stderr, "FAIL: py guest=%u model=%u\n",
                    (unsigned)guest32(sym_py), model);
            assert(!"camera ground height diverged from model");
        }
    }

    /* full edit-model truth: replay the recorded action targets against the
     * model table (stepwise +-32 with clamping, insertion order) and require
     * the guest's delta_count AND table digest to match exactly */
    {
        struct { int x, z, h; } mt[16];
        unsigned mn = 0, i, d = 2166136261u;

        for (i = 0; i < (unsigned)guest32(sym_an); i++) {
            const int  tx = (int)guest32(sym_atx + (int32_t)(4 * i));
            const int  tz = (int)guest32(sym_atz + (int32_t)(4 * i));
            const bool ok = guest32(sym_aok + (int32_t)(4 * i)) != 0u;
            int j, found = -1;

            if (!ok)                           /* rejected place: no edit */
                continue;
            for (j = 0; j < (int)mn; j++)
                if (mt[j].x == tx && mt[j].z == tz)
                    found = j;
            if (found < 0) {                   /* new cell: append        */
                assert(mn < 16 && "model table full");
                mt[mn].x = tx;
                mt[mn].z = tz;
                mt[mn].h = (int)model_ray_height(tx, tz);
                found = (int)mn;
                mn++;
            }
            /* dig = -AMT, place = +AMT: recorded index i maps to script
             * action i for i<5, and to script action 6 for i==5 (the craft
             * at script index 5 records nothing, the rejected place at 6
             * records ok=0 and is skipped above) */
            {
                const unsigned si = (i < 5u) ? i : 6u;
                const int dlt = (act_pad[si] == PAD_B) ? AMT : -AMT;
                mt[found].h = clamp_h(mt[found].h + dlt);
            }
        }
        CHECK(guest32(sym_dcount), mn, "delta table entry count");
        for (i = 0; i < mn; i++) {
            d = (d ^ (unsigned)mt[i].x) * 16777619u;
            d = (d ^ (unsigned)mt[i].z) * 16777619u;
            d = (d ^ (unsigned)mt[i].h) * 16777619u;
        }
        if (guest32(sym_tdigest) != d) {
            fprintf(stderr, "FAIL: tdigest guest=0x%08X model=0x%08X\n",
                    (unsigned)guest32(sym_tdigest), d);
            for (i = 0; i < mn; i++)
                fprintf(stderr, "  model cell %u: (%d,%d)->%d\n", i,
                        mt[i].x, mt[i].z, mt[i].h);
            assert(!"delta table digest diverged from the edit model");
        }
    }

    /* entity colours must actually be on screen in the final frame */
    scan_entities();
    if (!magenta_seen)
        fprintf(stderr, "note: entity 1 (magenta) not in final frame\n");
    if (!orange_seen)
        fprintf(stderr, "note: entity 2 (orange) not in final frame\n");
    assert(magenta_seen && "entity 1 (magenta) must be visible");
    assert(orange_seen && "entity 2 (orange) must be visible");
}

static void report(void)
{
    unsigned p;
    const double idle = (double)inst_sum[0] / (double)counts[0];

    printf("%-26s %7s %11s\n", "mode", "frames", "inst/fr");
    for (p = 0; p < MODES; p++)
        printf("%1d %-24s %7u %11.1f\n", p, mode_labels[p], counts[p],
               (double)inst_sum[p] / (double)counts[p]);
    printf("idle+systems vs P3 idle (427,816): %+.1f inst/frame\n",
           idle - 427816.0);
    printf("tod at f15/f45/f75: %u/%u/%u   e1x=%u e2x=%u   "
           "entity colours: magenta=%d orange=%d\n",
           tod_seen[0], tod_seen[1], tod_seen[2],
           (unsigned)guest32(sym_e1x), (unsigned)guest32(sym_e2x),
           magenta_seen, orange_seen);
}

static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i;

    assert(f && "cannot open build/systems.ppm");
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
    unsigned idle_avg;

    if (argc != 19) {
        fprintf(stderr, "usage: %s <image> <camx> <digs> <places> <crafts> "
                        "<inv_dirt> <inv_stone> <delta_count> <mode> <py> "
                        "<tdigest> <tod> <e1x> <e2x> "
                        "<act_tx> <act_tz> <act_ok> <act_n>\n", argv[0]);
        return 2;
    }

    f = fopen(argv[1], "rb");
    assert(f && "cannot open guest image");
    n = (long)fread(img, 1, sizeof img, f);
    assert(n > 0 && (size_t)n <= sizeof img && "image must fit buffer");
    fclose(f);
    img_size = (size_t)n;

    sym_camx    = (int32_t)strtoul(argv[2],  NULL, 16);
    sym_digs    = (int32_t)strtoul(argv[3],  NULL, 16);
    sym_places  = (int32_t)strtoul(argv[4],  NULL, 16);
    sym_crafts  = (int32_t)strtoul(argv[5],  NULL, 16);
    sym_dirt    = (int32_t)strtoul(argv[6],  NULL, 16);
    sym_stone   = (int32_t)strtoul(argv[7],  NULL, 16);
    sym_dcount  = (int32_t)strtoul(argv[8],  NULL, 16);
    sym_mode    = (int32_t)strtoul(argv[9],  NULL, 16);
    sym_py      = (int32_t)strtoul(argv[10], NULL, 16);
    sym_tdigest = (int32_t)strtoul(argv[11], NULL, 16);
    sym_tod     = (int32_t)strtoul(argv[12], NULL, 16);
    sym_e1x     = (int32_t)strtoul(argv[13], NULL, 16);
    sym_e2x     = (int32_t)strtoul(argv[14], NULL, 16);
    sym_atx     = (int32_t)strtoul(argv[15], NULL, 16);
    sym_atz     = (int32_t)strtoul(argv[16], NULL, 16);
    sym_aok     = (int32_t)strtoul(argv[17], NULL, 16);
    sym_an      = (int32_t)strtoul(argv[18], NULL, 16);

    printf("--- pass 1: scripted scenario ---\n");
    pass(false);
    nhist1 = frame_count;
    memcpy(fb1, m.framebuffer, sizeof fb1);

    printf("--- pass 2: reset and replay ---\n");
    pass(true);
    CHECK(frame_count, nhist1, "replay frame count");
    CHECK(memcmp(fb1, m.framebuffer, sizeof fb1),
          0, "final framebuffer diverged after reset");

    dump_ppm("build/systems.ppm");
    report();
    fflush(stdout);

    idle_avg = (unsigned)(0.5 + (double)inst_sum[0] / (double)counts[0]);
    printf("camx=%u digs=%u places=%u crafts=%u dirt=%u stone=%u "
           "deltas=%u py=%u tdigest=0x%08X idle_inst=%u "
           "state_hash=0x%016llX\n",
           guest32(sym_camx), guest32(sym_digs), guest32(sym_places),
           guest32(sym_crafts), guest32(sym_dirt), guest32(sym_stone),
           guest32(sym_dcount), guest32(sym_py), guest32(sym_tdigest),
           idle_avg, (unsigned long long)hist1[nhist1 - 1]);
    fflush(stdout);

    CHECK(guest32(sym_tdigest), GOLDEN_TDIGEST,
          "delta table digest golden drifted");
    CHECK(idle_avg, GOLDEN_IDLE_INST, "idle inst/frame golden drifted");
    CHECK(hist1[nhist1 - 1], GOLDEN_STATE_HASH,
          "final state hash golden drifted");

    printf("voxel-P4: game systems ok (%u frames x2, identical, "
           "inventory/craft/entities/day-night)\n", nhist1);
    return 0;
}
