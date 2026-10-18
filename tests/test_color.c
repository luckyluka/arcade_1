/* ARCADE-1 — M27 step 4 test harness (color_demo: paletted 4-bit graphics).
 *
 * Boots color_demo.c on the machine's own CPU and proves the whole M27 colour
 * pipeline end-to-end:
 *
 *   - property checks (frame 0, camera 0,0) that the expected colours really
 *     appear: the screen-space 1-bit HUD at its exact position, the power-on
 *     default (bank 0) blue, the custom bank 1 orange, and the bank 2 sprite
 *     colour — i.e. the 1-bit and both 4-bit palette-bank paths all rendered;
 *   - per-frame determinism: two identical runs (reset + replay) yield the
 *     identical per-frame state-hash sequence and a byte-identical final
 *     framebuffer (each frame's hash is printed for the golden regression);
 *   - a pinned final state hash and framebuffer hash;
 *   - a PPM dump of the final frame to $TMPDIR/color_demo.ppm for the owner.
 */

#include "machine.h"
#include "rom.h"
#include "state_hash.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Probes and constants shared with programs/color_demo.c (kept in sync by
 * hand, like the other guest tests). */
#define COLOR_HUD  0x5642u   /* screen-space HUD rect (unique teal)        */
#define COLOR_BANK0 0x001Fu  /* power-on default blue (bank 0 tile interior)*/
#define COLOR_BANK1 0xFD20u  /* custom bank 1 orange (bank 1 tile interior)*/
#define COLOR_SPR  0x8010u   /* bank 2 sprite disc colour (SPRITE4)        */
#define HUD_X 8
#define HUD_Y 8
#define HUD_W 60
#define HUD_H 12

#define NFRAMES 120

/* Pinned fingerprints of the final (120th) frame. Filled in after the first
 * run; the guards make the bootstrap run pass before they are pinned. */
/* PINNED_FB is machine behaviour and never moves: it holds for the committed
 * image (tests/roms) and for a fresh rebuild alike. PINNED_STATE is disabled
 * on purpose: state_hash folds in the guest's instruction counter, so it
 * tracks guest codegen, and `make check` drives this test with two images
 * (the committed ROM and the freshly built one) that no longer agree on it
 * since programs/gfx.h gained the M29 draw barrier. Per-frame regression
 * coverage for the committed image is tests/golden/color.txt. */
#define PINNED_STATE 0ull
#define PINNED_FB    0xEE39DB3714BF643Aull

#define PIX(x, y) ((unsigned)(y) * (unsigned)ARCADE1_WIDTH + (unsigned)(x))

static arcade1_machine_t m;
static uint8_t  img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
static size_t   img_size;
static uint64_t hist1[NFRAMES];
static uint16_t fb1[ARCADE1_FB_PIXELS];

/* Number of framebuffer pixels equal to colour c. */
static unsigned count_color(const uint16_t *fb, uint16_t c)
{
    unsigned n = 0;
    for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
        if (fb[i] == c)
            n++;
    return n;
}

/* Frame 0 (camera 0,0): the expected colours must really be on screen. */
static void check_frame0(void)
{
    const uint16_t *fb = arcade1_framebuffer(&m);

    /* The 1-bit HUD sits at its exact position and covers its whole rect (its
     * colour is unique, so the count is exact). */
    assert(fb[PIX(HUD_X + 1, HUD_Y + 1)] == COLOR_HUD &&
           "HUD pixel must be the HUD colour");
    assert(count_color(fb, COLOR_HUD) == HUD_W * HUD_H &&
           "HUD rect must be fully covered exactly once");

    /* Both palette banks and the sprite bank rendered their colours. */
    assert(count_color(fb, COLOR_BANK0) > 0 &&
           "power-on default (bank 0) blue must appear");
    assert(count_color(fb, COLOR_BANK1) > 0 &&
           "custom bank 1 orange must appear");
    assert(count_color(fb, COLOR_SPR) > 0 &&
           "bank 2 SPRITE4 disc colour must appear");
}

