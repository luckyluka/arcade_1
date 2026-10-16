/* ARCADE-1 guest linker script (milestone 10).
 *
 * Memory map (matches the placeholder map in src/arcade1/memory.h):
 *   ROM 0x0000..0x0FFF   code + read-only data + the stored image of .data
 *   RAM 0x1000..0x1FFF   .data/.bss + stack (grows down from the top)
 *
 * .text.init (crt0's _start) is forced to ROM base so it sits at 0x0000 —
 * where the CPU resets. .data is *stored* in ROM (LMA) but *lives* in RAM
 * (VMA); crt0 copies it at startup. That keeps the flat objcopy image
 * contiguous (no 4 KiB gap) so it fits the ROM.
 */

MEMORY
{
    rom (rx)  : ORIGIN = 0x00000000, LENGTH = 4K
    ram (rwx) : ORIGIN = 0x00001000, LENGTH = 4K
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
