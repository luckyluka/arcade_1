/* ARCADE-1 milestone 10 guest program — the example from docs.md.
 *
 * Compiled with riscv64-unknown-elf-gcc (-march=rv32i) into a flat RV32
 * binary, loaded into the VM's ROM, and executed by the CPU. `counter` is a
 * file-scope uninitialized `volatile`, so it lands in .bss (RAM); the
 * `volatile` keeps the store from being optimised away. The harness checks
 * that it ends up holding 123.
 */

volatile int counter;

int main(void)
{
    counter = 123;
    return 0;
}
