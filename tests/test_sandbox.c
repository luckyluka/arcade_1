/* ARCADE-1 — voxel-experiment P3 harness: walk / dig / place playthrough.
 *
 * Boots programs/sandbox.c as firmware (arcade1_set_firmware) and drives a
 * scripted pad for two identical 150-frame passes, exercising the delta
 * overlay that the whole "world stores nothing" design rests on:
 *
 *   frames  0..29  idle (baseline view, no edits)
 *   frames 30..59  hold RIGHT — camera walks 30 cells, eye rides terrain
 *   frames 60,70,80,90,102  B B A B B — 4 places + 1 dig, alternating
 *                   between the two watched silhouette cells (the guest
 *                   edits the render's own nearest-visible-ground cells, so
 *                   edits are visible BY CONSTRUCTION — fixed (x,z) targets
 *                   can fall below the screen, see section 0.4)
 *   frames110..149 idle (final view with the edits rendered)
 *
 * Assertions:
 *   - the guest's `mode` symbol matches the scripted phase every frame;
 *   - gameplay truth: camx == 30, digs == 1, places == 4, delta_count == 2;
 *   - collision truth py == independent host model of ray_height(camx, 0);
 *   - full edit-model truth: the harness re-simulates the 5-edit sequence
 *     (alternating cells, +-32 with clamping, in script order) against its
 *     own ray_height model and requires the guest's delta-table digest to
 *     match EXACTLY — pins x, z and final h of both edited cells;
 *   - the edits are VISIBLE: fb digest after the last action differs from
 *     the edit-free walk view; per-edit visibility is reported;
 *   - reset-and-compare determinism: per-frame hash sequence + final fb;
 *   - frame protocol honesty (one present/timer tick per frame, no strays).
 *
 * Per-mode instruction averages are reported; the idle-with-deltas average
 * is golden-pinned — it is P3's headline measurement: what the linear delta
 * scan costs on top of P2's 406,563-inst raycast frame.
 *
 * The final framebuffer is dumped to build/sandbox.ppm for eyeballing.
 *
 * Usage: test_sandbox <sandbox.arc1|bin> <camx> <digs> <places> <delta_count>
 *                     <mode> <py> <tdigest> <tgt1_x> <tgt1_z> <tgt2_x>
 *                     <tgt2_z>
 */

#include "machine.h"    /* brings in memory/timer/video/audio/exec */
#include "rom.h"        /* M17: .arc1 image parsing */
#include "state_hash.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* --- Script (kept in sync with programs/sandbox.c by hand) ----------------- */

#define TOTAL_FRAMES 150u
#define WALK_FIRST   30u
#define WALK_LAST    59u          /* inclusive */
#define ACTIONS      5u
#define PAD_RIGHT    0x08u
#define PAD_A        0x10u        /* dig   */
#define PAD_B        0x20u        /* place */

static const unsigned act_f[ACTIONS]   = { 60u, 70u, 80u, 90u, 102u };
static const unsigned act_pad[ACTIONS] = { PAD_B, PAD_B, PAD_A, PAD_B, PAD_B };

#define EXP_CAMX   30u
#define EXP_DIGS   1u             /* act 2 */
#define EXP_PLACES 4u             /* acts 0,1,3,4 */
#define EXP_DELTAS 2u             /* cells alternate by action parity */
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
    return 0u;
}

/* Pinned golden values (observational run first, then pinned): the delta
 * table digest (also independently modelled above), the idle-with-deltas
 * raycast cost, and the final per-frame state hash. Observed at P3:
 * tdigest 0x00C134B1, idle 427,816 inst/frame, hash 0x360883B3C544A0CA. */
#define GOLDEN_TDIGEST   0x00C134B1u
#define GOLDEN_IDLE_INST 427816u
#define GOLDEN_STATE_HASH UINT64_C(0x355725A83145E16A)

#define MODES 4u

static const char *mode_labels[MODES] = {
    "idle (view + delta scan)", "walk", "dig", "place",
};

/* --- Independent model of ray_height (collision + edit truth) ------------- */

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

/* --- State ---------------------------------------------------------------- */

static arcade1_machine_t m;
static uint8_t  img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
static size_t   img_size;
static int32_t  sym_camx, sym_digs, sym_places, sym_dcount, sym_mode,
                sym_py, sym_tdigest, sym_t1x, sym_t1z, sym_t2x, sym_t2z;

