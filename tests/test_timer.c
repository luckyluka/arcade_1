/* ARCADE-1 — Milestone 12 test harness (timer).
 *
 * Proves docs.md milestone 12: a deterministic timer suitable for games, with
 * the required relationship between CPU execution, frame timing and the
 * timer — and no dependence on host wall-clock time.
 *
 * Links machine.c + timer.c + memory.c + exec.c + cpu.c (no libretro, no .so).
 * The scenario below is run TWICE with a reset in between (reset-and-compare):
 *
 *   - arcade1_run_frame() advances TIMER_COUNT by exactly one tick per frame;
 *   - the count is observed through the bus's MMIO dispatch both by host
 *     level arcade1_read32() calls and by a hand-assembled RV32I program
 *     (LUI + LW) executed on the CPU;
 *   - TIMER_CTRL bit0 pauses and resumes ticking without stopping frames;
 *   - TIMER_COUNT is read-only and unknown offsets are harmless;
 *   - stray_accesses stays 0 (every access reaches the device);
 *   - after arcade1_reset() + arcade1_cpu_reset() the replayed scenario
 *     reproduces the identical timer count and state hash.
 *
 * Usage: test_timer (no arguments).
 */

#include "machine.h"
#include "exec.h"
#include "state_hash.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>

/* Absolute MMIO addresses of the timer's two registers. */
#define TIMER_COUNT_ADDR (ARCADE1_MMIO_BASE + ARCADE1_TIMER_REG_COUNT)
#define TIMER_CTRL_ADDR  (ARCADE1_MMIO_BASE + ARCADE1_TIMER_REG_CTRL)

/* --- Hand-assembled guest (RV32I field encoders, as in test_exec) -------- */

static uint32_t enc_u(uint32_t opcode, uint32_t rd, uint32_t imm20)
{
    return (imm20 << 12) | (rd << 7) | opcode;
}

static uint32_t enc_i(uint32_t opcode, uint32_t funct3, uint32_t rd,
                      uint32_t rs1, int32_t imm)
{
    return (((uint32_t)imm & 0xFFFu) << 20) | (rs1 << 15) | (funct3 << 12) |
           (rd << 7) | opcode;
}

/* Load `lui x1, 0x4000 ; lw x2, 0(x1)` into ROM so the CPU can read the
 * timer register exactly as a game would. */
static void load_timer_reader(arcade1_bus_t *bus)
{
    const uint32_t prog[] = {
        enc_u(0x37, 1, 0x4u),      /* LUI x1, 0x4    -> x1 = 0x00004000 */
        enc_i(0x03, 2, 2, 1, 0),   /* LW  x2, 0(x1)  -> x2 = TIMER_COUNT */
    };
    uint8_t img[sizeof prog];

    for (size_t i = 0; i < sizeof prog; i++)
        img[i] = (uint8_t)((prog[i / 4] >> (8 * (i % 4))) & 0xFFu);

    assert(arcade1_load_rom(bus, img, sizeof img) == sizeof img &&
           "timer-reader program must fit ROM");
}

/* --- The scenario --------------------------------------------------------- *
 * One identical sequence of frames, CPU steps and register writes. It
 * asserts the observable behaviour at every step, so running it after a reset
 * must reach the same end state (count + hash). */
