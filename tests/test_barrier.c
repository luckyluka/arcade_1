/* M29 regression test: the SDK's draw helpers must be compiler barriers.
 *
 * Why this exists: the video device reads guest memory through the integer
 * addresses the guest writes into its registers (image at ADDR, cells at
 * MAP_ADDR, tiles at TILE_ADDR). That read is invisible to the compiler, so
 * GCC is free to delete or sink the stores that fill those tables. When it
 * does, the device reads zeroed RAM and draws nothing — no error, no
 * warning, just a blank screen (the failure the M29 fresh-agent build hit).
 *
 * programs/gfx.h now routes every command through gfx_cmd(), which brackets
 * the CMD write with a compile-time memory barrier. This test pins that
 * down: each probe guest fills a table in RAM at run time and draws it, and
 * must paint exactly the pixels its geometry implies. Without the barrier
 * several of them paint 0.
 *
 * Usage: test_barrier <guest-build-dir>
 */
#include "machine.h"
#include "rom.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define FRAMES 3       /* boot frame, then the drawn ones */

static arcade1_machine_t m;
static uint8_t           image[ARCADE1_ROM_SIZE];

/* Pixels a probe must paint; the geometry is spelled out so a mismatch
 * names the bug rather than a magic number. */
struct probe {
    const char *bin;
    unsigned    expect;
};

static const struct probe probes[] = {
    { "probe_tm4_4x3.bin",   4 * 3 * 16 * 8 },  /* TILEMAP4 cells of 16x8   */
    { "probe_tm4_5x1.bin",   5 * 1 * 16 * 8 },
    { "probe_tm4_1x8.bin",   1 * 8 * 16 * 8 },
    { "probe_tm4_6x3.bin",   6 * 3 * 16 * 8 },
    { "probe_tm4_10x3.bin", 10 * 3 * 16 * 8 },
    { "probe_sprite4.bin",   4 * 4            },  /* one 4x4 SPRITE4        */
    { "probe_tilemap1.bin",  2 * 2 * 8 * 8    },  /* 2x2 8x8 1-bit cells    */
};

static unsigned count_pixels(void)
{
    const uint16_t *fb = arcade1_framebuffer(&m);
    unsigned        n  = 0;

    for (int i = 0; i < ARCADE1_FB_PIXELS; i++)
        if (fb[i] != 0)
            n++;
    return n;
}

/* Boot one probe image, run FRAMES frames, return the painted pixel count. */
static unsigned run_probe(const char *dir, const char *bin)
{
    char  path[512];
    FILE *f;
    size_t n;

    snprintf(path, sizeof path, "%s/%s", dir, bin);
    f = fopen(path, "rb");
    assert(f && "cannot open probe image");
    n = fread(image, 1, sizeof image, f);
    fclose(f);
    assert(n > 0 && n <= sizeof image && "probe image must fit ROM");

    arcade1_init(&m);
    arcade1_reset(&m);
    assert(arcade1_load_rom(&m.bus, image, (uint32_t)n) == n &&
           "probe image must load fully into ROM");
    arcade1_set_firmware(&m, true);

    for (unsigned fr = 0; fr < FRAMES; fr++)
        arcade1_run_frame(&m);

    return count_pixels();
}

int main(int argc, char **argv)
{
    int failed = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <guest-build-dir>\n", argv[0]);
        return 2;
    }

    for (size_t i = 0; i < sizeof probes / sizeof probes[0]; i++) {
        unsigned got = run_probe(argv[1], probes[i].bin);

        printf("  %-18s %6u px (want %6u)%s\n", probes[i].bin, got,
               probes[i].expect, got == probes[i].expect ? "" : "  <-- WRONG");
        if (got != probes[i].expect) {
            fprintf(stderr,
                    "FAIL %s: painted %u pixels, want %u — a RAM table the "
                    "device reads was optimized away; see the M29 draw "
                    "barrier in programs/gfx.h\n",
                    probes[i].bin, got, probes[i].expect);
            failed = 1;
        }
    }

    if (failed)
        return 1;
    printf("test-barrier: all %zu probes painted their expected pixels\n",
           sizeof probes / sizeof probes[0]);
    return 0;
}
