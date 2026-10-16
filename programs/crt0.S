/* ARCADE-1 guest startup (milestone 10).
 *
 * Minimal bare-metal crt0 — just enough to run compiled C, no SDK:
 *   1. set the stack pointer to the top of RAM (linker script),
 *   2. copy .data from ROM (its LMA) into RAM (its VMA),
 *   3. zero .bss,
 *   4. call main(),
 *   5. park in a self-loop (`j .`) so the harness can detect completion by
 *      watching PC stop changing.
 *
 * Runs on the RV32I interpreter only (no M extension, no gp setup: guest code
 * is compiled with -msmall-data-limit=0 so it never needs global_pointer).
 */

    .section .text.init, "ax", @progbits
    .global _start
    .type   _start, @function
_start:
    /* Stack grows down from the top of RAM (set in link.ld). */
    la   sp, _stack_top

    /* Copy .data: ROM source _sidata -> RAM [_sdata, _edata). */
    la   t0, _sidata
    la   t1, _sdata
    la   t2, _edata
1:  bgeu t1, t2, 2f
    lw   t3, 0(t0)
    sw   t3, 0(t1)
    addi t0, t0, 4
    addi t1, t1, 4
    j    1b
2:
    /* Zero .bss: [_sbss, _ebss). */
    la   t0, _sbss
    la   t1, _ebss
3:  bgeu t0, t1, 4f
    sw   zero, 0(t0)
    addi t0, t0, 4
    j    3b
4:
    /* Run the program. */
    call main

    /* main() returned: park here. The harness sees PC stop advancing. */
5:  j    5b

    .size _start, . - _start