static void scenario(arcade1_machine_t *m, arcade1_cpu_t *cpu)
{
    /* --- One tick per frame: 50 frames -> count 50 --------------------- */
    for (int i = 0; i < 50; i++)
        arcade1_run_frame(m);
    assert(arcade1_read32(&m->bus, TIMER_COUNT_ADDR) == 50u &&
           "timer must tick exactly once per frame");
    assert(m->frame_counter == 50u && "frames must advance with the count");

    /* --- A CPU instruction reads the frame-advanced count via MMIO ------ */
    arcade1_cpu_reset(cpu);
    arcade1_cpu_step(cpu, &m->bus);   /* LUI x1, 0x4000 */
    arcade1_cpu_step(cpu, &m->bus);   /* LW  x2, 0(x1)  */
    assert(cpu->regs[1] == ARCADE1_MMIO_BASE &&
           "LUI must compute the MMIO window base");
    assert(cpu->regs[2] == 50u &&
           "a CPU lw must observe the frame-advanced timer count");
    assert(cpu->instret == 2 && "two instructions must be counted");

    /* --- CTRL bit0 pauses the timer (frames keep running) --------------- */
    arcade1_write32(&m->bus, TIMER_CTRL_ADDR, 0u);
    assert(arcade1_read32(&m->bus, TIMER_CTRL_ADDR) == 0u &&
           "CTRL must read back 0 (paused)");
    for (int i = 0; i < 10; i++)
        arcade1_run_frame(m);
    assert(arcade1_read32(&m->bus, TIMER_COUNT_ADDR) == 50u &&
           "count must freeze while the timer is paused");
    assert(m->frame_counter == 60u &&
           "frames must keep advancing while only the timer is paused");

    /* --- Re-enabling resumes ticking from where it stopped -------------- */
    arcade1_write32(&m->bus, TIMER_CTRL_ADDR, ARCADE1_TIMER_CTRL_ENABLE);
    assert(arcade1_read32(&m->bus, TIMER_CTRL_ADDR) ==
               ARCADE1_TIMER_CTRL_ENABLE &&
           "CTRL must read back enabled");
    for (int i = 0; i < 5; i++)
        arcade1_run_frame(m);
    assert(arcade1_read32(&m->bus, TIMER_COUNT_ADDR) == 55u &&
           "count must resume after re-enabling");

    /* --- COUNT is read-only; unknown offsets are harmless ---------------- */
    arcade1_write32(&m->bus, TIMER_COUNT_ADDR, 0x1234u);
    assert(arcade1_read32(&m->bus, TIMER_COUNT_ADDR) == 55u &&
           "TIMER_COUNT must be read-only");
    assert(arcade1_read32(&m->bus, ARCADE1_MMIO_BASE + 0x40u) == 0u &&
           "unknown timer offset must read 0");

    /* Every access above reached the attached device: no stray accesses. */
    assert(m->bus.stray_accesses == 0 &&
           "every timer access must reach the device (strays stay 0)");
}

/* --- Harness -------------------------------------------------------------- */

int main(void)
{
    static arcade1_machine_t m;   /* Static: the machine embeds the buffers. */
    static arcade1_cpu_t cpu;

    arcade1_init(&m);
    /* Both runs start from the identical reset state (count 0, RAM clear,
     * ROM and the timer's MMIO attachment preserved). */
    arcade1_reset(&m);
    load_timer_reader(&m.bus);
    arcade1_cpu_reset(&cpu);

    /* --- Power-on state -------------------------------------------------- */
    assert(arcade1_read32(&m.bus, TIMER_COUNT_ADDR) == 0u &&
           "count must start at 0");
    assert(arcade1_read32(&m.bus, TIMER_CTRL_ADDR) ==
               ARCADE1_TIMER_CTRL_ENABLE &&
           "timer must start enabled");

    /* --- Run 1 ----------------------------------------------------------- */
    scenario(&m, &cpu);
    const uint32_t count1 = arcade1_read32(&m.bus, TIMER_COUNT_ADDR);
    const uint64_t hash1  = state_hash(&cpu, &m.bus);

    /* --- Determinism: reset reproduces the identical state --------------- */
    /* arcade1_reset() clears the frame counter, the timer and RAM, keeps the
     * loaded ROM and the timer's MMIO attachment; the CPU resets too. The
     * replayed scenario must end in the exact same state. */
    arcade1_reset(&m);
    arcade1_cpu_reset(&cpu);

    assert(arcade1_read32(&m.bus, TIMER_COUNT_ADDR) == 0u &&
           "reset must clear the timer count");

    scenario(&m, &cpu);
    const uint32_t count2 = arcade1_read32(&m.bus, TIMER_COUNT_ADDR);
    const uint64_t hash2  = state_hash(&cpu, &m.bus);

    assert(count1 == count2 && "reset must reproduce the timer count");
    assert(hash1 == hash2 && "reset must reproduce the state hash");

    printf("milestone12: all assertions passed\n");
    printf("  timer: count run1=%u run2=%u (frames each run: 65)\n",
           count1, count2);
    printf("  state hash: run1=0x%016llX run2=0x%016llX\n",
           (unsigned long long)hash1, (unsigned long long)hash2);
    printf("  verified: 1 tick/frame, CPU lw reads COUNT via MMIO,\n");
    printf("            CTRL pause/resume, read-only COUNT, reset,\n");
    printf("            deterministic under reset (reset-and-compare)\n");
    return 0;
}
