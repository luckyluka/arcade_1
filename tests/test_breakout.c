/* ARCADE-1 — Milestone 15 test harness: Breakout as guest firmware.
 *
 * Boots breakout.c on the machine's OWN CPU (arcade1_set_firmware) and drives
 * it frame-by-frame with a scripted RetroPad, proving the full docs.md M15
 * chain: guest game loop <-> INPUT_PAD/TIMER/AUDIO devices <-> video device
 * <-> framebuffer, one guest iteration per host frame.
 *
 * The script walks the whole game story and checks state by reading the four
 * game globals straight out of guest RAM (addresses from the ELF symbol
 * table, passed on the command line):
 *
 *   P0  boot          state=SERVE, score 0, lives 3, paddle centred, screen
 *                     drawn (bricks/score/lives/paddle pixels), INPUT reg
 *                     reads back the pad, audio device defaults + silence
 *   P1  paddle right  holding RIGHT moves the paddle (136 -> 196)
 *   P2  paddle left   holding LEFT moves it back (196 -> 176)
 *   P3  serve + A     state -> PLAY
 *   P4  first brick   score == 10 (bottom row), AUDIO_FREQ == 880, beep
 *                     active (CTRL=1) and the frame's samples are the 880 Hz
 *                     square wave
 *   P5  first death   lives == 2 (paddle parked left, ball lands right),
 *                     AUDIO_FREQ == 220
 *   P6  game over     lives == 0, score >= 30, AUDIO_FREQ == 110
 *   P7  idle          beep expired: CTRL == 0, samples all zero (silence)
 *   P8  START         restart: state=SERVE, score 0, lives 3, paddle centred,
 *                     AUDIO_FREQ == 440, field redrawn from scratch
 *   P9  settle        timer == frames run, no stray accesses
 *
 * Determinism: the whole script runs twice — after a plain arcade1_reset the
 * replay must produce the identical per-frame state-hash sequence (printed
 * one hash per frame, per docs.md "Games") and a byte-identical framebuffer.
 */

#include "machine.h"    /* brings in memory/timer/video/audio/exec */
#include "rom.h"        /* M17: .arc1 image parsing */

#include "state_hash.h"
#include "video_budget.h"   /* task C: per-frame video work avg/max */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Probes and constants shared with programs/breakout.c (kept in sync by
 * hand, like the other guest tests). */
#define COL_BRICK_ROW0 0xF800u  /* top brick row (red)  */
#define COL_HUD        0x07E0u  /* score digit green    */
#define COL_LIFE       0x07FFu  /* life rect aqua       */
#define COL_PADDLE     0xFFFFu  /* paddle white         */

#define PAD_UP     0x01u
#define PAD_DOWN   0x02u
#define PAD_LEFT   0x04u
#define PAD_RIGHT  0x08u
#define PAD_A      0x10u
#define PAD_B      0x20u
#define PAD_START  0x40u
#define PAD_SELECT 0x80u

#define ST_SERVE 0
#define ST_PLAY  1
#define ST_DEAD  2
#define ST_OVER  3
#define ST_WIN   4

#define MMIO_BASE ARCADE1_MMIO_BASE

#define PIX(x, y) ((size_t)(y) * ARCADE1_WIDTH + (size_t)(x))

static arcade1_machine_t m;
static uint16_t fb1[ARCADE1_FB_PIXELS];       /* pass-1 framebuffer          */

static uint8_t img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
static size_t  img_size;

/* Guest symbol addresses (RAM) handed in by the Makefile via nm. */
static int32_t sym_score, sym_lives, sym_state, sym_paddle;

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

/* --- Helpers -------------------------------------------------------------- */

