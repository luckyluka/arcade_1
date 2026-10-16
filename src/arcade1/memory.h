/* ARCADE-1 memory subsystem — public interface.
 *
 * A clean memory/bus abstraction the CPU (milestone 08+) will go through for
 * every access:
 *
 *   CPU  ->  arcade1_read32 / arcade1_write32  ->  { ROM, RAM, future MMIO }
 *
 * This is deliberately minimal: one ROM region and one RAM region. There is no
 * large memory map and no peripherals yet — those are later decisions. The
 * machine owns one bus; the CPU will be given a pointer to it.
 *
 * No dependency on libretro or any host frontend.
 */

#ifndef ARCADE1_MEMORY_H
#define ARCADE1_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- Minimal memory map -------------------------------------------------- *
 * ROM is loaded once (arcade1_load_rom) and is read-only at runtime; RAM is
 * read/write. Addresses are byte addresses; accesses are 32-bit little-endian.
 * This map is a placeholder and is NOT the final ARCADE-1 memory map. */

#define ARCADE1_ROM_BASE  0x0000u
#define ARCADE1_ROM_SIZE  0x1000u           /* 4 KiB */
#define ARCADE1_RAM_BASE  0x1000u
#define ARCADE1_RAM_SIZE  0x1000u           /* 4 KiB */

typedef struct arcade1_bus {
    uint8_t rom[ARCADE1_ROM_SIZE];
    uint8_t ram[ARCADE1_RAM_SIZE];
} arcade1_bus_t;

/* --- Lifecycle ----------------------------------------------------------- */

/* Zero the whole bus (ROM and RAM). Call once before loading a program. */
void arcade1_bus_init(arcade1_bus_t *bus);

/* Reset for a new run: clears RAM, leaves ROM intact. */
void arcade1_bus_reset(arcade1_bus_t *bus);

/* --- ROM loading --------------------------------------------------------- */

/* Copy a program image into ROM. Truncates if the image is larger than ROM,
 * leaves the remainder zeroed. Returns the number of bytes loaded. */
size_t arcade1_load_rom(arcade1_bus_t *bus, const void *data, size_t size);

/* --- Bus accesses (the one clear interface) ------------------------------ *
 * Widths: 8, 16, and 32 bits, little-endian (RV32I loads/stores need all
 * three). A read returns 0 if the address is out of range or the access
 * straddles a region boundary. A write is accepted only when the whole access
 * lands in RAM (ROM and out-of-range writes are ignored). Deterministic;
 * never faults. */

uint8_t  arcade1_read8 (const arcade1_bus_t *bus, uint32_t addr);
uint16_t arcade1_read16(const arcade1_bus_t *bus, uint32_t addr);
uint32_t arcade1_read32(const arcade1_bus_t *bus, uint32_t addr);

void arcade1_write8 (arcade1_bus_t *bus, uint32_t addr, uint8_t value);
void arcade1_write16(arcade1_bus_t *bus, uint32_t addr, uint16_t value);
void arcade1_write32(arcade1_bus_t *bus, uint32_t addr, uint32_t value);

#endif /* ARCADE1_MEMORY_H */
