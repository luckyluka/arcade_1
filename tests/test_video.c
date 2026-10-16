/* ARCADE-1 — Milestone 13 test harness (video device).
 *
 * Proves docs.md milestone 13: a guest program communicates with the video
 * device through the machine's MMIO, and the chain
 *
 *     RV32 program -> MMIO -> video device -> software renderer -> Libretro
 *
 * is real: the guest's CLEAR / SET_PIXEL / PRESENT writes land in the exact
 * RGB565 framebuffer arcade1_framebuffer() exposes to the frontend.
 *
 * Links machine+video+timer+memory+exec+cpu (no libretro, no .so). The scenario
 * below runs TWICE with a reset in between (reset-and-compare) and asserts:
 *   - the timer still ticks through the muxed window (0x000..) while the video
 *     device is attached (0x100..);
 *   - the guest (video_demo.c) clears the screen, draws, presents, and parks;
 *   - framebuffer pixels match the drawn colours, including a deliberately
 *     out-of-range SET_PIXEL that must be clipped;
 *   - register readbacks (X, Y kept raw, COLOR masked to 16 bits, STATUS =
 *     number of PRESENTs);
 *   - stray_accesses stays 0 (every access reached a device);
 *   - reset restores a black screen and replays to an identical framebuffer,
 *     timer count and state hash.
 *
 * Usage: test_video <image.bin>
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

/* Colours the guest uses (RGB565). White appears only in the clipped plot. */
#define COLOR_RED   ((uint16_t)0xF800u)
#define COLOR_GREEN ((uint16_t)0x07E0u)
#define COLOR_BLUE  ((uint16_t)0x001Fu)
#define COLOR_WHITE ((uint16_t)0xFFFFu)

/* Framebuffer index of pixel (x, y). */
#define PIX(x, y) ((unsigned)(y) * (unsigned)ARCADE1_WIDTH + (unsigned)(x))

#define MAX_STEPS 200000

/* Step the CPU until crt0 parks at `j .` (PC stops changing) or a safety cap.
 * Returns the number of steps taken (identical on every replay: same ROM,
 * same reset state, same device responses). */
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
 * One identical sequence of frames + guest run. Every assertion holds on both
 * executions, so the end state must be reproducible after a reset. */
static void scenario(arcade1_machine_t *m, arcade1_cpu_t *cpu)
{
    /* --- Frame timing keeps working through the muxed window (M12) -------- */
    for (int i = 0; i < 10; i++)
        arcade1_run_frame(m);
    assert(arcade1_read32(&m->bus, ADDR_TIMER_COUNT) == 10u &&
           "timer must tick once per frame with the video device attached");
    assert(m->frame_counter == 10u && "exactly 10 frames driven");

    /* --- The guest drives the video device over MMIO ---------------------- */
    arcade1_cpu_reset(cpu);
    const int steps = run_to_park(cpu, &m->bus, MAX_STEPS);
    assert(steps > 0 && "CPU must execute the guest program");
    assert(steps < MAX_STEPS && "guest must park, not run to the step cap");
    assert(cpu->instret == (uint64_t)steps && "every executed step counted");

    /* The guest only touched the video half of the window (0x100..). */
    assert(arcade1_read32(&m->bus, ADDR_TIMER_COUNT) == 10u &&
           "video commands must not disturb the timer");

    /* --- Device register readbacks ---------------------------------------- */
    assert(arcade1_read32(&m->bus, ADDR_X) == 500u && "X kept as written");
    assert(arcade1_read32(&m->bus, ADDR_Y) == 600u && "Y kept as written");
    assert(arcade1_read32(&m->bus, ADDR_COLOR) == 0xFFFFu &&
           "COLOR must be masked to 16 bits");
    assert(arcade1_read32(&m->bus, ADDR_STATUS) == 1u &&
           "exactly one PRESENT must be counted");

    /* --- The framebuffer Libretro scans out -------------------------------- */
    const uint16_t *fb = arcade1_framebuffer(m);

    assert(fb[PIX(0, 0)] == COLOR_GREEN && fb[PIX(3, 3)] == COLOR_GREEN &&
           "green diagonal must be plotted at (0,0)..(3,3)");
    assert(fb[PIX(10, 10)] == COLOR_RED &&
           "CLEAR must wipe the demo pattern to red");
    assert(fb[PIX(319, 0)] == COLOR_RED &&
           "CLEAR must wipe the top-right demo pattern too");
    assert(fb[PIX(319, 239)] == COLOR_BLUE && "far-corner pixel must be blue");

    /* White was used ONLY for the out-of-range plot (500,600): if the device
     * clipped it as documented, no pixel anywhere may be white. */
    for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
        assert(fb[i] != COLOR_WHITE && "out-of-range SET_PIXEL must be clipped");

    /* Every access above went through the muxed window to a real device. */
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

    /* --- Power-on video state: black screen, nothing presented ------------ */
    assert(fb[0] == 0u && "power-on screen must be black");
    assert(arcade1_read32(&m.bus, ADDR_STATUS) == 0u && "nothing presented");
    assert(arcade1_read32(&m.bus, ADDR_X) == 0u && "X starts at 0");

    /* --- Run 1 ------------------------------------------------------------- */
    scenario(&m, &cpu);
    const uint32_t count1    = arcade1_read32(&m.bus, ADDR_TIMER_COUNT);
    const uint32_t presents1 = arcade1_read32(&m.bus, ADDR_STATUS);
    const uint64_t hash1     = state_hash(&cpu, &m.bus);
    memcpy(snap1, arcade1_framebuffer(&m), sizeof(snap1));

    /* --- Determinism: reset-and-compare ----------------------------------- */
    /* arcade1_reset() restores the power-on video state (black screen, cleared
     * registers), restarts the timer and clears RAM, while keeping the loaded
     * ROM and the window's MMIO attachment. */
    arcade1_reset(&m);
    arcade1_cpu_reset(&cpu);

    assert(arcade1_read32(&m.bus, ADDR_TIMER_COUNT) == 0u &&
           "reset must clear the timer count");
    assert(arcade1_read32(&m.bus, ADDR_STATUS) == 0u &&
           "reset must clear the present counter");
    for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
        assert(fb[i] == 0u && "reset must restore a black screen");

    /* --- Run 2: replay the identical scenario ----------------------------- */
    scenario(&m, &cpu);
    const uint32_t count2    = arcade1_read32(&m.bus, ADDR_TIMER_COUNT);
    const uint32_t presents2 = arcade1_read32(&m.bus, ADDR_STATUS);
    const uint64_t hash2     = state_hash(&cpu, &m.bus);

    assert(count2 == count1 && "reset must reproduce the timer count");
    assert(presents2 == presents1 && "reset must reproduce the present count");
    assert(memcmp(snap1, arcade1_framebuffer(&m), sizeof(snap1)) == 0 &&
           "reset + replay must reproduce the identical framebuffer");
    assert(hash2 == hash1 && "reset must reproduce the state hash");

    printf("  guest: %zu bytes video_demo, CLEAR + 6x SET_PIXEL + PRESENT\n", n);
    printf("  video: presents=%u, timer count after 10 frames=%u\n",
           presents1, count1);
    printf("  state hash: run1=0x%016llX run2=0x%016llX\n",
           (unsigned long long)hash1, (unsigned long long)hash2);

    printf("milestone13: all assertions passed\n");
    printf("  verified: guest -> MMIO -> video device -> framebuffer,\n");
    printf("            timer+video on one muxed window, pixel clipping,\n");
    printf("            reset (black screen), deterministic replay\n");
    return 0;
}