static void boot_machine(void)
{
    const void *rom = img;
    size_t      rom_size = img_size;

    if (arcade1_image_recognized(img, img_size)) {
        arcade1_image_t parsed;
        assert(arcade1_image_parse(img, img_size, &parsed) == ARCADE1_IMAGE_OK);
        rom      = parsed.payload;
        rom_size = parsed.payload_size;
    }

    arcade1_init(&m);
    arcade1_reset(&m);
    assert(arcade1_load_rom(&m.bus, rom, (uint32_t)rom_size) == rom_size);
    arcade1_set_firmware(&m, true);
}

/* Run NFRAMES frames of color_demo, printing one hash line per frame. In
 * verify mode the sequence must match the recorded pass-1 one. */
static void run_pass(bool verify)
{
    boot_machine();
    for (unsigned f = 0; f < NFRAMES; f++) {
        arcade1_run_frame(&m);
        if (f == 0)
            check_frame0();
        const uint64_t h = state_hash(&m.cpu, &m.bus);
        printf("frame %4u hash=0x%016llX fb=0x%016llX\n", f,
               (unsigned long long)h, (unsigned long long)fb_hash(&m));
        if (verify) {
            assert(h == hist1[f] && "per-frame state hash diverged on replay");
        } else {
            hist1[f] = h;
        }
    }
}

/* Dump the final framebuffer as a binary PPM (P6) to $TMPDIR/color_demo.ppm. */
static void dump_ppm(void)
{
    const char *dir = getenv("TMPDIR");
    char path[512];
    FILE *f;
    const uint16_t *fb = arcade1_framebuffer(&m);

    if (dir == NULL || dir[0] == '\0')
        dir = "/tmp";
    snprintf(path, sizeof path, "%s/color_demo.ppm", dir);
    f = fopen(path, "wb");
    assert(f && "cannot open PPM output");
    fprintf(f, "P6\n%d %d\n255\n", ARCADE1_WIDTH, ARCADE1_HEIGHT);
    for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++) {
        const uint16_t p = fb[i];
        const unsigned r = ((p >> 11) & 31u) * 255u / 31u;
        const unsigned g = ((p >> 5) & 63u) * 255u / 63u;
        const unsigned b = (p & 31u) * 255u / 31u;
        fputc((int)r, f);
        fputc((int)g, f);
        fputc((int)b, f);
    }
    fclose(f);
    printf("  PPM frame: %s\n", path);
}

int main(int argc, char **argv)
{
    FILE *f;
    long  n;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <image.arc1|bin>\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    assert(f && "cannot open guest image");
    n = (long)fread(img, 1, sizeof img, f);
    assert(n > 0 && (size_t)n <= sizeof img && "ROM image must fit");
    fclose(f);
    img_size = (size_t)n;

    /* Pass 1: record the per-frame hash sequence. */
    run_pass(false);
    memcpy(fb1, m.framebuffer, sizeof fb1);
    const uint64_t sh1 = state_hash(&m.cpu, &m.bus);

    /* Pass 2: reset + replay must reproduce hashes and the exact screen. */
    run_pass(true);
    assert(memcmp(fb1, m.framebuffer, sizeof fb1) == 0 &&
           "final framebuffer diverged after reset");
    const uint64_t sh2 = state_hash(&m.cpu, &m.bus);
    assert(sh2 == sh1 && "final state hash diverged after reset");

    /* Pinned fingerprints (guarded so the bootstrap run passes). */
    if (PINNED_STATE != 0ull)
        assert(sh2 == PINNED_STATE && "final state hash != pinned value");
    if (PINNED_FB != 0ull)
        assert(fb_hash(&m) == PINNED_FB && "final fb hash != pinned value");

    /* No stray accesses over the whole run. */
    assert(m.bus.stray_accesses == 0 && "guest must not stray outside memory");

    printf("milestone27-step4: color_demo ok (%u frames, two identical runs)\n",
           NFRAMES);
    printf("  final state hash: 0x%016llX\n", (unsigned long long)sh2);
    printf("  final fb hash:    0x%016llX\n", (unsigned long long)fb_hash(&m));
    dump_ppm();
    return 0;
}
