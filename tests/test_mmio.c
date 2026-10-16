/* ARCADE-1 — Milestone 11 test harness (MMIO).
 *
 * Proves the roadmap's milestone-11 goal: a guest program can write/read a
 * device register. The harness registers a minimal device on the bus's MMIO
 * region, loads a compiled guest image (mmio_demo), and runs it. The guest
 * reads a host-provided value from one register and writes value+1 to another;
 * the harness asserts the device handler observed exactly that write — only
 * possible if the guest reached the device through the bus's MMIO dispatch.
 * A reset-and-compare pass then re-runs the program to confirm the dispatch is
 * deterministic.
 *
 * Usage: test_mmio <image.bin>
 * Links exec.c + cpu.c + memory.c — no libretro, no machine, no .so.
 */

#include "exec.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* --- Minimal demonstration device ---------------------------------------- */

#define DEV_REG_IN     0u    /* offset 0: host -> guest (readable)   */
#define DEV_REG_OUT    4u    /* offset 4: guest -> host (writable)   */
#define DEV_IN_VALUE   41u   /* value the host provides to the guest */

typedef struct demo_dev {
    uint32_t in;       /* host-provided input, returned for REG_IN */
    uint32_t seen;     /* last value the guest wrote to REG_OUT */
    int      writes;   /* number of REG_OUT writes observed */
} demo_dev_t;

static uint32_t demo_read(void *ctx, uint32_t offset)
{
    demo_dev_t *d = ctx;

    if (offset == DEV_REG_IN)
        return d->in;
    return 0;
}

static void demo_write(void *ctx, uint32_t offset, uint32_t value)
{
    demo_dev_t *d = ctx;

    if (offset == DEV_REG_OUT) {
        d->seen = value;
        d->writes++;
    }
}

/* --- Harness -------------------------------------------------------------- */

/* Step the CPU until crt0 parks at `j .` (PC stops changing) or a safety cap.
 * Returns the number of steps taken. Extracted so the determinism check below
 * can replay the exact same run after a reset. */
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

int main(int argc, char **argv)
{
    static arcade1_bus_t bus;          /* Static: the bus embeds 8 KiB. */
    static arcade1_cpu_t cpu;
    static uint8_t image[ARCADE1_ROM_SIZE];
    static demo_dev_t dev;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <image.bin>\n", argv[0]);
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    assert(f && "cannot open guest image");
    size_t n = fread(image, 1, sizeof image, f);
    fclose(f);
    assert(n > 0 && n <= sizeof image && "guest image must fit ROM");

    dev.in = DEV_IN_VALUE;   /* Host preloads the input register. */

    arcade1_bus_init(&bus);
    arcade1_set_mmio(&bus, demo_read, demo_write, &dev);   /* attach device */
    assert(arcade1_load_rom(&bus, image, n) == n &&
           "guest image must load fully into ROM");
    arcade1_cpu_reset(&cpu);

    /* Run until crt0 parks at `j .` (PC stops changing) or a safety cap. */
    const int MAX_STEPS = 200000;
    int steps = run_to_park(&cpu, &bus, MAX_STEPS);

    printf("  image: %zu bytes, ran %d steps, stopped at PC=0x%08X\n",
           n, steps, cpu.pc);
    printf("  device: REG_IN=%u -> guest wrote REG_OUT=%u (writes=%d)\n",
           DEV_IN_VALUE, dev.seen, dev.writes);

    assert(steps > 0 && "CPU must have executed instructions");
    assert(steps < MAX_STEPS && "program must park (not run to the cap)");
    assert(dev.writes == 1 && "guest must write REG_OUT exactly once");
    assert(dev.seen == DEV_IN_VALUE + 1 &&
           "guest must read REG_IN and write back value+1");

    /* --- Determinism: reset reproduces the identical MMIO write ----------- */
    /* Snapshot the observed write, then reset (RAM + CPU; ROM and the attached
     * device survive) and replay the same program with the same host input.
     * The result must be identical — MMIO dispatch must be deterministic. */
    const uint32_t snap_seen   = dev.seen;
    const int      snap_writes = dev.writes;

    dev.in     = DEV_IN_VALUE;   /* same host input */
    dev.seen   = 0;
    dev.writes = 0;
    arcade1_bus_reset(&bus);     /* clears RAM; keeps ROM + MMIO handler */
    arcade1_cpu_reset(&cpu);

    int steps2 = run_to_park(&cpu, &bus, MAX_STEPS);
    assert(steps2 == steps && "reset must re-run the program identically");
    assert(dev.writes == snap_writes && dev.seen == snap_seen &&
           "reset + same input must reproduce the identical MMIO write");

    printf("milestone11: all assertions passed\n");
    printf("  verified: guest read + wrote an MMIO device register,\n");
    printf("            deterministic under reset (reset-and-compare)\n");
    return 0;
}
