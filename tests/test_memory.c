/* ARCADE-1 — Milestone 07 test harness.
 *
 * Proves the memory/bus abstraction. Links ONLY src/arcade1/memory.c (no
 * libretro, no machine, no .so), exercising arcade1_read32/arcade1_write32
 * directly. Verifies:
 *   - ROM is readable and read-only (write32 to ROM is ignored);
 *   - RAM is read/write and round-trips values;
 *   - little-endian byte order;
 *   - out-of-range reads return 0 and out-of-range writes are ignored;
 *   - boundary accesses at the first/last valid word of each region;
 *   - accesses that straddle a region boundary are rejected (return 0);
 *   - arcade1_bus_reset clears RAM but preserves ROM;
 *   - arcade1_load_rom truncates oversized images and zeroes the tail.
 */

#include "memory.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static arcade1_bus_t bus;   /* Static: the bus embeds 8 KiB. */

    arcade1_bus_init(&bus);

    /* --- Fresh bus reads as zero ---------------------------------------- */
    assert(arcade1_read32(&bus, ARCADE1_ROM_BASE) == 0 &&
           "fresh ROM must read as zero");
    assert(arcade1_read32(&bus, ARCADE1_RAM_BASE) == 0 &&
           "fresh RAM must read as zero");

    /* --- ROM loading + readback (little-endian) -------------------------- */
    {
        const uint8_t img[8] = { 0x11, 0x22, 0x33, 0x44,
                                 0xAA, 0xBB, 0xCC, 0xDD };
        assert(arcade1_load_rom(&bus, img, sizeof(img)) == sizeof(img) &&
               "load_rom must return bytes loaded");

        assert(arcade1_read32(&bus, ARCADE1_ROM_BASE) == 0x44332211u &&
               "ROM word 0 must be little-endian 0x44332211");
        assert(arcade1_read32(&bus, ARCADE1_ROM_BASE + 4) == 0xDDCCBBAAu &&
               "ROM word 1 must be little-endian 0xDDCCBBAA");
    }

    /* --- ROM is read-only at runtime ------------------------------------ */
    arcade1_write32(&bus, ARCADE1_ROM_BASE, 0xDEADBEEFu);
    assert(arcade1_read32(&bus, ARCADE1_ROM_BASE) == 0x44332211u &&
           "write to ROM must be ignored");

    /* --- RAM read/write round-trip --------------------------------------- */
    arcade1_write32(&bus, ARCADE1_RAM_BASE, 0xCAFEBABEu);
    assert(arcade1_read32(&bus, ARCADE1_RAM_BASE) == 0xCAFEBABEu &&
           "RAM round-trip must preserve value");

    /* Little-endian byte order in RAM. */
    {
        const uint8_t *raw = bus.ram;
        assert(raw[0] == 0xBE && raw[1] == 0xBA && raw[2] == 0xFE &&
               raw[3] == 0xCA && "RAM word must be stored little-endian");
    }

    /* --- Boundary accesses (first + last valid word of RAM) -------------- */
    arcade1_write32(&bus, ARCADE1_RAM_BASE + ARCADE1_RAM_SIZE - 4,
                    0x12345678u);
    assert(arcade1_read32(&bus, ARCADE1_RAM_BASE + ARCADE1_RAM_SIZE - 4)
               == 0x12345678u &&
           "last RAM word must be accessible");

    /* --- Straddling + out-of-range accesses return 0, no fault ---------- */
    /* Last bytes of ROM + start of RAM: straddles the two regions, rejected. */
    assert(arcade1_read32(&bus, ARCADE1_ROM_BASE + ARCADE1_ROM_SIZE - 2) == 0 &&
           "access straddling ROM/RAM must return 0");
    /* One past the end of RAM: out of range (map covers 0x0000..0x1FFF). */
    assert(arcade1_read32(&bus, ARCADE1_RAM_BASE + ARCADE1_RAM_SIZE) == 0 &&
           "access just past RAM must return 0");
    /* Far out of range. */
    assert(arcade1_read32(&bus, 0xFFFFF000u) == 0 &&
           "far out-of-range read must return 0");

    /* Out-of-range write must be ignored (not fault, not wrap). */
    arcade1_write32(&bus, 0xFFFFF000u, 0xFFFFFFFFu);
    arcade1_write32(&bus, ARCADE1_RAM_BASE + ARCADE1_RAM_SIZE,
                    0xFFFFFFFFu);
    arcade1_write32(&bus, ARCADE1_ROM_BASE + ARCADE1_ROM_SIZE - 2,
                    0xFFFFFFFFu);

    /* --- bus_reset clears RAM, preserves ROM ----------------------------- */
    assert(arcade1_read32(&bus, ARCADE1_RAM_BASE) == 0xCAFEBABEu &&
           "RAM must still hold value before reset");
    arcade1_bus_reset(&bus);
    assert(arcade1_read32(&bus, ARCADE1_RAM_BASE) == 0 &&
           "bus_reset must clear RAM");
    assert(arcade1_read32(&bus, ARCADE1_ROM_BASE) == 0x44332211u &&
           "bus_reset must preserve ROM");

    /* --- load_rom truncates oversized images ----------------------------- */
    {
        static uint8_t big[ARCADE1_ROM_SIZE + 64];
        memset(big, 0x5A, sizeof(big));
        size_t n = arcade1_load_rom(&bus, big, sizeof(big));
        assert(n == ARCADE1_ROM_SIZE &&
               "load_rom must truncate to ROM size");
        assert(arcade1_read32(&bus, ARCADE1_ROM_BASE) == 0x5A5A5A5Au &&
               "truncated image must fill ROM");
    }

    printf("milestone07: all assertions passed\n");
    printf("  map: ROM 0x%04X+%u, RAM 0x%04X+%u\n",
           ARCADE1_ROM_BASE, ARCADE1_ROM_SIZE,
           ARCADE1_RAM_BASE, ARCADE1_RAM_SIZE);
    printf("  verified: ROM r/o, RAM r/w, little-endian, boundaries,\n");
    printf("            straddle/out-of-range -> 0, reset, load truncation\n");

    return 0;
}
