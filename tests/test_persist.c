/* ARCADE-1 — voxel-experiment P5 harness: persistence (save/load + replay).
 *
 * The machine has no storage device, so a "save file" is a mechanism claim,
 * and section 1 makes two: the world is procedural (seed, baked into ROM) +
 * a small delta table, so a save is seed+deltas; or you can persist nothing
 * but the input log and replay it. P5 verifies BOTH mechanisms against
 * programs/systems.c (no guest changes needed — the game state is exactly
 * the guest's globals + RAM):
 *
 *   Pass A "record"  boot, run the P4 scripted scenario (270 frames),
 *                   recording: the pad log (1 byte/frame), a per-frame
 *                   state_hash AND a per-frame ram_hash, and — at frame
 *                   122, after the rejected place — a snapshot: the full
 *                   4 KB guest RAM + the timer count + present count.
 *   Pass B "replay"  fresh boot, drive from the recorded pad log only;
 *                   every frame's state_hash must equal pass A's, frame
 *                   for frame (all 270, including the boot frame).
 *   Pass C "load"    fresh boot, run one frame, then LOAD the snapshot
 *                   (RAM + timer + presents) — the "reboot into your
 *                   save" model — and continue frames 123..269 from the
 *                   pad log. Every frame's ram_hash (FNV over RAM bytes:
 *                   state_hash itself can never match across a load,
 *                   since it folds in the cumulative instret and the
 *                   transient CPU registers) must equal pass A's, the
 *                   final framebuffer must be byte-identical, and the
 *                   end-state symbols must match.
 *
 * Exit criterion (section 8): load-equals-replay — the loaded machine's
 * continuation is bit-identical (RAM, then screen) to the recorded run's,
 * and the replay machine reproduces the whole run exactly.
 *
 * Reported: the measured save sizes for this scenario (input log; full
 * RAM restore; the game-level seed+deltas+scalars save) plus the 128-
 * delta design budget, which corrects section 1's 6-bytes-per-delta
 * estimate (the guest delta entry is three 32-bit words = 12 bytes).
 *
 * Usage: test_persist <systems.arc1|bin> <camx> <digs> <places> <crafts>
 *   <inv_dirt> <inv_stone> <delta_count> <tdigest>
 */

#include "machine.h"    /* brings in memory/timer/video/audio/exec */
#include "rom.h"        /* M17: .arc1 image parsing */
#include "state_hash.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CHECK(got, want, what) do {                                 \
    const unsigned long long g_ = (unsigned long long)(got);        \
    const unsigned long long w_ = (unsigned long long)(want);       \
    if (g_ != w_) {                                                 \
        fprintf(stderr, "FAIL %s: got=0x%llX expected=0x%llX\n",    \
                what, g_, w_);                                      \
        assert(!what);                                              \
    }                                                               \
} while (0)

/* --- Script: identical to P4's scenario (kept in sync by hand) ------------ */

#define TOTAL_FRAMES 270u
#define WALK_FIRST   30u
#define WALK_LAST    59u
#define ACTIONS      7u
#define SNAP_FRAME   122u      /* snapshot AFTER this frame completes */
#define PAD_DOWN     0x02u
#define PAD_RIGHT    0x08u
#define PAD_A        0x10u
#define PAD_B        0x20u

static const unsigned act_f[ACTIONS]   = {
    60u, 70u, 80u, 90u, 102u, 110u, 122u,
};
static const unsigned act_pad[ACTIONS] = {
    PAD_A, PAD_B, PAD_A, PAD_A, PAD_A, PAD_DOWN, PAD_B,
};

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

/* Pinned golden values (observational run first, then pinned). The final
 * state hash equals P4's (same guest + same script — cross-check), and the
 * loaded continuation's RAM digest is pinned below. Observed at P5. */
#define GOLDEN_FINAL_STATE UINT64_C(0x0686D9110C654632)
#define GOLDEN_CONT_HASH   UINT64_C(0x195A24BC13043C38)

/* --- State ----------------------------------------------------------------- */

