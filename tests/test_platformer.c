/* ARCADE-1 — Milestone 22 test harness: the platformer as guest firmware.
 *
 * Boots platformer.c on the machine's OWN CPU (arcade1_set_firmware) and drives
 * it frame-by-frame with a scripted RetroPad, proving the integration
 * milestone: one game exercising tilemaps, sprites, scrolling, collision,
 * animation, multiple layers, input and audio together.
 *
 * It is a physics game, so (like the Breakout/Tetris harnesses) it checks the
 * GAME STATE by reading the guest's globals straight out of RAM (addresses
 * from the ELF symbol table, passed on the command line) and asserts the
 * gameplay story, rather than re-rendering every pixel:
 *
 *   P0  boot + land      the player spawns above the ground, falls under
 *                        gravity and lands (onground -> 1, py settles on the
 *                        surface); camera clamps vertically; the layers render
 *                        (sky / parallax hills / terrain / player / coin);
 *                        audio starts silent
 *   P1  walk + coin      holding RIGHT walks the player right (px increases),
 *                        collecting a ground coin (score +1, coin beep) and
 *                        the camera begins to follow (cam_x > 0)
 *   P2  jump + land      pressing A on the ground launches the player (vy < 0,
 *                        onground -> 0), it rises then falls and lands again
 *                        (onground -> 1, py back on the surface); jump beep
 *   P3  walk to goal     holding RIGHT to the far side reaches the magenta
 *                        goal flag (state -> WIN), the camera follows and
 *                        clamps at the world's right edge, goal beep
 *   P4  restart          pressing A from WIN rebuilds the level and respawns
 *                        (state -> PLAY, score -> 0)
 *
 * Plus per-frame determinism hashes across two reset-and-compare runs and a
 * byte-identical final framebuffer, plus timer/present/stray accounting.
 *
 * Usage: test_platformer <platformer.arc1|bin> <px> <py> <onground> <score>
 *                        <state> <cam_x> <cam_y>
 */

#include "machine.h"    /* brings in memory/timer/video/audio/exec */
#include "rom.h"        /* M17: .arc1 image parsing */
#include "state_hash.h"
#include "video_budget.h"   /* task C: per-frame video work avg/max */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Video/audio/timer register offsets. Passed to reg(), which adds
 * ARCADE1_MMIO_BASE to reach the muxed window. */
#define A_TIMER ARCADE1_TIMER_REG_COUNT
#define A_FREQ  ARCADE1_AUDIO_REG_FREQ
#define A_CTRL  ARCADE1_AUDIO_REG_CTRL

/* --- Constants mirrored from platformer.c (kept in sync by hand) --------- */
#define TILE 16
#define PW   12
#define PH   14
#define WORLD_W 640
#define WORLD_H 256
#define VIEW_W  320
#define VIEW_H  240

#define COL_SKY  0x2E7Fu
#define COL_HILL 0x1C06u
#define COL_GND  0x8260u
#define COL_PLR  0xFD20u
#define COL_COIN 0xFFE0u
#define COL_HUD  0xFFFFu

#define BE_JUMP 660u
#define BE_COIN 880u
#define BE_GOAL 440u

#define ST_PLAY 0
#define ST_WIN  1

#define GROUND_TOP (14 * TILE)      /* world y of the ground surface */

/* --- Guest state ----------------------------------------------------------- */
static arcade1_machine_t m;
static uint8_t  img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
static size_t   img_size;
static int32_t  sym_px, sym_py, sym_onground, sym_score;
static int32_t  sym_state, sym_cam_x, sym_cam_y;
static uint16_t fb1[ARCADE1_FB_PIXELS];  /* run-1 final framebuffer           */
static int32_t  px_after_walk;           /* px after P1, pinned for P2       */

static uint64_t hist1[2048];
static unsigned nhist1, frame_count;
static bool     verify_hashes;
static uint64_t total_inst, max_inst;

/* Task C: measured per-frame video work (avg + max) over the scripted run. */
static video_budget_t vbudget;
static unsigned frames_this_pass;

static uint32_t reg(uint32_t off)
{
    return arcade1_read32(&m.bus, ARCADE1_MMIO_BASE + off);
}

/* Read a game global out of guest RAM. */
static int32_t guest(int32_t addr)
{
    return (int32_t)arcade1_read32(&m.bus, (uint32_t)addr);
}

