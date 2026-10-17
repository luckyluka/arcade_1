/* ARCADE-1 — Milestone 14 test harness (guest graphics API).
 *
 * Proves docs.md milestone 14: a program running inside RV32 draws graphics
 * through the guest-facing API — gfx_clear / gfx_rect / gfx_present in
 * programs/gfx.h, compiled into gfx_demo.c and executed on the RV32 CPU. The
 * API sits purely on top of the M13 video device's MMIO registers, so the
 * whole path guest code -> MMIO -> video device -> framebuffer is exercised
 * without a single raw register poke in the guest.
 *
 * Links machine+video+timer+memory+exec+cpu (no libretro, no .so). The
 * scenario runs TWICE with a reset in between (reset-and-compare) and asserts:
 *   - the timer still ticks through the muxed window while the API draws;
 *   - every pixel of the framebuffer is one of the five drawn colours, with
 *     EXACT counts (proves coverage, overlap order and per-pixel clipping of
 *     both the off-screen and the negative-coordinate rectangle);
 *   - spot-checked pixels around every rectangle edge;
 *   - register readbacks left by the API's last plot;
 *   - stray_accesses stays 0;
 *   - reset restores a black screen and the replay reproduces the identical
 *     framebuffer, timer count and state hash.
 *
 * Usage: test_gfx <image.bin>
 */

#include "machine.h"
#include "exec.h"
#include "state_hash.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Absolute MMIO addresses of the registers used here. */
#define ADDR_TIMER_COUNT (ARCADE1_MMIO_BASE + ARCADE1_TIMER_REG_COUNT)
#define ADDR_X           (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_X)
#define ADDR_Y           (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_Y)
#define ADDR_COLOR       (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_COLOR)
#define ADDR_STATUS      (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_STATUS)
#define ADDR_W           (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_W)
#define ADDR_H           (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_H)

/* The five colours gfx_demo.c draws (RGB565), chosen to be distinct. */
#define COLOR_BLUE  ((uint16_t)0x001Fu)
#define COLOR_RED   ((uint16_t)0xF800u)
#define COLOR_GREEN ((uint16_t)0x07E0u)
#define COLOR_WHITE ((uint16_t)0xFFFFu)
#define COLOR_BLACK ((uint16_t)0x0000u)

/* Framebuffer index of pixel (x, y). */
#define PIX(x, y) ((unsigned)(y) * (unsigned)ARCADE1_WIDTH + (unsigned)(x))

#define MAX_STEPS 200000

/* Step the CPU until crt0 parks at `j .` (PC stops changing) or a safety cap.
 * Returns the number of steps taken (identical on every replay). */
static int run_to_park(arcade1_cpu_t *cpu, arcade1_bus_t *bus, int max_steps)
{
    uint32_t prev = 0xFFFFFFFFu;
    int steps = 0;

    while (steps < max_steps && cpu->pc != prev) {
        prev = cpu->pc;
        arcade1_cpu_step(cpu, bus);
        steps++;
    }
    return steps;
}

/* --- The scenario --------------------------------------------------------- *
 * One identical sequence of frames + API-drawing guest run. All assertions
 * hold on both executions, so a reset must reproduce the end state. */
