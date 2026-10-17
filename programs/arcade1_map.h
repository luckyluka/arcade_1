/* ARCADE-1 guest memory map (v0.2) — the ONE source of truth for guests.
 *
 * Every guest that touches memory or MMIO includes this header (most get it
 * via gfx.h). It mirrors the machine's map in src/arcade1/memory.h, which is
 * pinned in SPEC.md section 6; keep the two in sync.
 *
 *   ROM   0x00000000   256 KiB  program image (read-only, code + .data image)
 *   RAM   0x20000000    64 KiB  .data/.bss + stack (grows down from top)
 *   MMIO  0x40000000     4 KiB  device registers (see the offsets below)
 *
 * The stack top (RAM base + RAM size) is where crt0 parks `sp`; the linker
 * script (link.ld) derives it from the same numbers. Reset vector stays at
 * ROM base 0x00000000, so `_start` still sits at 0x0000.
 */
#ifndef ARCADE1_MAP_H
#define ARCADE1_MAP_H

#define ARCADE1_ROM_BASE  0x00000000u
#define ARCADE1_ROM_SIZE  0x00040000u        /* 256 KiB */
#define ARCADE1_RAM_BASE  0x20000000u
#define ARCADE1_RAM_SIZE  0x00010000u        /* 64 KiB  */
#define ARCADE1_MMIO_BASE 0x40000000u
#define ARCADE1_MMIO_SIZE 0x00001000u        /* 4 KiB device window */

/* Top of RAM — the initial stack pointer (see link.ld / crt0.S). */
#define ARCADE1_STACK_TOP (ARCADE1_RAM_BASE + ARCADE1_RAM_SIZE)  /* 0x20010000 */

#endif /* ARCADE1_MAP_H */