static uint16_t fb1[ARCADE1_FB_PIXELS];
static uint64_t hist1[512];
static unsigned nhist1, frame_count;
static bool     verify_hashes;

static uint64_t inst_sum[MODES];
static unsigned counts[MODES];
static uint64_t fb_walk;                 /* fb digest at end of walk (f59) */
static uint64_t fb_edit[ACTIONS];        /* fb 2 frames after each action  */

/* --- Helpers --------------------------------------------------------------- */

static uint32_t guest32(int32_t addr)
{
    return arcade1_read32(&m.bus, (uint32_t)addr);
}

static void set_pad(unsigned mask)
{
    arcade1_input_t in;

    memset(&in, 0, sizeof in);
    in.left  = (mask & 0x04u) != 0;
    in.right = (mask & PAD_RIGHT) != 0;
    in.a     = (mask & PAD_A) != 0;
    in.b     = (mask & PAD_B) != 0;
    arcade1_set_input(&m, &in);
}

static uint64_t fb_digest(void)
{
    uint64_t h = UINT64_C(14695981039346656037);
    int i;

    for (i = 0; i < ARCADE1_FB_PIXELS; i++) {
        h = (h ^ (m.framebuffer[i] & 0xFFu)) * UINT64_C(1099511628211);
        h = (h ^ (m.framebuffer[i] >> 8)) * UINT64_C(1099511628211);
    }
    return h;
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
    fb_walk = 0;
    memset(fb_edit, 0, sizeof fb_edit);
}

/* --- One 150-frame scripted pass ------------------------------------------- */

