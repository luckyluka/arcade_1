/* ARCADE-1 — Milestone 21 test harness (camera scrolling).
 *
 * Proves docs.md milestone 21: the video device gained a 2D camera (CAM_X /
 * CAM_Y). The two WORLD-space commands — SPRITE and TILEMAP — are drawn by the
 * device at (x - cam_x, y - cam_y); the primitives (CLEAR / SET_PIXEL / FILL)
 * stay screen-space and are never scrolled (they are the HUD). The guest API
 * exposes it as gfx_camera().
 *
 * scroll_demo.c is a firmware game (infinite loop, one iteration per host
 * frame). It holds a 32x22-cell map of 16px tiles — a 512x352 world, larger
 * than the 320x240 screen — and sweeps the camera over both axes, plus a
 * world-space sprite that pans and a screen-space HUD rect that does not.
 *
 * Every frame the harness re-renders the WHOLE framebuffer from an independent
 * model (written from the documented semantics, not by calling the device) and
 * memcmps it against the real one. The model:
 *
 *   - reads the demo's 704 map cells straight out of guest RAM (the symbol
 *     `cells`, so a wrong MAP_ADDR register cannot hide — the model uses the
 *     real array the guest built);
 *   - uses its OWN hand-mirrored copies of the three tiles and the sprite, so
 *     a wrong TILE_ADDR or sprite address cannot hide either;
 *   - computes the expected camera from the frame index with the demo's own
 *     bounce() formula and asserts the CAM_X/CAM_Y register readback matches
 *     (a wrong camera register is caught), then renders at that camera;
 *   - visits EVERY cell then clips (no windowing), while the device only
 *     visits the visible cells — proving the device's tilemap WINDOWing is
 *     pixel-identical to drawing the whole map.
 *
 * Plus invariants that are camera-dependent (sprite pans, HUD stays), register
 * readbacks, timer/present/stray accounting, and reset-and-compare determinism
 * (identical per-frame hash sequence + byte-identical final framebuffer).
 *
 * Usage: test_scroll <scroll_demo.arc1|bin> <cells_hex>
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

/* Video/timer register offsets. Passed to reg(), which adds ARCADE1_MMIO_BASE
 * to reach the muxed window. */
#define A_TIMER ARCADE1_TIMER_REG_COUNT
#define A_X     ARCADE1_VIDEO_REG_X
#define A_Y     ARCADE1_VIDEO_REG_Y
#define A_W     ARCADE1_VIDEO_REG_W
#define A_H     ARCADE1_VIDEO_REG_H
#define A_MAP   ARCADE1_VIDEO_REG_MAP_ADDR
#define A_TILE  ARCADE1_VIDEO_REG_TILE_ADDR
#define A_COLS  ARCADE1_VIDEO_REG_COLS
#define A_ROWS  ARCADE1_VIDEO_REG_ROWS
#define A_CX    ARCADE1_VIDEO_REG_CAM_X
#define A_CY    ARCADE1_VIDEO_REG_CAM_Y

/* --- Constants mirrored from scroll_demo.c (kept in sync by hand) --------- */
#define TILE_W 16
#define TILE_H 16
#define MAP_COLS 32
#define MAP_ROWS 22
#define WORLD_W (MAP_COLS * TILE_W)   /* 512 */
#define WORLD_H (MAP_ROWS * TILE_H)   /* 352 */
#define MAXCX   (WORLD_W - 320)       /* 192 */
#define MAXCY   (WORLD_H - 240)       /* 112 */

#define COLOR_BG  0x0000u
#define HUD_X 8
#define HUD_Y 8
#define HUD_W 48
#define HUD_H 10
#define COLOR_HUD 0xFD20u
#define SPRITE_X 200
#define SPRITE_Y 150
#define COLOR_SPR 0xFFFFu

#define NFRAMES 120

/* The three 16x16 1-bpp tiles: 2 bytes/row, MSB = leftmost. */
static const uint8_t tiles[3][32] = {
    {   /* 1: solid */
        0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
        0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
        0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
        0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
    },
    {   /* 2: box outline */
        0xFF,0xFF, 0x80,0x01, 0x80,0x01, 0x80,0x01,
        0x80,0x01, 0x80,0x01, 0x80,0x01, 0x80,0x01,
        0x80,0x01, 0x80,0x01, 0x80,0x01, 0x80,0x01,
        0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
    },
    {   /* 3: top-left -> bottom-right diagonal */
        0x80,0x00, 0x40,0x00, 0x20,0x00, 0x10,0x00,
        0x08,0x00, 0x04,0x00, 0x02,0x00, 0x01,0x00,
        0x00,0x80, 0x00,0x40, 0x00,0x20, 0x00,0x10,
        0x00,0x08, 0x00,0x04, 0x00,0x02, 0x00,0x01,
    },
};

