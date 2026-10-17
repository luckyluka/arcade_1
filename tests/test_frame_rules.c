/* ARCADE-1 — SPEC section 8 "Frame boundaries" harness (BEFORE_M25 task B).
 *
 * Proves, with hand-assembled guests and NO RISC-V compiler, exactly what the
 * reference does at the end of a frame (SPEC.md section 8). Links the machine
 * sources directly — no libretro, no .so, no guest toolchain.
 *
 * The three frame-ending rules, each with its own tiny guest:
 *
 *   1. PRESENT ends the frame IMMEDIATELY. A marker instruction placed after
 *      the PRESENT store runs only in the NEXT frame: after frame 1 the
 *      marker's register is untouched and its pixel is not on screen.
 *   2. A parked guest (pc stops changing, `j .`) ends every frame on its
 *      first step, forever.
 *   3. A 2-instruction counting loop with no PRESENT runs exactly 1,000,000
 *      steps in one frame; the next frame continues the count from there.
 *
 * For each case it pins the timer tick and STATUS (the PRESENT count) exactly
 * as the code behaves: the timer ticks once per arcade1_run_frame() call
 * whatever ended the frame, STATUS counts PRESENTs only, and instret/step
 * counts are diagnostic (SPEC.md section 8).
 *
 * Every printed value is the MEASURED one from src/arcade1/ — this harness
 * pins behaviour, it does not predict it.
 */

#include "machine.h"    /* arcade1_machine_t + init/run_frame/firmware */
#include "memory.h"     /* ARCADE1_MMIO_BASE, arcade1_read32 */
#include "video.h"      /* ARCADE1_VIDEO_REG_* */
#include "timer.h"      /* ARCADE1_TIMER_REG_COUNT */

#include <assert.h>
#include <stdio.h>
#include <stdint.h>

/* Absolute MMIO addresses the guests drive. */
#define CMD_ADDR     (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_CMD)
#define STATUS_ADDR  (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_STATUS)
#define TIMER_ADDR   (ARCADE1_MMIO_BASE + ARCADE1_TIMER_REG_COUNT)

#define VIDEO_X_ADDR (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_X)
#define VIDEO_Y_ADDR (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_Y)
#define COLOR_ADDR   (ARCADE1_MMIO_BASE + ARCADE1_VIDEO_REG_COLOR)

#define PIX(x, y) ((y) * ARCADE1_WIDTH + (x))

/* --- Hand-assembled guests (RV32I field encoders, as in test_exec) ------- */

static uint32_t enc_i(uint32_t funct3, uint32_t rd, uint32_t rs1, int32_t imm)
{
    return (((uint32_t)imm & 0xFFFu) << 20) | (rs1 << 15) | (funct3 << 12) |
           (rd << 7) | 0x13u;                    /* OP-IMM */
}

