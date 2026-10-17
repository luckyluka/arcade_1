/* ARCADE-1 — synthetic video worst-case harness (BEFORE_M25 task C step 3).
 *
 * MEASUREMENT ONLY. Sizes a per-frame work budget from data by driving the
 * video device with hand-assembled guests through the worst cases a guest can
 * produce, and reading the device's own end-of-frame counters
 * (arcade1_video_frame_totals). Links the machine sources directly: no
 * libretro, no .so, no RISC-V toolchain.
 *
 * Five cases, each run for exactly one frame:
 *
 *   (a) one full-screen CLEAR
 *   (b) 100 full-screen CLEARs
 *   (c) a full-screen TILEMAP: 30x16 cells of 16x16 tiles = 480 cells, every
 *       cell non-empty with its own index and its own tint. All 480 distinct
 *       32-byte tiles fit in the 64 KiB RAM (2048 addressable), so every cell
 *       draws and pixels_written tracks the full-screen worst case. The
 *       FETCH-side work (cells_visited, bytes_fetched) is likewise the true
 *       full-screen worst case.
 *       A second variant reuses one all-set tile across all 480 cells to show
 *       the true pixel-write worst case for the same fetch cost.
 *   (d) 128 overlapping 32x32 all-set sprites
 *   (e) the most commands a guest can issue inside the 1,000,000-step cap
 *       using the cheapest instruction sequence (see the arithmetic below)
 *
 * Every number printed is MEASURED from the device counters; the assertions
 * are the arithmetic prediction of each case, so a mismatch is a real
 * measurement bug, not a re-pinned constant.
 */

#include "machine.h"
#include "memory.h"
#include "video.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Screen geometry: the arithmetic below is written against these. */
#define SCR_W  ARCADE1_WIDTH        /* 480 */
#define SCR_H  ARCADE1_HEIGHT       /* 270 */
#define SCREEN_PIXELS ((uint32_t)ARCADE1_FB_PIXELS)

#define MMIO(off) (ARCADE1_MMIO_BASE + (off))

/* --- Hand-assembled guests (RV32I field encoders, as in test_exec) ------- */

static uint32_t enc_i(uint32_t funct3, uint32_t rd, uint32_t rs1, int32_t imm)
{
    return (((uint32_t)imm & 0xFFFu) << 20) | (rs1 << 15) | (funct3 << 12) |
           (rd << 7) | 0x13u;
}
static uint32_t enc_s(uint32_t funct3, uint32_t rs1, uint32_t rs2, int32_t imm)
{
    uint32_t u = (uint32_t)imm;
    return ((u >> 5) << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
           ((u & 0x1Fu) << 7) | 0x23u;
}
static uint32_t enc_u(uint32_t rd, uint32_t imm20)
{
    return (imm20 << 12) | (rd << 7) | 0x37u;
}
static uint32_t enc_j(uint32_t rd, int32_t imm)
{
    uint32_t u = (uint32_t)imm;
    return (((u >> 20) & 1u) << 31) | (((u >> 1) & 0x3FFu) << 21) |
           (((u >> 11) & 1u) << 20) | (((u >> 12) & 0xFFu) << 12) |
           (rd << 7) | 0x6Fu;
}

#define ADDI(rd, rs1, imm) enc_i(0u, (rd), (rs1), (imm))
#define SW(rs2, rs1, imm)  enc_s(2u, (rs1), (rs2), (imm))
#define LUI(rd, imm20)     enc_u((rd), (imm20))
#define JAL(rd, imm)       enc_j((rd), (imm))

#define CMD_OFF 0x10Cu            /* VIDEO_CMD register offset */
#define X1_MMIO LUI(1, 0x40000u)  /* x1 = 0x40000000 (MMIO base)   */

/* Load a flat image as firmware (resets the CPU to the vector). */
static void boot(arcade1_machine_t *m, const uint8_t *img, size_t n)
{
    arcade1_init(m);
    assert(arcade1_load_rom(&m->bus, img, n) == n && "image must fit ROM");
    arcade1_set_firmware(m, true);
}

/* Serialise words little-endian into an image. */
static size_t pack(uint8_t *img, const uint32_t *w, size_t nwords)
{
    for (size_t i = 0; i < nwords; i++) {
        img[4 * i + 0] = (uint8_t)(w[i] & 0xFFu);
        img[4 * i + 1] = (uint8_t)((w[i] >> 8) & 0xFFu);
        img[4 * i + 2] = (uint8_t)((w[i] >> 16) & 0xFFu);
        img[4 * i + 3] = (uint8_t)((w[i] >> 24) & 0xFFu);
    }
    return 4 * nwords;
}

/* --- Case (a): one full-screen CLEAR ------------------------------------- */

static void case_one_clear(void)
{
    arcade1_machine_t m;
    const uint32_t prog[] = {
        X1_MMIO,                 /* x1 = 0x40000000                              */
        ADDI(2, 0, 1),           /* x2 = 1 (CLEAR)                           */
        ADDI(3, 0, 3),           /* x3 = 3 (PRESENT)                         */
        SW(2, 1, CMD_OFF),       /* CMD = CLEAR                              */
        SW(3, 1, CMD_OFF),       /* CMD = PRESENT  (ends the frame)          */
        JAL(0, 0),               /* park                                     */
    };
    uint8_t img[4 * 16];
    const arcade1_video_stats_t *s;

    boot(&m, img, pack(img, prog, sizeof prog / sizeof prog[0]));
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_COLOR), 0x1234u);

    arcade1_run_frame(&m);
    s = arcade1_video_frame_totals(&m.video);

    /* Arithmetic: 1 CLEAR writes every screen pixel; +1 PRESENT. */
    assert(s->cmds_clear == 1u && "exactly one CLEAR");
    assert(s->cmds_present == 1u && "exactly one PRESENT");
    assert(s->commands == 2u && "CLEAR + PRESENT");
    assert(s->pixels_written == SCREEN_PIXELS && "CLEAR writes the screen");
    assert(s->bytes_fetched == 0u && "CLEAR fetches nothing");
    assert(s->cells_visited == 0u && "no TILEMAP ran");

    printf("  (a) 1 CLEAR        : cmd %u (clear %u, present %u), "
           "px %u, bytes %u, cells %u\n",
           s->commands, s->cmds_clear, s->cmds_present,
           s->pixels_written, s->bytes_fetched, s->cells_visited);
}

