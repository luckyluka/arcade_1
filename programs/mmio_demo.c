/* ARCADE-1 milestone 11 guest program — MMIO demonstration.
 *
 * The point of milestone 11: a guest program can write/read a device register.
 * This program reads a 32-bit value the host placed in one MMIO register
 * (REG_IN) and writes a computed result to another (REG_OUT).
 *
 * The harness (tests/test_mmio.c) registers a device handler on the bus, runs
 * this image, and asserts the handler observed a write of (REG_IN + 1) — which
 * is only possible if the guest both *read* REG_IN and *wrote* REG_OUT through
 * the bus's MMIO region.
 *
 * MMIO registers are 32-bit (lw/sw); see src/arcade1/memory.h for the map.
 */

#define MMIO_BASE 0x4000u
#define REG_IN    (*(volatile unsigned int *)(MMIO_BASE + 0))  /* host -> guest */
#define REG_OUT   (*(volatile unsigned int *)(MMIO_BASE + 4))  /* guest -> host */

int main(void)
{
    unsigned int v = REG_IN;   /* lw from a device register */
    REG_OUT = v + 1;           /* sw back to a device register */
    return 0;
}