static uint32_t enc_s(uint32_t funct3, uint32_t rs1, uint32_t rs2, int32_t imm)
{
    uint32_t u = (uint32_t)imm;
    return ((u >> 5) << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
           ((u & 0x1Fu) << 7) | 0x23u;           /* STORE */
}

static uint32_t enc_u(uint32_t opcode, uint32_t rd, uint32_t imm20)
{
    return (imm20 << 12) | (rd << 7) | opcode;
}

static uint32_t enc_j(uint32_t rd, int32_t imm)   /* JAL */
{
    uint32_t u = (uint32_t)imm;
    return (((u >> 20) & 1u) << 31) | (((u >> 1) & 0x3FFu) << 21) |
           (((u >> 11) & 1u) << 20) | (((u >> 12) & 0xFFu) << 12) |
           (rd << 7) | 0x6Fu;
}

/* Serialise instruction words little-endian into a flat image and boot it as
 * firmware (which resets the CPU, so execution starts at the reset vector). */
static void boot(arcade1_machine_t *m, const uint32_t *prog, size_t n)
{
    uint8_t img[4 * 64];

    assert(n <= 64 && "guest too large for harness buffer");
    for (size_t i = 0; i < n; i++) {
        img[4 * i + 0] = (uint8_t)(prog[i] & 0xFFu);
        img[4 * i + 1] = (uint8_t)((prog[i] >> 8) & 0xFFu);
        img[4 * i + 2] = (uint8_t)((prog[i] >> 16) & 0xFFu);
        img[4 * i + 3] = (uint8_t)((prog[i] >> 24) & 0xFFu);
    }
    arcade1_init(m);
    assert(arcade1_load_rom(&m->bus, img, sizeof img) == sizeof img &&
           "guest image must fit ROM");
    arcade1_set_firmware(m, true);   /* resets the CPU to the vector */
}

#define ADDI(rd, rs1, imm) enc_i(0u, (rd), (rs1), (imm))  /* funct3 = 0 */
#define SW(rs2, rs1, imm)  enc_s(2u, (rs1), (rs2), (imm)) /* funct3 = 2 */
#define LUI(rd, imm20)     enc_u(0x37u, (rd), (imm20))
#define JAL(rd, imm)       enc_j((rd), (imm))

/* --- Case 1 + 2: PRESENT ends the frame; a marker runs next frame; then the
 * guest parks. Draws pixel A BEFORE the present and pixel B (the marker)
 * after it, so the framebuffer itself shows where the frame ended.
 * Automatic (not static): the encoders are runtime calls, as in test_exec. */
static void test_present_ends_frame_immediately(void)
{
    const uint32_t present_guest[] = {
        LUI (1, 0x40000u),   /* x1 = 0x40000000 (MMIO base)             */
        ADDI(5, 0, 10),      /* x5 = 10                                */
        SW  (5, 1, 0x100),   /* X     = 10                             */
        SW  (5, 1, 0x104),   /* Y     = 10                             */
        ADDI(6, 0, 0x7E0),   /* x6 = 0x07E0 (green; fits a 12-bit imm) */
        SW  (6, 1, 0x108),   /* COLOR = 0x07E0                         */
        ADDI(2, 0, 2),       /* x2 = 2 (SET_PIXEL)                     */
        SW  (2, 1, 0x10C),   /* CMD = SET_PIXEL  -> pixel (10,10)      */
        ADDI(2, 0, 3),       /* x2 = 3 (PRESENT)                       */
        SW  (2, 1, 0x10C),   /* CMD = PRESENT    <- FRAME 1 ENDS HERE  */
        /* --- marker: must NOT run in frame 1 ------------------------ */
        ADDI(3, 0, 42),      /* x3 = 42  (the marker)                  */
        ADDI(5, 0, 20),      /* x5 = 20                                */
        SW  (5, 1, 0x100),   /* X     = 20                             */
        SW  (5, 1, 0x104),   /* Y     = 20                             */
        ADDI(6, 0, 31),      /* x6 = 0x001F (blue)                     */
        SW  (6, 1, 0x108),   /* COLOR = 0x001F                         */
        ADDI(2, 0, 2),       /* x2 = 2 (SET_PIXEL)                     */
        SW  (2, 1, 0x10C),   /* CMD = SET_PIXEL  -> pixel (20,20)      */
        ADDI(2, 0, 3),       /* x2 = 3 (PRESENT)                       */
        SW  (2, 1, 0x10C),   /* CMD = PRESENT    <- FRAME 2 ENDS HERE  */
        JAL (0, 0),          /* park: `j .`                            */
    };
    arcade1_machine_t m;
    const uint16_t *fb;

    boot(&m, present_guest, sizeof present_guest / sizeof present_guest[0]);
    fb = arcade1_framebuffer(&m);

    /* --- Frame 1: ends at the FIRST PRESENT (instruction 10 of 21) ---- */
    arcade1_run_frame(&m);
    assert(m.cpu.regs[3] == 0 &&
           "marker after PRESENT must not run in the same frame");
    assert(fb[PIX(10, 10)] == 0x07E0 && "pixel drawn BEFORE present shows");
    assert(fb[PIX(20, 20)] == 0 &&
           "marker pixel drawn AFTER present must not be on screen");
    assert(arcade1_read32(&m.bus, STATUS_ADDR) == 1 &&
           "STATUS == 1 after the first PRESENT");
    assert(arcade1_read32(&m.bus, TIMER_ADDR) == 1 &&
           "timer ticks once per frame even when PRESENT ends it");
    assert(m.cpu.instret == 10 &&
           "frame 1 ran exactly the 10 instructions up to PRESENT");
    assert(m.cpu.pc == 0x28 &&
           "next frame resumes at the instruction AFTER the PRESENT");

    /* --- Frame 2: resumes at the marker, ends at the second PRESENT ---- */
    arcade1_run_frame(&m);
    assert(m.cpu.regs[3] == 42 && "marker runs in the NEXT frame");
    assert(fb[PIX(20, 20)] == 0x001F && "marker pixel now on screen");
    assert(arcade1_read32(&m.bus, STATUS_ADDR) == 2 && "STATUS == 2");
    assert(arcade1_read32(&m.bus, TIMER_ADDR) == 2 && "timer == 2");
    assert(m.cpu.instret == 20 && "frame 2 ran the 10 marker instructions");

    /* --- Frames 3-5: the guest is parked, every frame ends immediately -- */
    for (uint32_t f = 3; f <= 5; f++) {
        const uint64_t before = m.cpu.instret;

        arcade1_run_frame(&m);
        assert(m.cpu.instret - before == 1 &&
               "a parked guest ends every frame on its first step");
        assert(arcade1_read32(&m.bus, STATUS_ADDR) == 2 &&
               "parked frames present nothing: STATUS must not move");
        assert(arcade1_read32(&m.bus, TIMER_ADDR) == f &&
               "the timer still ticks once per parked frame");
        assert(m.cpu.regs[3] == 42 && "parking does not re-run the marker");
    }

    printf("  case 1+2: PRESENT ended frame 1 at instret 10 (pc=0x%03X); "
           "marker ran frame 2; frames 3-5 parked at 1 step, STATUS stayed 2, "
           "timer reached %u\n", 0x28u, arcade1_read32(&m.bus, TIMER_ADDR));
}

static void test_cap_ends_frame(void)
{
    /* Case 3: a 2-instruction counting loop with no PRESENT, so only the
     * 1,000,000-step cap can end the frame. */
    const uint32_t counter_guest[] = {
        ADDI(5, 5, 1),       /* x5++                                   */
        JAL (0, -4),         /* loop back one instruction              */
    };
    arcade1_machine_t m;
    const uint64_t cap = 1000000;   /* SPEC section 8: the step cap */

    boot(&m, counter_guest, sizeof counter_guest / sizeof counter_guest[0]);

    /* --- Frame 1: exactly the cap, no PRESENT anywhere ------------------ */
    arcade1_run_frame(&m);
    assert(m.cpu.instret == cap &&
           "one frame runs exactly 1,000,000 steps when nothing ends it "
           "sooner");
    assert(m.cpu.regs[5] == (uint32_t)(cap / 2) &&
           "2 instructions per iteration -> cap/2 increments");
    assert(arcade1_read32(&m.bus, STATUS_ADDR) == 0 &&
           "no PRESENT ever ran: STATUS must stay 0");
    assert(arcade1_read32(&m.bus, TIMER_ADDR) == 1 &&
           "the timer ticks once even for a cap-terminated frame");
    assert(m.cpu.pc == 0u &&
           "an even step count returns to the top of the loop");

    /* --- Frame 2: continues the count from where frame 1 stopped -------- */
    arcade1_run_frame(&m);
    assert(m.cpu.instret == 2 * cap &&
           "the next frame continues stepping, it does not restart");
    assert(m.cpu.regs[5] == (uint32_t)cap &&
           "counter continues: cap/2 more increments");
    assert(arcade1_read32(&m.bus, STATUS_ADDR) == 0 && "STATUS still 0");
    assert(arcade1_read32(&m.bus, TIMER_ADDR) == 2 && "timer == 2");

    printf("  case 3: cap-terminated frames ran %llu and %llu steps, "
           "counter %u -> %u, STATUS 0, timer 1 -> 2\n",
           (unsigned long long)cap, (unsigned long long)(2 * cap),
           (unsigned)(cap / 2), (unsigned)cap);
}

int main(void)
{
    printf("--- case 1+2: PRESENT and parked guests ---\n");
    test_present_ends_frame_immediately();
    printf("--- case 3: 1,000,000-step cap ---\n");
    test_cap_ends_frame();
    printf("spec-8 frame rules: ok "
           "(PRESENT ends the frame, parked guests re-end instantly, "
           "the cap is exactly 1e6 steps and the next frame continues)\n");
    return 0;
}