/* --- Case (b): 100 full-screen CLEARs ------------------------------------ */

#define N_CLEARS 100u

static void case_many_clears(void)
{
    arcade1_machine_t m;
    uint32_t prog[4 + N_CLEARS + 3];
    size_t n = 0;
    uint8_t img[4 * 128];
    const arcade1_video_stats_t *s;

    prog[n++] = X1_MMIO;                    /* x1 = 0x40000000                 */
    prog[n++] = ADDI(2, 0, 1);              /* x2 = 1 (CLEAR)              */
    prog[n++] = ADDI(3, 0, 3);              /* x3 = 3 (PRESENT)            */
    for (unsigned i = 0; i < N_CLEARS; i++)
        prog[n++] = SW(2, 1, CMD_OFF);      /* CMD = CLEAR                 */
    prog[n++] = SW(3, 1, CMD_OFF);          /* CMD = PRESENT               */
    prog[n++] = JAL(0, 0);                  /* park                        */

    boot(&m, img, pack(img, prog, n));
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_COLOR), 0x1234u);

    arcade1_run_frame(&m);
    s = arcade1_video_frame_totals(&m.video);

    /* Arithmetic: 100 CLEARs each rewrite the whole screen. */
    assert(s->cmds_clear == N_CLEARS && "100 CLEARs");
    assert(s->commands == N_CLEARS + 1u && "100 CLEARs + PRESENT");
    assert(s->pixels_written == N_CLEARS * SCREEN_PIXELS &&
           "each CLEAR rewrites every pixel");
    assert(s->bytes_fetched == 0u && "CLEARs fetch nothing");

    printf("  (b) %u CLEARs      : cmd %u, px %u (= %u x screen), bytes %u\n",
           N_CLEARS, s->commands, s->pixels_written, N_CLEARS,
           s->bytes_fetched);
}

/* --- Case (c): full-screen TILEMAP, 30x16 cells of 16x16 tiles ----------- */