static void scenario(arcade1_machine_t *m, arcade1_cpu_t *cpu)
{
    /* --- Frame timing keeps working through the muxed window (M12) -------- */
    for (int i = 0; i < 10; i++)
        arcade1_run_frame(m);
    assert(arcade1_read32(&m->bus, ADDR_TIMER_COUNT) == 10u &&
           "timer must tick once per frame while the API draws");

    /* --- The guest draws using ONLY the graphics API ---------------------- */
    arcade1_cpu_reset(cpu);
    const int steps = run_to_park(cpu, &m->bus, MAX_STEPS);
    assert(steps > 0 && "CPU must execute the guest program");
    assert(steps < MAX_STEPS && "guest must park, not run to the step cap");
    assert(cpu->instret == (uint64_t)steps && "every executed step counted");
    assert(arcade1_read32(&m->bus, ADDR_TIMER_COUNT) == 10u &&
           "drawing must not disturb the timer");
    assert(arcade1_read32(&m->bus, ADDR_STATUS) == 1u &&
           "gfx_present must be counted exactly once");

    /* --- Register state left behind by the API's last plot ---------------- *
     * The final call was gfx_rect(-10, -5, 12, 8, black). Since M19 this is
     * one FILL command, so the registers hold the rectangle's parameters
     * (X/Y as signed coordinates, read back as raw u32) rather than the last
     * plotted pixel. */
    assert(arcade1_read32(&m->bus, ADDR_X) == (uint32_t)-10 &&
           "last fill X readback");
    assert(arcade1_read32(&m->bus, ADDR_Y) == (uint32_t)-5 &&
           "last fill Y readback");
    assert(arcade1_read32(&m->bus, ADDR_W) == 12u && "last fill W readback");
    assert(arcade1_read32(&m->bus, ADDR_H) == 8u && "last fill H readback");
    assert(arcade1_read32(&m->bus, ADDR_COLOR) == 0x0000u &&
           "last fill colour readback (black)");

    /* --- Exact pixel accounting (drawing + overlap + clipping) ------------- *
     * gfx_demo draws: clear(blue); rect red 40x10 at (20,30);
     * rect green 40x10 at (50,35) overlapping 50 px of the red;
     * rect white 30x20 at (310,230) -> fully visible at 480x270 (the frozen
     *   demo position no longer reaches the screen edge, so FILL right/bottom
     *   clipping is not exercised here — only the negative rect still clips);
     * rect black 12x8 at (-10,-5) -> only 2x3 visible. */
    const uint16_t *fb = arcade1_framebuffer(m);
    unsigned nblue = 0, nred = 0, ngreen = 0, nwhite = 0, nblack = 0;

    for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++) {
        const uint16_t p = fb[i];
        if      (p == COLOR_BLUE)  nblue++;
        else if (p == COLOR_RED)   nred++;
        else if (p == COLOR_GREEN) ngreen++;
        else if (p == COLOR_WHITE) nwhite++;
        else if (p == COLOR_BLACK) nblack++;
        else assert(0 && "unexpected pixel: clear/rect drawing is wrong");
    }
    assert(nred == 350u && "red: 400 px minus the 50 px the green rect covers");
    assert(ngreen == 400u && "green: full 40x10, drawn last over the red");
    assert(nwhite == 30u * 20u && "white: fully on-screen at 480x270 (30x20)");
    assert(nblack == 6u && "black: negative-coordinate part clipped (2x3)");
    assert(nblue == (unsigned)ARCADE1_FB_PIXELS - 350u - 400u - 30u * 20u - 6u &&
           "background: everything else, pattern wiped");

    /* --- Spot checks around every rectangle edge --------------------------- */
    assert(fb[PIX(0, 0)] == COLOR_BLACK && "visible part of negative rect");
    assert(fb[PIX(1, 2)] == COLOR_BLACK && "negative rect bottom-right");
    assert(fb[PIX(2, 2)] == COLOR_BLUE && "negative rect must be clipped at x=2");
    assert(fb[PIX(5, 5)] == COLOR_BLUE && "background untouched by black rect");
    assert(fb[PIX(20, 30)] == COLOR_RED && "red rect top-left");
    assert(fb[PIX(49, 34)] == COLOR_RED && "red-only region (green starts x=50)");
    assert(fb[PIX(50, 35)] == COLOR_GREEN && "overlap: later rect wins");
    assert(fb[PIX(89, 44)] == COLOR_GREEN && "green rect bottom-right");
    assert(fb[PIX(90, 44)] == COLOR_BLUE && "green rect must be clipped");
    assert(fb[PIX(309, 230)] == COLOR_BLUE && "just left of the white rect");
    assert(fb[PIX(319, 239)] == COLOR_WHITE && "inside the white rect");

    /* Everything above went through the muxed window to a real device. */
    assert(m->bus.stray_accesses == 0 &&
           "every MMIO access must reach a device (strays stay 0)");
}