/* The 16x16 world-space sprite: a plus sign. */
static const uint8_t sprite_img[32] = {
    0x01,0x80, 0x01,0x80, 0x01,0x80, 0x01,0x80,
    0x01,0x80, 0x01,0x80, 0x01,0x80, 0xFF,0xFF,
    0xFF,0xFF, 0x01,0x80, 0x01,0x80, 0x01,0x80,
    0x01,0x80, 0x01,0x80, 0x01,0x80, 0x01,0x80,
};

#define PIX(x, y) ((size_t)(y) * (size_t)ARCADE1_WIDTH + (size_t)(x))

/* --- Guest state ----------------------------------------------------------- */
static arcade1_machine_t m;
static uint8_t  img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
static size_t   img_size;
static int32_t  sym_cells;               /* RAM address of the map (nm)      */
static uint16_t fb1[ARCADE1_FB_PIXELS];  /* run-1 final framebuffer           */

/* Per-frame hashing + CPU load. */
static uint64_t hist1[512];
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

/* --- Independent model ---------------------------------------------------- */

static void model_px(uint16_t *fb, int x, int y, uint16_t c)
{
    if (x >= 0 && x < ARCADE1_WIDTH && y >= 0 && y < ARCADE1_HEIGHT)
        fb[PIX(x, y)] = c;
}

/* Blit a 16x16 1-bpp image (2 bytes/row, MSB = leftmost) at (ox, oy). */
static void model_img(uint16_t *fb, const uint8_t *img, int ox, int oy,
                      uint16_t color)
{
    int row, col;
    for (row = 0; row < TILE_H; row++)
        for (col = 0; col < TILE_W; col++)
            if ((img[row * 2 + col / 8] >> (7 - (col & 7))) & 1)
                model_px(fb, ox + col, oy + row, color);
}

/* Draw the whole map (visiting EVERY cell, no windowing) at (ox, oy). */
static void model_map(uint16_t *fb, const uint32_t *cells, int ox, int oy)
{
    int col, row;
    for (row = 0; row < MAP_ROWS; row++)
        for (col = 0; col < MAP_COLS; col++) {
            const uint32_t cell  = cells[row * MAP_COLS + col];
            const uint32_t index = cell & 0xFFFFu;
            if (index == 0)
                continue;                       /* empty: background shows */
            model_img(fb, tiles[index - 1u],
                      ox + col * TILE_W, oy + row * TILE_H,
                      (uint16_t)(cell >> 16));
        }
}

/* Render the demo's whole draw sequence for camera (cx, cy). Order matters. */
static void model_render(uint16_t *fb, const uint32_t *cells, int cx, int cy)
{
    unsigned i;
    int x, y;

    for (i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++)
        fb[i] = COLOR_BG;                                   /* gfx_clear    */

    model_map(fb, cells, -cx, -cy);                         /* world (0,0)  */
    model_img(fb, sprite_img, SPRITE_X - cx, SPRITE_Y - cy,
              COLOR_SPR);                                   /* world sprite */

    for (y = HUD_Y; y < HUD_Y + HUD_H; y++)                 /* HUD (screen) */
        for (x = HUD_X; x < HUD_X + HUD_W; x++)
            model_px(fb, x, y, COLOR_HUD);
}

/* The demo's bounce(), mirrored. */
static int bounce(int v, int maxv)
{
    int period = maxv * 2;
    if (period <= 0)
        return 0;
    v %= period;
    if (v < 0)
        v += period;
    return (v <= maxv) ? v : (period - v);
}

/* --- Harness --------------------------------------------------------------- */