#define TILE_W   16u
#define TILE_H   16u
#define MAP_COLS ((SCR_W) / (TILE_W))   /* 30 */
#define MAP_ROWS ((SCR_H) / (TILE_H))   /* 16 */
#define MAP_CELLS (MAP_COLS * MAP_ROWS)  /* 480 */
#define TILE_BYTES ((TILE_H) * ((TILE_W) / 8u))   /* 32 */
#define CELL_ADDR 0x0400u              /* cell array lives in the ROM image */
#define TILE_ADDR 0x20000000u       /* tile set lives in RAM (all 0xFF)  */

/* Build the ROM image: [guest code][cell array]. Each cell gets its own
 * tile index (1..480) and its own tint; `reuse_one_tile` instead points
 * every cell at the single all-set tile 1 (the true pixel-write worst case
 * for the same fetch cost). */
static size_t build_tilemap_image(uint8_t *img, int reuse_one_tile)
{
    const uint32_t prog[] = {
        X1_MMIO,                 /* x1 = 0x40000000                              */
        ADDI(2, 0, 6),           /* x2 = 6 (TILEMAP)                         */
        ADDI(3, 0, 3),           /* x3 = 3 (PRESENT)                         */
        SW(2, 1, CMD_OFF),       /* CMD = TILEMAP                            */
        SW(3, 1, CMD_OFF),       /* CMD = PRESENT                            */
        JAL(0, 0),               /* park                                     */
    };
    const size_t code_bytes = sizeof prog;
    size_t n = pack(img, prog, sizeof prog / sizeof prog[0]);

    assert(code_bytes <= CELL_ADDR && "code must not overlap the cell array");

    /* Cell array at CELL_ADDR (little-endian 32-bit words). */
    for (uint32_t i = 0; i < MAP_CELLS; i++) {
        const uint32_t index = reuse_one_tile ? 1u : (i + 1u);
        const uint32_t tint  = 0x0001u * ((i * 37u) & 0x1Fu);  /* distinct */
        const uint32_t cell  = (tint << 16) | index;
        const size_t at = CELL_ADDR + 4u * i;

        img[at + 0] = (uint8_t)(cell & 0xFFu);
        img[at + 1] = (uint8_t)((cell >> 8) & 0xFFu);
        img[at + 2] = (uint8_t)((cell >> 16) & 0xFFu);
        img[at + 3] = (uint8_t)((cell >> 24) & 0xFFu);
        if (at + 4u > n)
            n = at + 4u;
    }
    return n;
}

static void case_tilemap(int reuse_one_tile)
{
    arcade1_machine_t m;
    uint8_t img[4 * 1024];
    const size_t n = build_tilemap_image(img, reuse_one_tile);
    const arcade1_video_stats_t *s;
    /* How many of the 480 distinct tiles land inside the 64 KiB of RAM. */
    const uint32_t addrable_tiles =
        (ARCADE1_RAM_SIZE) / TILE_BYTES;             /* 65536 / 32 = 2048 */
    const uint32_t full_tiles =
        (MAP_CELLS < addrable_tiles) ? MAP_CELLS : addrable_tiles;
    /* How many tiles actually have set bits: with distinct indices only the
     * addressable ones do, but the reuse variant points every cell at the one
     * all-set tile. */
    const uint32_t full_tiles_written =
        reuse_one_tile ? MAP_CELLS : full_tiles;

    boot(&m, img, n);
    memset(m.bus.ram, 0xFF, ARCADE1_RAM_SIZE);   /* every tile bit set */

    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_X), 0u);
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_Y), 0u);
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_W), TILE_W);
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_H), TILE_H);
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_COLS), MAP_COLS);
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_ROWS), MAP_ROWS);
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_MAP_ADDR), CELL_ADDR);
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_TILE_ADDR), TILE_ADDR);

    arcade1_run_frame(&m);
    s = arcade1_video_frame_totals(&m.video);

    /* Arithmetic (independent of the data, so exact for both variants):
     * every one of the 480 cells is on screen (no windowing), each costs a
     * 4-byte cell fetch plus one bitmap byte per pixel examined. */
    assert(s->cmds_tilemap == 1u && "exactly one TILEMAP");
    assert(s->cells_visited == MAP_CELLS &&
           "a full-screen map visits every cell");
    assert(s->bytes_fetched == MAP_CELLS * 4u + MAP_CELLS * TILE_W * TILE_H &&
           "4 cell bytes + one bitmap byte per examined pixel, per cell");
    assert(s->pixels_written == full_tiles_written * TILE_W * TILE_H &&
           "only tiles with data can have set bits");

    printf("  (c) TILEMAP %ux%u cells: cmd %u, cells %u, bytes %u, "
           "px %u (%s; %u of %u tiles addressable in 64 KiB RAM)\n",
           MAP_COLS, MAP_ROWS, s->commands, s->cells_visited,
           s->bytes_fetched, s->pixels_written,
           reuse_one_tile ? "one reused all-set tile" : "distinct tiles",
           full_tiles, MAP_CELLS);

    if (reuse_one_tile) {
        /* Same fetch cost as the distinct case, but every cell reuses the one
         * all-set tile, so every examined pixel is written: this is the true
         * pixel-write worst case for a full-screen map. */
        assert(s->bytes_fetched ==
               MAP_CELLS * 4u + MAP_CELLS * TILE_W * TILE_H &&
               "fetch cost is identical to the distinct-tile case");
        printf("       (same map, one reused all-set tile: px %u = full "
               "screen x %u tiles — the pixel-write worst case)\n",
               s->pixels_written, MAP_CELLS);
    }
}

