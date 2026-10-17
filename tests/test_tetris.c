/* ARCADE-1 — Milestone 18 test harness: Tetris as guest firmware.
 *
 * Boots tetris.c on the machine's OWN CPU (arcade1_set_firmware) and drives
 * it frame-by-frame with a scripted RetroPad, proving the docs.md M18
 * stress list end to end: grid graphics, repeated objects, text, timer
 * determinism and game state — all on the existing M14 API (no sprites).
 *
 * The script walks the whole game story and checks state by reading the
 * game globals straight out of guest RAM (addresses from the ELF symbol
 * table, passed on the command line), plus exact framebuffer probes:
 *
 *   P0  boot          state=PLAY, the pinned first pieces of the fixed-seed
 *                     PRNG (cur=L, next=O), field empty, gravity armed for
 *                     tick 30, silent boot, screen drawn (frame, floor, the
 *                     L and T piece cells, SCORE/'0'/NEXT-preview pixels)
 *   P1  gravity       no input: the timer alone steps the piece down
 *   P2  four locks    soft-drop the stream L O L Z: each lock advances the
 *                     PRNG stream, the four pieces land in their exact cells
 *                     (checked byte-for-byte), lock beep 440 Hz square wave
 *   P3  T controls    A rotates (cells move on screen), a rotation that
 *                     would hit filled cells is rejected, left clamp at the
 *                     wall, right clamp at the wall
 *   P4  line clear    field rebuilt (marker row + bottom row with one gap),
 *                     the T drops into the gap: one line clears, the marker
 *                     row compacts one row down (exact 200-byte compare),
 *                     score 100, clear beep 880 Hz
 *   P5  game over     bottom pre-filled (cols 0-1 left open so no row can
 *                     complete): pieces stack until a spawn is blocked,
 *                     state=OVER, 110 Hz beep, GAME OVER / PRESS START text
 *                     pixels
 *   P6  idle + START  beep expires into silence; START resets everything to
 *                     the boot state, including the PRNG (restart replays
 *                     the same game)
 *   P7  settle        timer == frames, presents == frames, no strays
 *
 * Determinism: the whole script runs twice — after a plain arcade1_reset the
 * replay must produce the identical per-frame state-hash sequence (printed
 * one hash per frame, per docs.md "Games") and a byte-identical framebuffer.
 *
 * Probes/constants below are shared with programs/tetris.c and kept in sync
 * by hand, like the other guest tests.
 */

#include "machine.h"    /* brings in memory/timer/video/audio/exec */
#include "rom.h"        /* M17: .arc1 image parsing */

#include "state_hash.h"
#include "video_budget.h"   /* task C: per-frame video work avg/max */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Shared with programs/tetris.c ---------------------------------------- */

#define PAD_DOWN   0x02u
#define PAD_LEFT   0x04u
#define PAD_RIGHT  0x08u
#define PAD_A      0x10u
#define PAD_START  0x40u

#define ST_PLAY  1
#define ST_OVER  2

#define COL_BG     0x0000u
#define COL_FRAME  0x8410u
#define COL_FLOOR  0x1082u
#define COL_TEXT   0xFFFFu
#define COL_NUM    0x07E0u
#define COL_OVER   0xF800u

#define COL_I 0x07FFu          /* piece colours, indexed by piece id       */
#define COL_O 0xFFE0u
#define COL_T 0xF81Fu
#define COL_Z 0xF800u
#define COL_L 0xFD20u

#define COLS  10
#define ROWS  20

#define SEED  0x1234ABCDu      /* tetris.c's fixed xorshift32 seed         */

/* M20: the playfield IS the tilemap. Each cell is a 32-bit word — 0 = empty,
 * else 1u | (pcol[piece id] << 16): tile index 1 plus a per-cell colour.
 * The field still stores pieces, so the script keeps thinking in the OLD
 * piece-id+1 values (1..7); CELL(pid) converts one into the cell word it
 * now lives as. pcol[] mirrors tetris.c's palette (I O T S Z J L). */
static const uint16_t pcol[7] = {
    0x07FFu, 0xFFE0u, 0xF81Fu, 0x07E0u, 0xF800u, 0x001Fu, 0xFD20u
};
#define CELL(pid) (1u | ((uint32_t)pcol[(pid) - 1] << 16))

#define MMIO_BASE 0x4000u
#define PIX(x, y) ((size_t)(y) * ARCADE1_WIDTH + (size_t)(x))