static void set_pad(unsigned mask)
{
    arcade1_input_t in;
    memset(&in, 0, sizeof in);
    in.up     = 0;
    in.down   = 0;
    in.left   = (mask & 0x04u) != 0;
    in.right  = (mask & 0x08u) != 0;
    in.a      = (mask & 0x10u) != 0;
    in.b      = 0;
    in.start  = 0;
    in.select = 0;
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

static bool pred_onground(void)      { return guest(sym_onground) != 0; }
static bool pred_win(void)           { return guest(sym_state) == ST_WIN; }

/* Does the framebuffer contain at least one pixel of this colour? */
static bool screen_has(uint16_t color)
{
    const uint16_t *fb = arcade1_framebuffer(&m);
    unsigned i;
    for (i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
        if (fb[i] == color)
            return true;
    return false;
}

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
    total_inst = max_inst = 0;
    frames_this_pass = 0;
    memset(&vbudget, 0, sizeof vbudget);   /* task C: fresh per pass */
}

/* --- The scripted game ---------------------------------------------------- */

static void scenario(bool verify)
{
    verify_hashes = verify;
    boot_machine();

    /* ---- P0: boot + land ------------------------------------------------ */
    /* First frame is silent (no beep at boot). */
    run_frames(1, 0);
    assert_silence();

    /* The player spawns above ground and falls; give it a moment to land. */
    run_until(0, pred_onground, "player lands after spawning", 40);
    assert(guest(sym_py) == GROUND_TOP - PH &&
           "player must settle exactly on the ground surface");
    assert(guest(sym_px) == 2 * TILE && "player must not drift while falling");
    assert(guest(sym_state) == ST_PLAY);
    assert(guest(sym_score) == 0);
    /* Camera: player near the left edge -> cam_x 0; clamped vertically. */
    assert(guest(sym_cam_x) == 0 && "camera must not pan before the midpoint");
    assert(guest(sym_cam_y) == WORLD_H - VIEW_H &&
           "camera clamps to the world's bottom");

    /* The layers all render: sky (clear), parallax hills (background), terrain
     * (tilemap), the player and a coin (sprites). */
    assert(screen_has(COL_SKY)  && "sky layer must render");
    assert(screen_has(COL_HILL) && "parallax background layer must render");
    assert(screen_has(COL_GND)  && "terrain tilemap must render");
    assert(screen_has(COL_PLR)  && "player sprite must render");
    assert(screen_has(COL_COIN) && "coin sprite must render");

    /* ---- P1: walk right + collect the ground coin ----------------------- */
    {
        const int32_t x0 = guest(sym_px);
        run_frames(20, 0x08u);                 /* hold RIGHT */
        assert(guest(sym_px) == x0 + 20 * 2 &&
               "walking right must advance px by 2/frame");
        assert(guest(sym_score) >= 1 &&
               "walking over a ground coin must collect it");
        assert(guest(sym_cam_y) == WORLD_H - VIEW_H);
        px_after_walk = guest(sym_px);         /* pinned for the jump check */
    }

    /* ---- P2: jump then land -------------------------------------------- */
    {
        run_frames(1, 0x10u);                  /* press A for one frame */
        assert(guest(sym_onground) == 0 && "jump must leave the ground");
        /* Read the beep the jump produced. */
        assert(reg(A_FREQ) == BE_JUMP && "jump must beep at its tone");
        assert(reg(A_CTRL) == 1u);
        assert_square();
        run_until(0, pred_onground, "player lands after a jump", 60);
        assert(guest(sym_py) == GROUND_TOP - PH &&
               "player must land back on the surface");
        assert(guest(sym_px) == px_after_walk &&
               "no horizontal input -> px unchanged by the jump");
    }

    /* ---- P3: walk all the way to the goal -------------------------------- */
    run_until(0x08u, pred_win, "player reaches the goal", 500);
    assert(reg(A_FREQ) == BE_GOAL && "reaching the goal must beep");
    assert(reg(A_CTRL) == 1u);
    assert(guest(sym_cam_x) == WORLD_W - VIEW_W &&
           "camera clamps at the world's right edge");
    /* All three ground-path coins were collected on the way. */
    assert(guest(sym_score) == 3 && "all ground coins collected by the walk");

    /* ---- P4: press A from WIN to restart --------------------------------- */
    run_frames(1, 0x10u);
    assert(guest(sym_state) == ST_PLAY && "A restarts from WIN");
    assert(guest(sym_score) == 0 && "restart resets the score");

    /* ---- Accounting ------------------------------------------------------ */
    assert(m.timer.count == frame_count && "timer == frames run");
    assert(m.video.presents == frame_count && "one present per frame");
    assert(m.bus.stray_accesses == 0 && "guest must not stray outside memory");

    printf("cpu load: %llu inst/frame avg, %llu max (%u frames)\n",
           (unsigned long long)(total_inst / frames_this_pass),
           (unsigned long long)max_inst, frames_this_pass);
    video_budget_report("platformer", &vbudget);   /* task C */
}

int main(int argc, char **argv)
{
    FILE *f;
    long n;

    if (argc != 9) {
        fprintf(stderr,
                "usage: %s <image.arc1|bin> <px> <py> <onground> <score> "
                "<state> <cam_x> <cam_y>\n", argv[0]);
        return 2;
    }

    f = fopen(argv[1], "rb");
    assert(f && "cannot open guest image");
    n = (long)fread(img, 1, sizeof img, f);
    assert(n > 0 && (size_t)n <= sizeof img && "ROM image must fit 4 KiB");
    fclose(f);
    img_size = (size_t)n;

    sym_px       = (int32_t)strtoul(argv[2],  NULL, 16);
    sym_py       = (int32_t)strtoul(argv[3],  NULL, 16);
    sym_onground = (int32_t)strtoul(argv[4],  NULL, 16);
    sym_score    = (int32_t)strtoul(argv[5],  NULL, 16);
    sym_state    = (int32_t)strtoul(argv[6],  NULL, 16);
    sym_cam_x    = (int32_t)strtoul(argv[7],  NULL, 16);
    sym_cam_y    = (int32_t)strtoul(argv[8],  NULL, 16);

    printf("--- pass 1: scripted playthrough ---\n");
    scenario(false);
    nhist1 = frame_count;
    memcpy(fb1, m.framebuffer, sizeof fb1);

    printf("--- pass 2: reset and replay ---\n");
    scenario(true);
    assert(frame_count == nhist1 && "replay frame count diverged");
    assert(memcmp(fb1, m.framebuffer, sizeof fb1) == 0 &&
           "framebuffer diverged after reset");

    printf("milestone-22: platformer ok (%u frames, two identical runs)\n",
           nhist1);
    printf("state hash: 0x%016llX\n",
           (unsigned long long)hist1[nhist1 - 1]);
    return 0;
}