static void set_pad(unsigned mask)
{
    arcade1_input_t in;
    memset(&in, 0, sizeof in);
    in.up     = (mask & PAD_UP)     != 0;
    in.down   = (mask & PAD_DOWN)   != 0;
    in.left   = (mask & PAD_LEFT)   != 0;
    in.right  = (mask & PAD_RIGHT)  != 0;
    in.a      = (mask & PAD_A)      != 0;
    in.b      = (mask & PAD_B)      != 0;
    in.start  = (mask & PAD_START)  != 0;
    in.select = (mask & PAD_SELECT) != 0;
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

static bool pred_score_positive(void) { return guest(sym_score) > 0; }
static bool pred_lives_two(void)      { return guest(sym_lives) == 2; }
static bool pred_game_over(void)      { return guest(sym_state) == ST_OVER; }

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

static void run_script(bool verify)
{
    verify_hashes = verify;
    boot_machine();

    /* P0: boot. crt0 + main init + the first draw must all fit in frame 0;
     * the machine runs the guest until it presents, once per frame. */
    run_frames(2, 0);
    assert(guest(sym_state) == ST_SERVE);
    assert(guest(sym_score) == 0);
    assert(guest(sym_lives) == 3);
    assert(guest(sym_paddle) == 136);                       /* centred     */
    assert(m.video.presents == 2);                          /* 1 per frame */
    assert(reg(0x200) == 0);                                /* INPUT empty */
    assert(reg(0x300) == 440);                              /* audio def.  */
    /* The device powers on enabled (the M05 demo tone), but the game
     * silences it in its boot frame: no tone until the first event. */
    assert(reg(0x304) == 0);
    assert_silence();
    assert(m.framebuffer[PIX(15, 26)]  == COL_BRICK_ROW0);  /* brick row0  */
    assert(m.framebuffer[PIX(4, 4)]    == COL_HUD);         /* score '0'   */
    assert(m.framebuffer[PIX(312, 4)]  == COL_LIFE);        /* life x3     */
    assert(m.framebuffer[PIX(146, 226)] == COL_PADDLE);     /* paddle 136  */
    assert(m.framebuffer[PIX(160, 120)] == 0);              /* empty bg    */

    /* P1: paddle right. */
    run_frames(30, PAD_RIGHT);
    assert(guest(sym_paddle) == 196);
    assert(reg(0x200) == PAD_RIGHT);                        /* pad readback */

    /* P2: paddle left. */
    run_frames(10, PAD_LEFT);
    assert(guest(sym_paddle) == 176);
    assert(reg(0x200) == PAD_LEFT);

    /* P3: serve — press A to launch. */
    run_frames(1, PAD_A);
    assert(guest(sym_state) == ST_PLAY);
    assert(reg(0x200) == PAD_A);

    /* P4: keep steering left; the fixed launch hits the bottom brick row. */
    run_until(PAD_LEFT | PAD_A, pred_score_positive, "first brick", 400);
    assert(guest(sym_score) == 10);      /* bottom row is worth 10          */
    assert(guest(sym_state) == ST_PLAY); /* ball still in flight            */
    assert(reg(0x300) == 880);           /* brick beep                      */
    assert(reg(0x304) == 1);             /* beep active this frame          */
    assert_square();                     /* ...and it is in this frame's PCM */

    /* P5: paddle stays left, ball lands right: first death. */
    run_until(PAD_LEFT | PAD_A, pred_lives_two, "first death", 400);
    assert(guest(sym_lives) == 2);
    assert(reg(0x300) == 220);           /* death beep                      */

    /* P6: two more lives, same fate: game over. */
    run_until(PAD_LEFT | PAD_A, pred_game_over, "game over", 600);
    assert(guest(sym_lives) == 0);
    assert(guest(sym_score) >= 30);      /* >= 1 brick per life             */
    assert(reg(0x300) == 110);           /* game-over beep                  */

    /* P7: idle — the 6-tick beep expires and the frame goes silent. */
    run_frames(10, 0);
    assert(reg(0x304) == 0);
    assert_silence();

    /* P8: START restarts from a fresh field. */
    run_frames(1, PAD_START);
    assert(guest(sym_state) == ST_SERVE);
    assert(guest(sym_score) == 0);
    assert(guest(sym_lives) == 3);
    assert(guest(sym_paddle) == 136);
    assert(reg(0x300) == 440);           /* restart beep                    */
    assert(m.framebuffer[PIX(15, 26)]  == COL_BRICK_ROW0); /* field reset   */
    assert(m.framebuffer[PIX(146, 226)] == COL_PADDLE);    /* paddle 136   */

    /* P9: settle. Timer is pure frame time; the guest never strays. */
    run_frames(2, 0);
    assert(m.timer.count == frame_count);
    assert(m.video.presents == frame_count);
    assert(m.bus.stray_accesses == 0);

    printf("cpu load: %llu inst/frame avg, %llu max (%u frames)\n",
           (unsigned long long)(total_inst / frames_this_pass),
           (unsigned long long)max_inst, frames_this_pass);
    video_budget_report("breakout", &vbudget);   /* task C */
}

/* --- Entry point ----------------------------------------------------------- */

int main(int argc, char **argv)
{
    FILE *f;
    long n;

    assert(argc == 6 && "usage: test_breakout <image> <score> <lives> "
                        "<state> <paddle_x> (hex symbol addresses)");

    f = fopen(argv[1], "rb");
    assert(f);
    n = (long)fread(img, 1, sizeof img, f);
    assert(n > 0 && (size_t)n <= sizeof img && "ROM image must fit 4 KiB");
    fclose(f);
    img_size = (size_t)n;

    sym_score  = (int32_t)strtoul(argv[2], NULL, 16);
    sym_lives  = (int32_t)strtoul(argv[3], NULL, 16);
    sym_state  = (int32_t)strtoul(argv[4], NULL, 16);
    sym_paddle = (int32_t)strtoul(argv[5], NULL, 16);

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

    printf("milestone-15: breakout ok (%u frames, two identical runs)\n",
           nhist1);
    printf("state hash: 0x%016llX\n",
           (unsigned long long)hist1[nhist1 - 1]);
    return 0;
}