/* Piece boxes, identical to tetris.c's shape[7][4] (row*4+col cells). */
static const uint8_t shape[7][4] = {
    { 4, 5, 6, 7 },   /* I */
    { 0, 1, 4, 5 },   /* O */
    { 1, 4, 5, 6 },   /* T */
    { 1, 2, 4, 5 },   /* S */
    { 0, 1, 5, 6 },   /* Z */
    { 0, 4, 5, 6 },   /* J */
    { 2, 4, 5, 6 },   /* L */
};

static arcade1_machine_t m;
static uint16_t fb1[ARCADE1_FB_PIXELS];       /* pass-1 framebuffer          */

static uint8_t img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
static size_t  img_size;

/* Guest symbol addresses (RAM) handed in by the Makefile via nm. */
static int32_t sym_score, sym_lines, sym_level, sym_state, sym_locked;
static int32_t sym_field, sym_cur, sym_next, sym_rot, sym_x, sym_y, sym_prng;

/* Per-frame hashing (docs.md "Games": one hash per frame, diffable). */
static uint64_t hist1[2048];
static unsigned nhist1;
static unsigned frame_count;
static bool     verify_hashes;

/* CPU load (docs.md "Games": instructions executed per frame, as a number). */
static uint64_t total_inst, max_inst;
static unsigned frames_this_pass;

/* Task C: measured per-frame video work (avg + max) over the scripted run. */
static video_budget_t vbudget;

/* run_until() targets. */
static int32_t want_locked, want_y, want_x;

/* --- Helpers -------------------------------------------------------------- */

static void set_pad(unsigned mask)
{
    arcade1_input_t in;
    memset(&in, 0, sizeof in);
    in.down   = (mask & PAD_DOWN)   != 0;
    in.left   = (mask & PAD_LEFT)   != 0;
    in.right  = (mask & PAD_RIGHT)  != 0;
    in.a      = (mask & PAD_A)      != 0;
    in.start  = (mask & PAD_START)  != 0;
    arcade1_set_input(&m, &in);
}

static void run_frame(void)
{
    const uint64_t before = m.cpu.instret;
    uint64_t delta, h;

    arcade1_run_frame(&m);

    delta = m.cpu.instret - before;
    total_inst += delta;
    if (delta > max_inst)
        max_inst = delta;
    frames_this_pass++;
    video_budget_sample(&vbudget, &m, delta);   /* task C */

    h = state_hash(&m.cpu, &m.bus);
    printf("frame %4u hash=0x%016llX fb=0x%016llX\n", frame_count,
           (unsigned long long)h, (unsigned long long)fb_hash(&m));
    if (verify_hashes) {
        assert(frame_count < nhist1 && "replay longer than the original run");
        assert(h == hist1[frame_count] && "per-frame hash diverged");
    } else {
        assert(frame_count < sizeof hist1 / sizeof hist1[0]);
        hist1[frame_count] = h;
    }
    frame_count++;
}

static void run_frames(unsigned n, unsigned mask)
{
    set_pad(mask);
    while (n--)
        run_frame();
}

/* Run frames with `mask` held until pred() holds; fail if it takes too long. */
typedef bool (*pred_fn)(void);
static void run_until(unsigned mask, pred_fn pred, const char *what,
                      unsigned cap)
{
    const unsigned start = frame_count;
    set_pad(mask);
    while (!pred()) {
        if (frame_count - start >= cap) {
            fprintf(stderr, "FAIL: %s not reached within %u frames\n",
                    what, cap);
            assert(0);
        }
        run_frame();
    }
}

/* Read a game global out of guest RAM. */
static int32_t guest(int32_t addr)
{
    return (int32_t)arcade1_read32(&m.bus, (uint32_t)addr);
}

static uint32_t reg(uint32_t off)
{
    return arcade1_read32(&m.bus, MMIO_BASE + off);
}

/* The playfield: 200 32-bit tilemap cells in guest RAM, read/written directly
 * (address is sym_field + i*4, since field[] is now an unsigned int array). */
static uint32_t fget(int i)
{
    return arcade1_read32(&m.bus, (uint32_t)(sym_field + i * 4));
}

static void fset(int i, uint32_t v)
{
    arcade1_write32(&m.bus, (uint32_t)(sym_field + i * 4), v);
}

static void field_clear(void)
{
    int i;
    for (i = 0; i < COLS * ROWS; i++)
        fset(i, 0);
}

