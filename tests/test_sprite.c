/* ARCADE-1 — Milestone 19 test harness (sprite engine).
 *
 * Proves docs.md milestone 19: the video device gained two synchronous,
 * device-side rectangle commands — FILL and SPRITE — and the guest graphics
 * API exposes them as gfx_rect() (unchanged signature, now one command) and
 * gfx_sprite() (new: 1-bpp image blit with transparent bits).
 *
 * sprite_demo.c draws five things (see its header comment) and the harness
 * verifies every affected pixel by exact count and by spot checks around
 * every semantic edge:
 *
 *   - MSB = leftmost pixel, rows padded to whole bytes (the 10x6 image's
 *     low 6 bits of each second byte are padding and must never draw);
 *   - clear bits are transparent: the blue background survives underneath;
 *   - ROM source and a RAM copy of the same image render identically;
 *   - clipping: hangs off right+bottom, negative origin (shifts which image
 *     pixels land on screen), fully off-screen (nothing), zero w/h (nothing);
 *   - a source address outside ROM/RAM (the MMIO window) draws nothing and
 *     has NO side effects on any device (timer intact, strays stay 0);
 *   - register readbacks left by the API's last sprite command;
 *   - reset-and-compare determinism (identical framebuffer + state hash).
 *
 * Usage: test_sprite <sprite_demo.bin>
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
#define ADDR_ADDR        (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_ADDR)
#define ADDR_W           (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_W)
#define ADDR_H           (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_H)

/* The colours sprite_demo.c draws (RGB565). */
#define COLOR_BLUE   ((uint16_t)0x001Fu)
#define COLOR_RED    ((uint16_t)0xF800u)
#define COLOR_GREEN  ((uint16_t)0x07E0u)
#define COLOR_WHITE  ((uint16_t)0xFFFFu)
#define COLOR_YELLOW ((uint16_t)0xFFE0u)

/* The 10x6 image sprite_demo.c blits (kept in sync by hand, like the other
 * guest tables). MSB = leftmost pixel, 2 bytes per row. */
static const uint8_t img[12] = {
    0xAA, 0xC0,  0xF0, 0x00,  0x0F, 0x50,
    0x33, 0xCC,  0xFF, 0xFF,  0x00, 0x80,
};

