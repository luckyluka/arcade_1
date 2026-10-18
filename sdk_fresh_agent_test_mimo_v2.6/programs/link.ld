/* ARCADE-1 guest linker script (milestone 10).
 *
 * Memory map (v0.2; matches src/arcade1/memory.h and programs/arcade1_map.h):
 *   ROM 0x00000000..0x0003FFFF  256 KiB  code + read-only data + the stored
 *                                        image of .data
 *   RAM 0x20000000..0x2000FFFF   64 KiB  .data/.bss + stack (grows down from
 *                                        the top, 0x20010000)
 *
 * .text.init (crt0's _start) is forced to ROM base so it sits at 0x0000 —
 * where the CPU resets. .data is *stored* in ROM (LMA) but *lives* in RAM
 * (VMA); crt0 copies it at startup. That keeps the flat objcopy image
 * contiguous ([code][.data image], no ROM->RAM gap) so it fits the ROM.
 */

MEMORY
{
    rom (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    ram (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

ENTRY(_start)

SECTIONS
{
    .text :
    {
        KEEP(*(.text.init))              /* _start must be at 0x0000 */
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > rom

    .data :
    {
        . = ALIGN(4);
        _sdata = .;                      /* VMA: RAM  */
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
        _edata = .;
    } > ram AT > rom                     /* stored in ROM, lives in RAM */

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > ram

    _sidata   = LOADADDR(.data);         /* LMA: ROM */
    _stack_top = ORIGIN(ram) + LENGTH(ram);

    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.riscv.attributes)
    }
}