static void assert_field(const uint32_t *want)
{
    int i;
    for (i = 0; i < COLS * ROWS; i++)
        assert(fget(i) == want[i] && "playfield cells diverged");
}

static bool pred_locked(void) { return guest(sym_locked) >= want_locked; }
static bool pred_y(void)      { return guest(sym_y) >= want_y; }
static bool pred_x(void)      { return guest(sym_x) == want_x; }
static bool pred_over(void)   { return guest(sym_state) == ST_OVER; }

/* tetris.c's xorshift32 step (kept in sync by hand). */
static uint32_t xs(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

/* tetris.c's rotation, mirrored: cell (r,c) -> (c,3-r), rot times. */
static void rotcells(int id, int rot, uint8_t out[4])
{
    int i, k;
    for (i = 0; i < 4; i++) {
        int r = shape[id][i] / 4, c = shape[id][i] % 4;
        if (id != 1)                        /* O never rotates */
            for (k = 0; k < rot; k++) {
                int t = r;
                r = c;
                c = 3 - t;
            }
        out[i] = (uint8_t)(r * 4 + c);
    }
}

static void boot_machine(void)
{
    /* M17: the ROM file is a proper `.arc1` image (what `make program`
     * now produces); accept a raw flat binary too. Either way exactly the
     * payload reaches the ROM. */
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
    total_inst = max_inst = 0;
    frames_this_pass = 0;
    memset(&vbudget, 0, sizeof vbudget);   /* task C: fresh per pass */
}

/* The frame's audio must be a clean +/-8000 square wave with both
 * polarities — the event beep that is active this frame. */
static void assert_square(void)
{
    unsigned i, pos = 0, neg = 0;
    for (i = 0; i < ARCADE1_AUDIO_FRAMES * 2; i++) {
        if (m.audio[i] == 8000)
            pos++;
        else if (m.audio[i] == -8000)
            neg++;
        else
            assert(!"audio must be a clean +/-8000 square wave");
    }
    assert(pos > 0 && neg > 0);
}

static void assert_silence(void)
{
    unsigned i;
    for (i = 0; i < ARCADE1_AUDIO_FRAMES * 2; i++)
        assert(m.audio[i] == 0 && "audio must be silent while CTRL is off");
}

/* --- The scripted game ---------------------------------------------------- */

static uint32_t want[COLS * ROWS];  /* expected playfield cells (tilemap)   */

static void run_script(bool verify)
{
    int i, c, r;
    uint8_t c4[4];
    int maxr;

    verify_hashes = verify;
    boot_machine();

    /* P0: boot. crt0 + main init + the first draw must all fit frame 0;
     * the machine runs the guest until it presents, once per frame. */
    run_frames(2, 0);
    assert(guest(sym_state) == ST_PLAY);
    assert(guest(sym_score) == 0);
    assert(guest(sym_lines) == 0);
    assert(guest(sym_level) == 0);
    assert(guest(sym_locked) == 0);
    assert(guest(sym_cur) == 6);                 /* pinned stream: L O L Z */
    assert(guest(sym_next) == 1);                /* ...first two pieces    */
    assert(guest(sym_rot) == 0);
    assert(guest(sym_x) == 3 && guest(sym_y) == 0);
    assert((uint32_t)guest(sym_prng) == xs(xs(SEED)));
    for (i = 0; i < COLS * ROWS; i++)
        assert(fget(i) == 0);
    assert(m.video.presents == 2);               /* 1 per frame            */
    assert(reg(0x200) == 0);                     /* INPUT empty            */
    assert(reg(0x300) == 440);                   /* audio device default   */
    assert(reg(0x304) == 0);                     /* ...silenced at boot    */
    assert_silence();
    assert(m.framebuffer[PIX(8, 8)]    == COL_FRAME);   /* playfield frame */
    assert(m.framebuffer[PIX(9, 9)]    == COL_FRAME);
    assert(m.framebuffer[PIX(10, 10)]  == COL_FLOOR);   /* empty cell      */
    assert(m.framebuffer[PIX(60, 10)]  == COL_L);       /* L cell (0,2)    */
    assert(m.framebuffer[PIX(40, 20)]  == COL_L);       /* L cell (1,0)    */
    assert(m.framebuffer[PIX(60, 20)]  == COL_L);       /* L cell (1,2)    */
    assert(m.framebuffer[PIX(50, 10)]  == COL_FLOOR);   /* T is NEXT, only */
                                                         /* in the preview  */
    assert(m.framebuffer[PIX(130, 14)] == COL_TEXT);    /* 'S' of SCORE    */
    assert(m.framebuffer[PIX(170, 26)] == COL_NUM);     /* score '0'       */
    assert(m.framebuffer[PIX(148, 130)] == COL_FRAME);  /* preview box     */
    assert(m.framebuffer[PIX(150, 132)] == COL_O);      /* next = O        */
    assert(m.framebuffer[PIX(300, 230)] == COL_BG);     /* empty corner    */

    /* P1: gravity — no input, the M12 timer alone steps the piece down. */
    want_y = 1;
    run_until(0, pred_y, "first gravity step", 40);
    assert(guest(sym_y) == 1 && guest(sym_locked) == 0);

    /* P2: four locks. Soft-drop the L O L Z stream onto the floor; after
     * every lock the playfield must hold exactly the expected cells. */
    want_locked = 1;
    run_until(PAD_DOWN, pred_locked, "first lock", 100);
    assert(guest(sym_locked) == 1);
    assert(guest(sym_cur) == 1);                 /* stream: next is O     */
    assert(guest(sym_lines) == 0 && guest(sym_score) == 0);
    assert(reg(0x300) == 440 && reg(0x304) == 1);/* lock beep             */
    assert_square();
    memset(want, 0, sizeof want);
    want[18 * COLS + 5] = CELL(7);                /* L: (0,2),(1,0..2)     */
    want[19 * COLS + 3] = CELL(7);
    want[19 * COLS + 4] = CELL(7);
    want[19 * COLS + 5] = CELL(7);
    assert_field(want);

    want_locked = 2;
    run_until(PAD_DOWN, pred_locked, "second lock", 100);
    assert(guest(sym_cur) == 6);                 /* stream: next is L     */
    assert(reg(0x300) == 440);
    want[17 * COLS + 3] = CELL(2);                /* O lands on the L      */
    want[17 * COLS + 4] = CELL(2);
    want[18 * COLS + 3] = CELL(2);
    want[18 * COLS + 4] = CELL(2);
    assert_field(want);

    want_locked = 3;
    run_until(PAD_DOWN, pred_locked, "third lock", 100);
    assert(guest(sym_cur) == 4);                 /* stream: next is Z      */
    assert(reg(0x300) == 440);
    want[15 * COLS + 5] = CELL(7);                /* L drops into a notch  */
    want[16 * COLS + 3] = CELL(7);
    want[16 * COLS + 4] = CELL(7);
    want[16 * COLS + 5] = CELL(7);
    assert_field(want);
    assert(guest(sym_lines) == 0);               /* no row full yet       */

    want_locked = 4;
    run_until(PAD_DOWN, pred_locked, "fourth lock", 100);
    assert(guest(sym_cur) == 2);                 /* T: P3 plays with it   */
    assert(reg(0x300) == 440);
    want[13 * COLS + 3] = CELL(5);                /* Z on top              */
    want[13 * COLS + 4] = CELL(5);
    want[14 * COLS + 4] = CELL(5);
    want[14 * COLS + 5] = CELL(5);
    assert_field(want);
    assert(guest(sym_lines) == 0 && guest(sym_score) == 0);

    /* P3: T controls. Rotate on A — the cells must move on screen. */
    assert(guest(sym_rot) == 0);
    assert(m.framebuffer[PIX(50, 10)] == COL_T);       /* rot0: cell (0,1) */
    assert(m.framebuffer[PIX(60, 10)] == COL_FLOOR);   /* rot0: no (0,2)   */
    run_frames(1, PAD_A);
    assert(guest(sym_rot) == 1);
    assert(m.framebuffer[PIX(50, 10)] == COL_FLOOR);   /* (0,1) vacated    */
    assert(m.framebuffer[PIX(60, 10)] == COL_T);       /* (0,2) appeared   */
    assert(m.framebuffer[PIX(70, 20)] == COL_T);       /* (1,3) appeared   */

    /* Left to the wall. */
    want_x = 0;
    run_until(PAD_LEFT, pred_x, "left wall clamp", 60);
    assert(guest(sym_x) == 0);

    /* A rotation into filled cells must be rejected (state restored). */
    rotcells(guest(sym_cur), 2, c4);             /* the rotation A tries   */
    {
        const int cx = guest(sym_x), cy = guest(sym_y);
        uint32_t saved[4];
        for (i = 0; i < 4; i++) {                /* block the new shape    */
            r = cy + c4[i] / 4;
            c = cx + c4[i] % 4;
            if (r >= 0 && r < ROWS && c >= 0 && c < COLS) {
                saved[i] = fget(r * COLS + c);
                fset(r * COLS + c, 1u);           /* tile 1, colour 0       */
            }
        }
        run_frames(1, PAD_A);
        assert(guest(sym_rot) == 1 && "rejected rotation must not advance");
        for (i = 0; i < 4; i++) {                /* restore the cells       */
            r = cy + c4[i] / 4;
            c = cx + c4[i] % 4;
            if (r >= 0 && r < ROWS && c >= 0 && c < COLS)
                fset(r * COLS + c, saved[i]);
        }
    }

    /* Held against the wall: the box origin may hang off-screen to the
     * left, but the CELLS clamp at column 0 — rot1 T cells start at box
     * column 2, so the origin settles at -2. */
    run_frames(15, PAD_LEFT);
    assert(guest(sym_x) == -2 && "cells must clamp at column 0");
    assert(guest(sym_locked) == 4);              /* still airborne         */

    /* Right to the wall (rotated T spans box columns 2..3). */
    want_x = 6;
    run_until(PAD_RIGHT, pred_x, "right wall clamp", 60);
    assert(guest(sym_x) == 6);
    assert(guest(sym_locked) == 4);

    /* P4: line clear. Rebuild the field: a marker row (cols 4..9, leaving
     * the descent corridor cols 2..3 open) and a bottom row with a gap
     * exactly where this piece's bottom cells will land. */
    field_clear();
    for (c = 4; c < COLS; c++)
        fset(10 * COLS + c, CELL(7));

    want_x = 0;
    run_until(PAD_LEFT, pred_x, "T back to the wall", 60);
    assert(guest(sym_x) == 0);

    rotcells(guest(sym_cur), guest(sym_rot), c4);
    maxr = c4[0] / 4;
    for (i = 1; i < 4; i++)
        if (c4[i] / 4 > maxr)
            maxr = c4[i] / 4;
    for (c = 0; c < COLS; c++)
        fset((ROWS - 1) * COLS + c, CELL(7));
    for (i = 0; i < 4; i++)                      /* gaps under bottom cells */
        if (c4[i] / 4 == maxr)
            fset((ROWS - 1) * COLS + c4[i] % 4, 0);

    want_y = ROWS - 1 - maxr;                    /* resting row of the T   */
    run_until(PAD_DOWN, pred_y, "T reaches the bottom row", 80);
    assert(guest(sym_y) == want_y);
    assert(guest(sym_locked) == 4);

    /* Expected playfield after the lock + clear: the bottom row (now full)
     * vanishes, everything above shifts down one — the marker row lands on
     * 11, the T cells land on 18/19, the gap cell dies with its row. */
    memset(want, 0, sizeof want);
    for (c = 4; c < COLS; c++)
        want[11 * COLS + c] = CELL(7);            /* marker row: 10 -> 11   */
    for (i = 0; i < 4; i++) {
        int fr = want_y + c4[i] / 4;
        int fc = c4[i] % 4;
        if (fr != ROWS - 1)
            want[(fr + 1) * COLS + fc] = CELL((uint32_t)guest(sym_cur) + 1);
    }

    want_locked = 5;
    run_until(PAD_DOWN, pred_locked, "the clearing lock", 10);
    assert(guest(sym_lines) == 1);
    assert(guest(sym_score) == 100);             /* 100 * (level + 1)     */
    assert(guest(sym_level) == 0);
    assert(reg(0x300) == 880 && reg(0x304) == 1);/* clear beep            */
    assert_square();
    assert_field(want);

    /* P5: game over. Fill rows 8..19 (cols 0..1 stay open so no row can
     * ever complete) and soft-drop until a spawn is blocked. */
    for (r = 8; r < ROWS; r++)
        for (c = 2; c < COLS; c++)
            fset(r * COLS + c, CELL(7));
    assert(guest(sym_cur) == 5);                 /* J next in the stream  */
    assert(guest(sym_locked) == 5);

    run_until(PAD_DOWN, pred_over, "game over", 600);
    assert(guest(sym_state) == ST_OVER);
    assert(guest(sym_locked) >= 6);
    assert(guest(sym_lines) == 1 && guest(sym_score) == 100); /* no clears */
    assert(reg(0x300) == 110 && reg(0x304) == 1);/* game-over beep        */
    assert_square();
    assert(m.framebuffer[PIX(106, 100)] == COL_OVER);  /* 'G' of GAME OVER */
    assert(m.framebuffer[PIX(96, 120)] == COL_TEXT);   /* 'P' of PRESS ...  */

    /* P6: idle, then START. */
    run_frames(10, 0);
    assert(reg(0x304) == 0);
    assert_silence();

    run_frames(1, PAD_START);
    assert(guest(sym_state) == ST_PLAY);
    assert(guest(sym_score) == 0 && guest(sym_lines) == 0);
    assert(guest(sym_level) == 0 && guest(sym_locked) == 0);
    assert(guest(sym_cur) == 6 && guest(sym_next) == 1);
    assert(guest(sym_rot) == 0);
    assert(guest(sym_x) == 3 && guest(sym_y) == 0);
    assert((uint32_t)guest(sym_prng) == xs(xs(SEED)));  /* stream rewound  */
    for (i = 0; i < COLS * ROWS; i++)
        assert(fget(i) == 0);
    assert(reg(0x300) == 220 && reg(0x304) == 1);       /* restart beep    */
    assert_square();
    assert(m.framebuffer[PIX(60, 10)] == COL_L);        /* opening piece   */
    assert(m.framebuffer[PIX(106, 100)] == COL_FLOOR);  /* overlay gone    */

    /* P7: settle. Timer is pure frame time; the guest never strays. */
    run_frames(2, 0);
    assert(m.timer.count == frame_count);
    assert(m.video.presents == frame_count);
    assert(m.bus.stray_accesses == 0);

    printf("cpu load: %llu inst/frame avg, %llu max (%u frames)\n",
           (unsigned long long)(total_inst / frames_this_pass),
           (unsigned long long)max_inst, frames_this_pass);
    video_budget_report("tetris", &vbudget);   /* task C */
}

/* --- Entry point ----------------------------------------------------------- */

int main(int argc, char **argv)
{
    FILE *f;
    long n;

    assert(argc == 14 && "usage: test_tetris <image> <score> <lines> "
                         "<level> <state> <locked> <field> <cur> <next> "
                         "<rot> <cur_x> <cur_y> <prng> (hex symbol addrs)");

    f = fopen(argv[1], "rb");
    assert(f);
    n = (long)fread(img, 1, sizeof img, f);
    assert(n > 0 && (size_t)n <= sizeof img && "ROM image must fit 4 KiB");
    fclose(f);
    img_size = (size_t)n;

    sym_score  = (int32_t)strtoul(argv[2],  NULL, 16);
    sym_lines  = (int32_t)strtoul(argv[3],  NULL, 16);
    sym_level  = (int32_t)strtoul(argv[4],  NULL, 16);
    sym_state  = (int32_t)strtoul(argv[5],  NULL, 16);
    sym_locked = (int32_t)strtoul(argv[6],  NULL, 16);
    sym_field  = (int32_t)strtoul(argv[7],  NULL, 16);
    sym_cur    = (int32_t)strtoul(argv[8],  NULL, 16);
    sym_next   = (int32_t)strtoul(argv[9],  NULL, 16);
    sym_rot    = (int32_t)strtoul(argv[10], NULL, 16);
    sym_x      = (int32_t)strtoul(argv[11], NULL, 16);
    sym_y      = (int32_t)strtoul(argv[12], NULL, 16);
    sym_prng   = (int32_t)strtoul(argv[13], NULL, 16);

    /* Pass 1: run the script, recording the hash of every frame. */
    printf("--- pass 1: scripted game ---\n");
    run_script(false);
    nhist1 = frame_count;
    memcpy(fb1, m.framebuffer, sizeof fb1);

    /* Pass 2: arcade1_reset reboots the guest (ROM + firmware persist); the
     * identical script must yield the identical hash sequence and screen. */
    printf("--- pass 2: reset and replay ---\n");
    run_script(true);
    assert(frame_count == nhist1 && "replay frame count diverged");
    assert(memcmp(fb1, m.framebuffer, sizeof fb1) == 0 &&
           "framebuffer diverged after reset");

    printf("milestone-18: tetris ok (%u frames, two identical runs)\n",
           nhist1);
    printf("state hash: 0x%016llX\n",
           (unsigned long long)hist1[nhist1 - 1]);
    return 0;
}