/* --- Case (d): 128 overlapping 32x32 all-set sprites --------------------- */

#define SPR_W   32u
#define SPR_H   32u
#define SPRITES 128u
#define SPR_ADDR 0x20000000u          /* the sprite image lives in RAM */

static void case_sprites(void)
{
    arcade1_machine_t m;
    uint32_t prog[4 + SPRITES + 3];
    size_t n = 0;
    uint8_t img[4 * 160];
    const arcade1_video_stats_t *s;

    prog[n++] = X1_MMIO;                    /* x1 = 0x40000000                 */
    prog[n++] = ADDI(2, 0, 5);              /* x2 = 5 (SPRITE)             */
    prog[n++] = ADDI(3, 0, 3);              /* x3 = 3 (PRESENT)            */
    for (unsigned i = 0; i < SPRITES; i++)
        prog[n++] = SW(2, 1, CMD_OFF);      /* CMD = SPRITE (all overlap)  */
    prog[n++] = SW(3, 1, CMD_OFF);          /* CMD = PRESENT               */
    prog[n++] = JAL(0, 0);                  /* park                        */

    boot(&m, img, pack(img, prog, n));
    memset(m.bus.ram, 0xFF, ARCADE1_RAM_SIZE);   /* all sprite bits set */

    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_X), 0u);
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_Y), 0u);
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_W), SPR_W);
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_H), SPR_H);
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_ADDR), SPR_ADDR);
    arcade1_write32(&m.bus, MMIO(ARCADE1_VIDEO_REG_COLOR), 0xFFFFu);

    arcade1_run_frame(&m);
    s = arcade1_video_frame_totals(&m.video);

    /* Arithmetic: each all-set 32x32 sprite is fully on screen, so every
     * examined pixel is written and every examined byte is fetched. */
    assert(s->cmds_sprite == SPRITES && "128 SPRITEs");
    assert(s->commands == SPRITES + 1u && "128 SPRITEs + PRESENT");
    assert(s->pixels_written == SPRITES * SPR_W * SPR_H &&
           "all-set sprites, fully on screen, write every pixel");
    assert(s->bytes_fetched == SPRITES * SPR_W * SPR_H &&
           "one bitmap byte fetched per examined pixel");

    printf("  (d) %u sprites %ux%u: cmd %u, px %u, bytes %u\n",
           SPRITES, SPR_W, SPR_H, s->commands,
           s->pixels_written, s->bytes_fetched);
}

/* --- Case (e): the most commands inside the 1,000,000-step cap ----------- *
 *
 * One command costs one `sw` (a store to VIDEO_CMD), so the cheapest way to
 * issue commands is a tight loop of stores. A loop needs a back-jump, so the
 * cost per iteration is (K stores + 1 jal) steps for K commands, and the
 * commands per frame is 1,000,000 * K/(K+1): unrolling (raising K) is the
 * only way to shrink the loop overhead.
 *
 * K is bounded by ROM: the image is [LUI][ADDI][K stores][jal] = K+3 words
 * = 4*(K+3) bytes <= 262144, so K <= 65533. The two setup instructions run
 * before the loop, so they are charged against the cap too:
 *     steps for setup   = 2                        (LUI + ADDI)
 *     steps per loop    = K + 1 = 65534            (K stores + 1 jal)
 *     steps for the loop = 1,000,000 - 2 = 999,998
 *     full iterations    = floor(999,998 / 65534) = 15  (15*65534 = 983,010)
 *     leftover steps     = 999,998 - 983,010 = 16,988 (all stores, all cmds)
 *     commands           = 15 * 65533 + 16,988 = 999,983
 *
 * The guest never presents, so the frame ends at the cap (SPEC.md section 8,
 * option B's premise). Every register powers on at 0, so the LUI (MMIO base)
 * and ADDI (SET_PIXEL) are unavoidable.
 */

