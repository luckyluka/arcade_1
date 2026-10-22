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

/* Freestanding C runtime minimum: the three functions GCC emits on its own
 * behalf. Assigning a struct, initialising an array or zeroing an aggregate
 * compiles to a memcpy/memset call, so without these the build dies at link
 * with "undefined reference to memcpy" — asteroid_run hit exactly that (all
 * its per-slot spin states were hand-listed to dodge the struct copy).
 *
 * Each is a straight byte loop: correctness over speed. Guest copies are
 * small struct-sized things, and large fills are the machine's job
 * (arcade1_clear_fb), not the guest's.
 *
 * They live in .arcade1.text, which link.ld appends AFTER the guest's own
 * code and rodata, so adding them here cannot move a single address in an
 * already-built guest: every committed golden stays reproducible. */
    .section .arcade1.text, "ax", @progbits
    /* 4-byte alignment: a function's address must be a multiple of 4, or the
     * RISC-V call relocations that reach it come out one byte off and the
     * guest jumps into the rodata instead of the function. The linker
     * script aligns the section too; this is the belt to that pair of
     * braces. (The linker script's ALIGN(4) is the one that matters —
     * the section's own alignment is what decides where it lands.) */
    .balign 4

    .global memcpy
    .type   memcpy, @function
memcpy:
    mv   t0, a0                       /* remember dst: memcpy returns it */
1:  beqz a2, 2f
    lbu  t1, 0(a1)
    sb   t1, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    j    1b
2:  mv   a0, t0
    ret
    .size memcpy, . - memcpy

    .global memset
    .type   memset, @function
memset:
    mv   t0, a0
    andi a1, a1, 0xff                 /* c arrives as an int; store its byte */
1:  beqz a2, 2f
    sb   a1, 0(a0)
    addi a0, a0, 1
    addi a2, a2, -1
    j    1b
2:  mv   a0, t0
    ret
    .size memset, . - memset

    .global memmove
    .type   memmove, @function
memmove:
    mv   t0, a0
    beq  a0, a1, 4f                    /* same pointer: nothing to copy */
    bltu a0, a1, 1f                    /* dst below src: forward is safe */
    add  a1, a1, a2                    /* else copy backward from the ends */
    add  a0, a0, a2
3:  beqz a2, 4f
    addi a1, a1, -1
    addi a0, a0, -1
    lbu  t1, 0(a1)
    sb   t1, 0(a0)
    addi a2, a2, -1
    j    3b
1:  beqz a2, 4f
    lbu  t1, 0(a1)
    sb   t1, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    j    1b
4:  mv   a0, t0
    ret
    .size memmove, . - memmove