static arcade1_machine_t m;
static uint8_t  img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
static size_t   img_size;
static int32_t  sym_camx, sym_digs, sym_places, sym_crafts, sym_dirt,
                sym_stone, sym_dcount, sym_tdigest;

static uint8_t  pad_log[TOTAL_FRAMES];
static uint64_t shist[TOTAL_FRAMES];   /* pass A state_hash per frame     */
static uint64_t rhist[TOTAL_FRAMES];   /* pass A ram_hash per frame       */
static uint8_t  ram_snap[ARCADE1_RAM_SIZE];
static uint32_t snap_timer, snap_presents;
static uint16_t fbA[ARCADE1_FB_PIXELS];
static unsigned passA_frames;

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

/* FNV-1a 64 over the guest RAM bytes only: all game state lives there, and
 * unlike state_hash this is comparable across a load (no instret/regs). */
static uint64_t ram_hash(void)
{
    uint64_t h = UINT64_C(14695981039346656037);
    size_t i;

    for (i = 0; i < ARCADE1_RAM_SIZE; i++)
        h = (h ^ (uint64_t)m.bus.ram[i]) * UINT64_C(1099511628211);
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
}

/* --- Pass A: record -------------------------------------------------------- */

static void pass_record(void)
{
    unsigned f;

    boot_machine();

    for (f = 0; f < TOTAL_FRAMES; f++) {
        const unsigned pad = frame_pad(f);

        set_pad(pad);
        pad_log[f] = (uint8_t)pad;
        arcade1_run_frame(&m);

        shist[f] = state_hash(&m.cpu, &m.bus);
        rhist[f] = ram_hash();
        if (f == SNAP_FRAME) {                 /* the "save file" moment  */
            memcpy(ram_snap, m.bus.ram, ARCADE1_RAM_SIZE);
            snap_timer    = m.timer.count;
            snap_presents = m.video.presents;
        }
    }

    CHECK(m.timer.count, TOTAL_FRAMES, "timer == frames run");
    CHECK(m.video.presents, TOTAL_FRAMES, "one present per frame");
    CHECK(m.bus.stray_accesses, 0u, "no stray accesses");
    passA_frames = TOTAL_FRAMES;
}

/* --- Pass B: replay from the input log only -------------------------------- */

static void pass_replay(void)
{
    unsigned f;

    boot_machine();

    for (f = 0; f < TOTAL_FRAMES; f++) {
        set_pad(pad_log[f]);
        arcade1_run_frame(&m);
        if (state_hash(&m.cpu, &m.bus) != shist[f]) {
            fprintf(stderr, "FAIL replay at frame %u\n", f);
            assert(!"replay diverged from recorded state_hash");
        }
    }
    CHECK(m.timer.count, TOTAL_FRAMES, "replay timer == frames");
    CHECK(m.video.presents, TOTAL_FRAMES, "replay one present per frame");
    CHECK(m.bus.stray_accesses, 0u, "replay no stray accesses");
}

/* --- Pass C: load the snapshot and continue -------------------------------- */

static void pass_load(void)
{
    unsigned i;

    boot_machine();

    /* One real frame first: main() writes its one-time initialisers on the
     * first pass through, so the load lands after them (reboot-into-save). */
    set_pad(pad_log[0]);
    arcade1_run_frame(&m);

    memcpy(m.bus.ram, ram_snap, ARCADE1_RAM_SIZE);
    m.timer.count    = snap_timer;
    m.video.presents = snap_presents;

    for (i = SNAP_FRAME + 1u; i < TOTAL_FRAMES; i++) {
        set_pad(pad_log[i]);
        arcade1_run_frame(&m);
        if (ram_hash() != rhist[i]) {
            fprintf(stderr, "FAIL load at frame %u\n", i);
            assert(!"loaded continuation diverged from recorded RAM");
        }
    }
    CHECK(m.video.presents, TOTAL_FRAMES, "load presents == frames");
    CHECK(m.bus.stray_accesses, 0u, "load no stray accesses");
}