static void pass(bool verify)
{
    unsigned f;

    verify_hashes = verify;
    boot_machine();

    for (f = 0; f < TOTAL_FRAMES; f++) {
        const uint64_t inst_before = m.cpu.instret;
        const unsigned pad = frame_pad(f);
        unsigned ph, i;
        uint64_t h;

        set_pad(pad);
        arcade1_run_frame(&m);

        ph = (unsigned)guest32(sym_mode);
        assert(ph == expect_mode(f) &&
               "guest mode must match the scripted phase");

        if (f > 0u) {                       /* exclude the boot frame     */
            inst_sum[ph]  += m.cpu.instret - inst_before;
            counts[ph]++;
        }

        if (f == WALK_LAST)
            fb_walk = fb_digest();
        for (i = 0; i < ACTIONS; i++)
            if (f == act_f[i] + 2u)
                fb_edit[i] = fb_digest();

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

    /* gameplay truth from the script */
    assert(guest32(sym_camx) == EXP_CAMX && "walk must end at camx 30");
    assert(guest32(sym_digs) == EXP_DIGS && "1 scripted dig");
    assert(guest32(sym_places) == EXP_PLACES && "4 scripted places");
    assert(guest32(sym_dcount) == EXP_DELTAS && "2 edited cells");

    /* collision truth vs independent model (no delta at (camx, 0)) */
    {
        const unsigned model = model_ray_height((int)EXP_CAMX, 0);
        if ((unsigned)guest32(sym_py) != model) {
            fprintf(stderr, "FAIL: py guest=%u model=%u\n",
                    (unsigned)guest32(sym_py), model);
            assert(!"camera ground height diverged from model");
        }
    }

    /* full edit-model truth: re-simulate the 5-edit sequence against the
     * model and require the guest's delta-table digest to match exactly */
    {
        const int t1x = (int)guest32(sym_t1x), t1z = (int)guest32(sym_t1z);
        const int t2x = (int)guest32(sym_t2x), t2z = (int)guest32(sym_t2z);
        int h1 = (int)model_ray_height(t1x, t1z);
        int h2 = (int)model_ray_height(t2x, t2z);
        unsigned i;
        unsigned d = 2166136261u;

        assert(t1z != 0u && t2z != 0u && "watched cells must be recorded");
        for (i = 0; i < ACTIONS; i++) {
            const int dlt = (act_pad[i] == PAD_B) ? AMT : -AMT;
            if (i & 1u)
                h2 = clamp_h(h2 + dlt);
            else
                h1 = clamp_h(h1 + dlt);
        }
        d = (d ^ (unsigned)t1x) * 16777619u;
        d = (d ^ (unsigned)t1z) * 16777619u;
        d = (d ^ (unsigned)h1)  * 16777619u;
        d = (d ^ (unsigned)t2x) * 16777619u;
        d = (d ^ (unsigned)t2z) * 16777619u;
        d = (d ^ (unsigned)h2)  * 16777619u;
        if (guest32(sym_tdigest) != d) {
            fprintf(stderr, "FAIL: tdigest guest=0x%08X model=0x%08X "
                            "(cell1 (%d,%d)->%d cell2 (%d,%d)->%d)\n",
                    (unsigned)guest32(sym_tdigest), d, t1x, t1z, h1,
                    t2x, t2z, h2);
            assert(!"delta table digest diverged from the edit model");
        }
    }
}

static void report(void)
{
    unsigned p, i;
    const double idle = (double)inst_sum[0] / (double)counts[0];

    printf("%-28s %7s %11s\n", "mode", "frames", "inst/fr");
    for (p = 0; p < MODES; p++)
        printf("%1d %-26s %7u %11.1f\n", p, mode_labels[p], counts[p],
               (double)inst_sum[p] / (double)counts[p]);
    printf("idle+delta scan vs P2 raycast (406,563): %+.1f inst/frame\n",
           idle - 406563.0);
    for (i = 0; i < ACTIONS; i++)
        printf("edit %u (%s @col %u): %s\n", i,
               act_pad[i] == PAD_B ? "place" : "dig   ",
               i & 1u ? (unsigned)guest32(sym_t2x)
                      : (unsigned)guest32(sym_t1x),
               fb_edit[i] != fb_walk ? "VISIBLE" : "no change");
}

/* Dump the final framebuffer as a binary PPM (build/sandbox.ppm). */
static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i;

    assert(f && "cannot open build/sandbox.ppm");
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

    if (argc != 13) {
        fprintf(stderr, "usage: %s <image> <camx> <digs> <places> "
                        "<delta_count> <mode> <py> <tdigest> "
                        "<tgt1_x> <tgt1_z> <tgt2_x> <tgt2_z>\n", argv[0]);
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
    sym_dcount  = (int32_t)strtoul(argv[5],  NULL, 16);
    sym_mode    = (int32_t)strtoul(argv[6],  NULL, 16);
    sym_py      = (int32_t)strtoul(argv[7],  NULL, 16);
    sym_tdigest = (int32_t)strtoul(argv[8],  NULL, 16);
    sym_t1x     = (int32_t)strtoul(argv[9],  NULL, 16);
    sym_t1z     = (int32_t)strtoul(argv[10], NULL, 16);
    sym_t2x     = (int32_t)strtoul(argv[11], NULL, 16);
    sym_t2z     = (int32_t)strtoul(argv[12], NULL, 16);

    printf("--- pass 1: scripted playthrough ---\n");
    pass(false);
    nhist1 = frame_count;
    memcpy(fb1, m.framebuffer, sizeof fb1);

    printf("--- pass 2: reset and replay ---\n");
    pass(true);
    assert(frame_count == nhist1 && "replay frame count diverged");
    assert(memcmp(fb1, m.framebuffer, sizeof fb1) == 0 &&
           "framebuffer diverged after reset");

    dump_ppm("build/sandbox.ppm");
    report();
    fflush(stdout);

    /* edits must be visible SOMEWHERE in the render (the view after the
     * last action, all edits recorded, camera unchanged since the walk) */
    assert(fb_edit[ACTIONS - 1] != fb_walk &&
           "edits must be visible in the render");
    fflush(stdout);

    idle_avg = (unsigned)(0.5 + (double)inst_sum[0] / (double)counts[0]);
    printf("camx=%u digs=%u places=%u deltas=%u py=%u tdigest=0x%08X "
           "idle_inst=%u state_hash=0x%016llX\n",
           guest32(sym_camx), guest32(sym_digs), guest32(sym_places),
           guest32(sym_dcount), guest32(sym_py), guest32(sym_tdigest),
           idle_avg, (unsigned long long)hist1[nhist1 - 1]);
    fflush(stdout);

    assert(guest32(sym_tdigest) == GOLDEN_TDIGEST &&
           "delta table digest golden drifted (edits changed?)");
    assert(idle_avg == GOLDEN_IDLE_INST &&
           "idle+scan inst/frame golden drifted");
    assert(hist1[nhist1 - 1] == GOLDEN_STATE_HASH &&
           "final state hash golden drifted");

    printf("voxel-P3: interaction ok (%u frames x2, identical, dig/place "
           "rendered)\n", nhist1);
    return 0;
}