static void boot_machine(void)
{
    const void *rom = img;
    size_t rom_size = img_size;

    if (arcade1_image_recognized(img, img_size)) {
        arcade1_image_t parsed;
        assert(arcade1_image_parse(img, img_size, &parsed) ==
               ARCADE1_IMAGE_OK);
        rom      = parsed.payload;
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

/* One run of scroll_demo: NFRAMES frames, each checked against the model. */
static void scenario(bool verify)
{
    static uint32_t cells[MAP_ROWS * MAP_COLS];
    static uint16_t model[ARCADE1_FB_PIXELS];
    const uint16_t *fb = arcade1_framebuffer(&m);

    verify_hashes = verify;
    boot_machine();

    for (unsigned iter = 0; iter < NFRAMES; iter++) {
        const uint64_t before = m.cpu.instret;
        uint64_t h;

        arcade1_run_frame(&m);

        {   /* CPU load (docs.md "Games": instructions per frame). */
            const uint64_t delta = m.cpu.instret - before;
            total_inst += delta;
            if (delta > max_inst)
                max_inst = delta;
            frames_this_pass++;
            video_budget_sample(&vbudget, &m, delta);   /* task C */
        }

        /* --- The camera this frame, computed independently from the index. */
        const int cx = bounce((int)iter * 5, MAXCX);
        const int cy = bounce((int)iter * 3, MAXCY);
        assert((int)reg(A_CX) == cx && "CAM_X register readback diverged");
        assert((int)reg(A_CY) == cy && "CAM_Y register readback diverged");

        /* --- Register readbacks left by the frame's last draw ------------- *
         * The HUD gfx_rect is the last command, so X/Y/W/H hold it; COLS/
         * ROWS/MAP/TILE survive it (only gfx_tilemap writes those). */
        assert(reg(A_COLS) == MAP_COLS && "COLS readback");
        assert(reg(A_ROWS) == MAP_ROWS && "ROWS readback");
        assert(reg(A_MAP)  == (uint32_t)sym_cells &&
               "MAP_ADDR must point at the guest's cells[]");
        assert(reg(A_TILE) < ARCADE1_ROM_SIZE && "TILE_ADDR must point at ROM");
        assert(reg(A_X) == HUD_X && reg(A_Y) == HUD_Y &&
               reg(A_W) == HUD_W && reg(A_H) == HUD_H &&
               "the last command (HUD rect) leaves its geometry behind");

        /* --- Re-render every pixel from the independent model -------------- */
        for (unsigned i = 0; i < MAP_ROWS * MAP_COLS; i++)
            cells[i] = arcade1_read32(&m.bus, (uint32_t)sym_cells + i * 4u);

        model_render(model, cells, cx, cy);
        assert(memcmp(fb, model, sizeof(uint16_t) * ARCADE1_FB_PIXELS) == 0 &&
               "framebuffer must match the independent scroll model");

        /* --- Camera-dependent invariants ---------------------------------- *
         * The HUD stays put; the sprite pans with the camera (its centre is
         * always on-screen for cameras in range, and the sprite is drawn after
         * the map so no tile can overwrite it). */
        assert(fb[PIX(HUD_X + 2, HUD_Y + 2)] == COLOR_HUD &&
               "HUD must not scroll");
        assert(fb[PIX(SPRITE_X - cx + 8, SPRITE_Y - cy + 8)] == COLOR_SPR &&
               "world sprite must pan with the camera");

        /* --- Per-frame determinism hash ------------------------------------ */
        h = state_hash(&m.cpu, &m.bus);
        printf("frame %4u hash=0x%016llX fb=0x%016llX\n", frame_count,
               (unsigned long long)h, (unsigned long long)fb_hash(&m));
        if (verify_hashes) {
            assert(frame_count < nhist1 && "replay longer than original run");
            assert(h == hist1[frame_count] && "per-frame hash diverged");
        } else {
            assert(frame_count < sizeof hist1 / sizeof hist1[0]);
            hist1[frame_count] = h;
        }
        frame_count++;
    }

    /* Timer is pure frame time; one present per frame; no stray accesses. */
    assert(m.timer.count == frame_count && "timer == frames run");
    assert(m.video.presents == frame_count && "one present per frame");
    assert(m.bus.stray_accesses == 0 && "guest must not stray outside memory");

    printf("cpu load: %llu inst/frame avg, %llu max (%u frames)\n",
           (unsigned long long)(total_inst / frames_this_pass),
           (unsigned long long)max_inst, frames_this_pass);
    video_budget_report("scroll", &vbudget);   /* task C */
}

int main(int argc, char **argv)
{
    FILE *f;
    long n;

    if (argc != 3) {
        fprintf(stderr,
                "usage: %s <image.arc1|bin> <cells_hex>\n", argv[0]);
        return 2;
    }

    f = fopen(argv[1], "rb");
    assert(f && "cannot open guest image");
    n = (long)fread(img, 1, sizeof img, f);
    assert(n > 0 && (size_t)n <= sizeof img && "ROM image must fit 4 KiB");
    fclose(f);
    img_size = (size_t)n;

    sym_cells = (int32_t)strtoul(argv[2], NULL, 16);

    /* Pass 1: run the sweep, recording the hash of every frame. */
    printf("--- pass 1: camera sweep ---\n");
    scenario(false);
    nhist1 = frame_count;
    memcpy(fb1, m.framebuffer, sizeof fb1);

    /* Pass 2: arcade1_reset reboots the guest; the identical sweep must yield
     * the identical hash sequence and screen. */
    printf("--- pass 2: reset and replay ---\n");
    scenario(true);
    assert(frame_count == nhist1 && "replay frame count diverged");
    assert(memcmp(fb1, m.framebuffer, sizeof fb1) == 0 &&
           "framebuffer diverged after reset");

    printf("milestone-21: scroll ok (%u frames, two identical runs)\n", nhist1);
    printf("state hash: 0x%016llX\n",
           (unsigned long long)hist1[nhist1 - 1]);
    return 0;
}
