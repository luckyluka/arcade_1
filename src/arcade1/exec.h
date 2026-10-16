/* ARCADE-1 CPU execution — RV32I interpreter (milestone 09).
 *
 * Architectural CPU state (32 general registers + PC) and single-instruction
 * stepping. Instruction *decoding* lives in cpu.h (milestone 08); this module
 * fetches a word from the bus, decodes it, and executes it, honouring the
 * RISC-V rule that x0 is hardwired to zero.
 *
 * This is a plain interpreter, not a hardware simulator: no caches, pipelines,
 * speculation, or interrupts. Every access goes through the bus (memory.h), so
 * there is no host/libretro dependency — the CPU is usable stand-alone (see
 * tests/test_exec.c).
 */

#ifndef ARCADE1_EXEC_H
#define ARCADE1_EXEC_H

#include "cpu.h"     /* decoder: arcade1_insn_t, arcade1_decode          */
#include "memory.h"  /* arcade1_bus_t                                    */

#include <stdint.h>

/* --- CPU architectural state -------------------------------------------- *
 * 32 x 32-bit general registers + program counter. x0 is hardwired to zero:
 * reads of x0 always yield 0 and writes to x0 are discarded. */
typedef struct arcade1_cpu {
    uint32_t regs[32];
    uint32_t pc;
} arcade1_cpu_t;

/* Power-on reset: all registers cleared to 0, PC set to the ROM base (the
 * entry point). */
void arcade1_cpu_reset(arcade1_cpu_t *cpu);

/* Execute the single instruction at PC, then advance PC. Deterministic and
 * never faults: unmapped fetches and unknown encodings behave as a NOP
 * (PC += 4). */
void arcade1_cpu_step(arcade1_cpu_t *cpu, arcade1_bus_t *bus);

#endif /* ARCADE1_EXEC_H */