int main(int argc, char **argv)
{
    FILE *f;
    long n;
    const unsigned cont_frames = TOTAL_FRAMES - SNAP_FRAME - 1u;

    if (argc != 10) {
        fprintf(stderr, "usage: %s <image> <camx> <digs> <places> <crafts> "
                        "<inv_dirt> <inv_stone> <delta_count> <tdigest>\n",
                argv[0]);
        return 2;
    }

    f = fopen(argv[1], "rb");
    assert(f && "cannot open guest image");
    n = (long)fread(img, 1, sizeof img, f);
    assert(n > 0 && (size_t)n <= sizeof img && "image must fit buffer");
    fclose(f);
    img_size = (size_t)n;

    sym_camx    = (int32_t)strtoul(argv[2], NULL, 16);
    sym_digs    = (int32_t)strtoul(argv[3], NULL, 16);
    sym_places  = (int32_t)strtoul(argv[4], NULL, 16);
    sym_crafts  = (int32_t)strtoul(argv[5], NULL, 16);
    sym_dirt    = (int32_t)strtoul(argv[6], NULL, 16);
    sym_stone   = (int32_t)strtoul(argv[7], NULL, 16);
    sym_dcount  = (int32_t)strtoul(argv[8], NULL, 16);
    sym_tdigest = (int32_t)strtoul(argv[9], NULL, 16);

    printf("--- pass A: record (scripted scenario) ---\n");
    pass_record();
    memcpy(fbA, m.framebuffer, sizeof fbA);
    {
        const uint64_t final_sh = shist[TOTAL_FRAMES - 1u];
        const uint64_t cont_hash = rhist[TOTAL_FRAMES - 1u];

        printf("--- pass B: replay from input log (%u bytes) ---\n",
               TOTAL_FRAMES);
        pass_replay();
        CHECK(state_hash(&m.cpu, &m.bus), final_sh,
              "replay final state_hash != recorded");

        printf("--- pass C: load snapshot at frame %u, continue ---\n",
               SNAP_FRAME);
        pass_load();
        CHECK(ram_hash(), cont_hash, "loaded final RAM != recorded RAM");
        CHECK(memcmp(fbA, m.framebuffer, sizeof fbA),
              0, "loaded final framebuffer != recorded");
        CHECK(guest32(sym_camx),    30u, "loaded camx");
        CHECK(guest32(sym_digs),    4u,  "loaded digs");
        CHECK(guest32(sym_places),  2u,  "loaded places");
        CHECK(guest32(sym_crafts),  1u,  "loaded crafts");
        CHECK(guest32(sym_dirt),    0u,  "loaded inv_dirt");
        CHECK(guest32(sym_stone),   1u,  "loaded inv_stone");
        CHECK(guest32(sym_dcount),  2u,  "loaded delta_count");
        /* same guest + same script as P4, so the loaded world's table
         * digest must be exactly P4's pinned value */
        CHECK(guest32(sym_tdigest), 0x2AA3A3F1u, "loaded tdigest");

        fflush(stdout);
        printf("final state_hash=0x%016llX continuation ram_hash=0x%016llX\n",
               (unsigned long long)final_sh, (unsigned long long)cont_hash);
        fflush(stdout);
        CHECK(final_sh, GOLDEN_FINAL_STATE,
              "final state hash golden drifted");
        CHECK(cont_hash, GOLDEN_CONT_HASH,
              "continuation RAM digest golden drifted");

        printf("save sizes: input log %u B; full RAM restore %u B; "
               "game save (seed 0 B + %u deltas x 12 B + 8 scalars x 4 B) "
               "= %u B; 128-delta budget = %u B\n",
               TOTAL_FRAMES, (unsigned)ARCADE1_RAM_SIZE,
               (unsigned)guest32(sym_dcount),
               (unsigned)guest32(sym_dcount) * 12u + 32u,
               128u * 12u + 32u);
        printf("voxel-P5: persistence ok (%u frames recorded, %u replayed "
               "bit-exact, %u loaded+continued bit-exact)\n",
               passA_frames, TOTAL_FRAMES, cont_frames);
    }
    return 0;
}