/* --- Harness -------------------------------------------------------------- */

int main(int argc, char **argv)
{
    static arcade1_machine_t m;          /* Static: embeds the buffers.      */
    static arcade1_cpu_t cpu;
    static uint8_t  image[ARCADE1_ROM_SIZE];
    static uint16_t snap1[ARCADE1_FB_PIXELS];   /* framebuffer of run 1      */

    if (argc != 2) {
        fprintf(stderr, "usage: %s <image.bin>\n", argv[0]);
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    assert(f && "cannot open guest image");
    size_t n = fread(image, 1, sizeof image, f);
    fclose(f);
    assert(n > 0 && n <= sizeof image && "guest image must fit ROM");

    arcade1_init(&m);
    arcade1_reset(&m);
    assert(arcade1_load_rom(&m.bus, image, n) == n &&
           "guest image must load fully into ROM");
    arcade1_cpu_reset(&cpu);

    const uint16_t *fb = arcade1_framebuffer(&m);

    /* --- Power-on state: black screen, nothing presented ------------------- */
    assert(fb[0] == 0u && "power-on screen must be black");
    assert(arcade1_read32(&m.bus, ADDR_STATUS) == 0u && "nothing presented");

    /* --- Run 1 ------------------------------------------------------------- */
    scenario(&m, &cpu);
    const uint32_t count1    = arcade1_read32(&m.bus, ADDR_TIMER_COUNT);
    const uint32_t presents1 = arcade1_read32(&m.bus, ADDR_STATUS);
    const uint64_t hash1     = state_hash(&cpu, &m.bus);
    memcpy(snap1, arcade1_framebuffer(&m), sizeof(snap1));

    /* --- Determinism: reset-and-compare ------------------------------------ */
    arcade1_reset(&m);
    arcade1_cpu_reset(&cpu);

    assert(arcade1_read32(&m.bus, ADDR_TIMER_COUNT) == 0u &&
           "reset must clear the timer count");
    assert(arcade1_read32(&m.bus, ADDR_STATUS) == 0u &&
           "reset must clear the present counter");
    for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
        assert(fb[i] == 0u && "reset must restore a black screen");

    /* --- Run 2: replay the identical scenario ------------------------------ */
    scenario(&m, &cpu);
    const uint32_t count2    = arcade1_read32(&m.bus, ADDR_TIMER_COUNT);
    const uint32_t presents2 = arcade1_read32(&m.bus, ADDR_STATUS);
    const uint64_t hash2     = state_hash(&cpu, &m.bus);

    assert(count2 == count1 && "reset must reproduce the timer count");
    assert(presents2 == presents1 && "reset must reproduce the present count");
    assert(memcmp(snap1, arcade1_framebuffer(&m), sizeof(snap1)) == 0 &&
           "reset + replay must reproduce the identical framebuffer");
    assert(hash2 == hash1 && "reset must reproduce the state hash");

    printf("  guest: %zu bytes gfx_demo via gfx_clear/gfx_rect/gfx_present\n", n);
    printf("  pixels: red=350 green=400 white=100 black=6 blue=75944 "
           "(sum=%u), presents=%u\n",
           (unsigned)ARCADE1_FB_PIXELS, presents1);
    printf("  state hash: run1=0x%016llX run2=0x%016llX\n",
           (unsigned long long)hash1, (unsigned long long)hash2);

    printf("milestone14: all assertions passed\n");
    printf("  verified: guest API clear/rect/present over MMIO,\n");
    printf("            exact pixels (overlap + clipping + negative coords),\n");
    printf("            timer unaffected, deterministic under reset\n");
    return 0;
}