#define SETUP_WORDS 2u                       /* LUI + ADDI                  */
#define JAL_WORDS   1u
#define K_STORES ((ARCADE1_ROM_SIZE / 4u) - SETUP_WORDS - JAL_WORDS)  /*65533*/

static void case_max_commands(void)
{
    arcade1_machine_t m;
    static uint32_t prog[K_STORES + SETUP_WORDS + JAL_WORDS];
    static uint8_t img[ARCADE1_ROM_SIZE];
    size_t n = 0;
    const arcade1_video_stats_t *s;
    const uint64_t cap = 1000000;
    const uint64_t setup_steps = SETUP_WORDS;                   /* 2      */
    const uint64_t iter_steps = K_STORES + JAL_WORDS;           /* 1022   */
    const uint64_t loop_steps = cap - setup_steps;              /* 999998 */
    const uint64_t full_iters = loop_steps / iter_steps;        /* 978    */
    const uint64_t leftover   = loop_steps % iter_steps;        /* 482    */
    const uint64_t predicted  = full_iters * K_STORES + leftover;

    assert(4u * (K_STORES + SETUP_WORDS + JAL_WORDS) == ARCADE1_ROM_SIZE &&
           "the unrolled loop must fill ROM exactly");

    prog[n++] = X1_MMIO;                    /* x1 = 0x40000000                 */
    prog[n++] = ADDI(2, 0, 2);              /* x2 = 2 (SET_PIXEL, cheapest) */
    for (unsigned i = 0; i < K_STORES; i++)
        prog[n++] = SW(2, 1, CMD_OFF);      /* CMD = SET_PIXEL             */
    /* Back to the first store: it is at byte offset 8, and this JAL sits at
     * byte offset 4*n, so the PC-relative immediate is 8 - 4*n. */
    {
        const int32_t jal_imm = (int32_t)8 - (int32_t)(4u * n);
        prog[n++] = JAL(0, jal_imm);
    }

    boot(&m, img, pack(img, prog, n));

    /* X/Y power on at (0,0), so every SET_PIXEL lands on screen. */
    arcade1_run_frame(&m);
    s = arcade1_video_frame_totals(&m.video);

    /* Arithmetic: the frame must have hit the cap exactly. */
    assert(m.cpu.instret == cap && "the cap ended the frame, not PRESENT");
    assert(s->cmds_present == 0u && "no PRESENT ever ran");
    assert(s->cmds_set_pixel == (uint32_t)predicted &&
           "commands = the unrolled-loop arithmetic above");
    assert(s->commands == (uint32_t)predicted && "all commands were SET_PIXEL");
    assert(s->pixels_written == (uint32_t)predicted &&
           "each SET_PIXEL at (0,0) writes one pixel");

    printf("  (e) max commands    : K=%u stores, %llu steps/iter, "
           "%llu full iters + %llu leftover -> %llu commands "
           "(= %.1f%% of the %llu-step cap)\n",
           K_STORES, (unsigned long long)iter_steps,
           (unsigned long long)full_iters, (unsigned long long)leftover,
           (unsigned long long)predicted,
           100.0 * (double)predicted / (double)cap,
           (unsigned long long)cap);
}

int main(void)
{
    printf("--- synthetic video worst cases (one frame each) ---\n");
    case_one_clear();
    case_many_clears();
    case_tilemap(0);      /* 480 distinct tiles: fetch-side worst case     */
    case_tilemap(1);      /* one reused all-set tile: pixel-write worst    */
    case_sprites();
    case_max_commands();
    printf("video-worst: ok (all five cases match their arithmetic)\n");
    return 0;
}