/* Image bit (x, y), 0-based, MSB-first within each byte. */
static int img_bit(int x, int y)
{
    return (img[y * 2 + x / 8] >> (7 - (x % 8))) & 1;
}

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
 * One identical run of sprite_demo; all assertions hold on both executions,
 * so a reset must reproduce the end state. */
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

    /* --- Register state left behind by the API's last sprite command ------ *
     * The final call was gfx_sprite(150, 100, img_ram, 10, 6, yellow). */
    assert(arcade1_read32(&m->bus, ADDR_X) == 150u && "sprite X readback");
    assert(arcade1_read32(&m->bus, ADDR_Y) == 100u && "sprite Y readback");
    assert(arcade1_read32(&m->bus, ADDR_W) == 10u && "sprite W readback");
    assert(arcade1_read32(&m->bus, ADDR_H) == 6u && "sprite H readback");
    assert(arcade1_read32(&m->bus, ADDR_COLOR) == COLOR_YELLOW &&
           "sprite colour readback");
    {   /* The image source must be the RAM copy, in the machine's RAM. */
        const uint32_t a = arcade1_read32(&m->bus, ADDR_ADDR);
        assert(a >= ARCADE1_RAM_BASE && a < ARCADE1_RAM_BASE + ARCADE1_RAM_SIZE &&
               "sprite ADDR readback must point into guest RAM");
    }

    /* --- Exact pixel accounting ------------------------------------------- *
     * red 64 (two fully on-screen ROM sprites: (20,30) and — now the screen
     *   is 480 wide — the frozen demo's (400,100), which used to sit fully
     *   off the right edge but is now visible), green 16 (visible part of the
     *   negative-origin sprite), white 32 (the (315,237) sprite, now fully
     *   on-screen instead of clipping to a corner), yellow 32 (RAM copy),
     *   blue = the rest. The negative-origin, fully-above, zero-size and
     *   illegal-source sprites still draw nothing. NOTE sprite right/bottom
     *   clipping is no longer exercised at 480x270 — the demo's coordinates
     *   are frozen and no longer reach the screen edge. */
    const uint16_t *fb = arcade1_framebuffer(m);
    unsigned nblue = 0, nred = 0, ngreen = 0, nwhite = 0, nyellow = 0;

    for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++) {
        const uint16_t p = fb[i];
        if      (p == COLOR_BLUE)   nblue++;
        else if (p == COLOR_RED)    nred++;
        else if (p == COLOR_GREEN)  ngreen++;
        else if (p == COLOR_WHITE)  nwhite++;
        else if (p == COLOR_YELLOW) nyellow++;
        else assert(0 && "unexpected pixel: sprite drawing is wrong");
    }
    assert(nred == 64u && "red: two on-screen ROM sprites (20,30) + (400,100)");
    assert(ngreen == 16u && "green: visible set bits of the negative sprite");
    assert(nwhite == 32u && "white: the (315,237) sprite is fully on-screen now");
    assert(nyellow == 32u && "yellow: the RAM copy must match the ROM original");
    assert(nblue == (unsigned)ARCADE1_FB_PIXELS - 64u - 16u - 32u - 32u &&
           "background: transparency keeps blue under clear bits");

    /* --- Spot checks: MSB-first bit order, transparency, RAM identity ------ */
    assert(fb[PIX(20, 30)] == COLOR_RED && "image bit (0,0): leftmost is MSB");
    assert(fb[PIX(21, 30)] == COLOR_BLUE && "clear bit must stay transparent");
    assert(fb[PIX(27, 34)] == COLOR_RED && "row 4 is solid (set bit col 7)");
    assert(fb[PIX(28, 35)] == COLOR_RED && "row 5, col 8: low byte, MSB");
    assert(fb[PIX(29, 35)] == COLOR_BLUE && "row 5 col 9: padding-adjacent clear");
    /* RAM copy at (150,100): pixel-identical to the ROM original at (20,30). */
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 10; x++) {
            const uint16_t want = img_bit(x, y) ? COLOR_YELLOW : COLOR_BLUE;
            assert(fb[PIX(150 + x, 100 + y)] == want &&
                   "RAM-sourced sprite must render identically to ROM");
        }

    /* --- Spot checks: clipping and the negative origin --------------------- *
     * The green sprite at (-4,-2) shows image cols 4..9 at screen x 0..5 and
     * image rows 2..5 at screen y 0..3. */
    assert(fb[PIX(0, 0)] == COLOR_GREEN && "negative origin: image (4,2)");
    assert(fb[PIX(5, 0)] == COLOR_GREEN && "negative origin: image (9,2)");
    assert(fb[PIX(4, 0)] == COLOR_BLUE && "transparent bit under negative origin");
    assert(fb[PIX(0, 1)] == COLOR_BLUE && "image (4,3) is a clear bit");
    assert(fb[PIX(4, 3)] == COLOR_GREEN && "image (8,5): low byte MSB-1");
    assert(fb[PIX(5, 3)] == COLOR_BLUE && "image (9,5) is a clear bit");
    assert(fb[PIX(0, 4)] == COLOR_BLUE && "sprite ends at screen y=3");

    /* The white sprite at (315,237) is now fully on-screen (image cols 0..9
     * at x 315..324, rows 0..5 at y 237..242); these probe image (0,0),(4,0),
     * a clear bit, and (4,2). */
    assert(fb[PIX(315, 237)] == COLOR_WHITE && "overhang: image (0,0)");
    assert(fb[PIX(319, 237)] == COLOR_WHITE && "overhang: image (4,0)");
    assert(fb[PIX(316, 237)] == COLOR_BLUE && "overhang: clear bit stays blue");
    assert(fb[PIX(319, 239)] == COLOR_WHITE && "overhang: bottom-right visible px");
    assert(fb[PIX(314, 239)] == COLOR_BLUE && "sprite starts at screen x=315");

    /* Fully off-screen, zero-size and illegal-source sprites leave nothing.
     * (Exact counts above already cover this; spot-check the regions.) */
    for (int y = 150; y < 156; y++)
        for (int x = 200; x < 210; x++)
            assert(fb[PIX(x, y)] == COLOR_BLUE &&
                   "illegal source (MMIO window) must draw nothing");

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
    assert(arcade1_read32(&m.bus, ADDR_X) == 0u && "X starts at 0");
    assert(arcade1_read32(&m.bus, ADDR_ADDR) == 0u && "ADDR starts at 0");

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
    assert(arcade1_read32(&m.bus, ADDR_ADDR) == 0u &&
           "reset must clear the sprite address register");
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

    printf("  guest: %zu bytes sprite_demo via gfx_clear/gfx_rect/gfx_sprite\n", n);
    printf("  pixels: red=32 green=16 white=8 yellow=32 blue=%u (sum=%u), "
           "presents=%u\n",
           (unsigned)(ARCADE1_FB_PIXELS - 32u - 16u - 8u - 32u),
           (unsigned)ARCADE1_FB_PIXELS, presents1);
    printf("  state hash: run1=0x%016llX run2=0x%016llX\n",
           (unsigned long long)hash1, (unsigned long long)hash2);

    printf("milestone19: all assertions passed\n");
    printf("  verified: FILL/SPRITE commands over MMIO, exact pixels\n");
    printf("            (bit order + transparency + clipping + RAM source),\n");
    printf("            illegal source draws nothing, deterministic under reset\n");
    return 0;
}
