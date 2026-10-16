/* ARCADE-1 — Milestone 10 test harness.
 *
 * Proves the full roadmap pipeline:
 *
 *   C source -> RISC-V GCC -> RV32 ELF -> flat binary -> ARCADE-1 VM -> runs
 *
 * Loads a compiled guest image into ROM, resets the CPU, and steps until the
 * program parks in crt0's self-loop (PC stops changing), then checks its
 * observable side effect (counter == 123).
 *
 * Usage: test_program <image.bin> <counter_addr_hex>
 * Links exec.c + cpu.c + memory.c — no libretro, no machine, no .so.
 */

#include "exec.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main(int argc, char **argv)
{
    static arcade1_bus_t bus;          /* Static: the bus embeds 8 KiB. */
    static arcade1_cpu_t cpu;
    static uint8_t image[ARCADE1_ROM_SIZE];

    if (argc != 3) {
        fprintf(stderr, "usage: %s <image.bin> <counter_addr_hex>\n", argv[0]);
        return 2;
    }

    uint32_t addr = (uint32_t)strtoul(argv[2], NULL, 16);
    assert(addr >= ARCADE1_RAM_BASE &&
           addr + 4 <= ARCADE1_RAM_BASE + ARCADE1_RAM_SIZE &&
           "counter must live in RAM (writable)");

    FILE *f = fopen(argv[1], "rb");
    assert(f && "cannot open guest image");
    size_t n = fread(image, 1, sizeof image, f);
    fclose(f);
    assert(n > 0 && n <= sizeof image && "guest image must fit ROM");

    arcade1_bus_init(&bus);
    assert(arcade1_load_rom(&bus, image, n) == n &&
           "guest image must load fully into ROM");
    arcade1_cpu_reset(&cpu);

    /* Run until crt0 parks at `j .` (PC stops changing) or a safety cap. */
    const int MAX_STEPS = 200000;
    uint32_t prev = 0xFFFFFFFFu;
    int steps = 0;
    while (steps < MAX_STEPS && cpu.pc != prev) {
        prev = cpu.pc;
        arcade1_cpu_step(&cpu, &bus);
        steps++;
    }

    uint32_t counter = arcade1_read32(&bus, addr);
    printf("  image: %zu bytes, ran %d steps, stopped at PC=0x%08X\n",
           n, steps, cpu.pc);
    printf("  counter @ 0x%08X = %u (expect 123)\n", addr, counter);

    assert(steps > 0 && "CPU must have executed instructions");
    assert(steps < MAX_STEPS && "program must park (not run to the cap)");
    assert(counter == 123 && "compiled C must set counter = 123");
    assert(bus.stray_accesses == 0 &&
           "guest must not perform any stray bus access");

    printf("milestone10: all assertions passed\n");
    printf("  verified: C -> riscv gcc -> RV32 ELF -> flat binary -> VM runs\n");
    return 0;
}
