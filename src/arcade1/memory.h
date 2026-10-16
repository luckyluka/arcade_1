/* ARCADE-1 memory subsystem — public interface.
 *
 * A clean memory/bus abstraction the CPU (milestone 08+) goes through for
 * every access:
 *
 *   CPU  ->  arcade1_read32 / arcade1_write32  ->  { ROM, RAM, MMIO }
 *
 * ROM is loaded once (arcade1_load_rom) and is read-only at runtime; RAM is
 * read/write; MMIO (milestone 11) routes to a host-registered device handler,
 * which is how a guest program reaches a device register. The map is still a
 * placeholder and NOT the final ARCADE-1 map, and only the minimal MMIO
 * infrastructure exists so far — no full set of peripherals yet.
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
 * read/write; MMIO routes to a device handler. Addresses are byte addresses;
 * accesses are little-endian. This map is a placeholder and is NOT the final
 * ARCADE-1 memory map.
 *
 *   ROM   0x0000..0x0FFF   4 KiB  program image (read-only)
 *   RAM   0x1000..0x1FFF   4 KiB  data + stack
 *   gap   0x2000..0x3FFF          unused (out of range)
 *   MMIO  0x4000..0x4FFF   4 KiB  device registers (routed to a handler)
 *
 * The gap keeps MMIO a distinct region rather than a continuation of RAM, the
 * way a real map separates memory from memory-mapped I/O. */

#define ARCADE1_ROM_BASE  0x0000u
#define ARCADE1_ROM_SIZE  0x1000u           /* 4 KiB */
#define ARCADE1_RAM_BASE  0x1000u
#define ARCADE1_RAM_SIZE  0x1000u           /* 4 KiB */
#define ARCADE1_MMIO_BASE 0x4000u
#define ARCADE1_MMIO_SIZE 0x1000u           /* 4 KiB device-register window */

/* --- MMIO handlers ------------------------------------------------------- *
 * The bus routes the MMIO region to a host-registered handler so devices can
 * service register reads/writes without memory.c knowing about them. This is
 * the minimal milestone-11 infrastructure: timer (M12), video (M13) and later
 * devices attach by registering a handler that dispatches on the byte offset
 * within the MMIO window. Reads are called with the register's byte offset and
 * return a 32-bit value; no handler means "no device" (reads 0, writes
 * ignored). */

typedef uint32_t (*arcade1_mmio_read_fn)(void *ctx, uint32_t offset);
typedef void     (*arcade1_mmio_write_fn)(void *ctx, uint32_t offset,
                                          uint32_t value);

typedef struct arcade1_bus {
    uint8_t rom[ARCADE1_ROM_SIZE];
    uint8_t ram[ARCADE1_RAM_SIZE];

    /* MMIO device handler (registered after init; cleared by init, kept by
     * reset). NULL = no device attached. */
    arcade1_mmio_read_fn  mmio_read;
    arcade1_mmio_write_fn mmio_write;
    void                 *mmio_ctx;

    /* Number of accesses the bus ignored or answered with a default (the full
     * case list lives with the accesses in memory.c). Diagnostic only — it
     * never affects what a read returns or which writes are accepted. Cleared
     * by arcade1_bus_init() and arcade1_bus_reset(). */
    uint32_t stray_accesses;
} arcade1_bus_t;

/* --- Lifecycle ----------------------------------------------------------- */

/* Zero the whole bus (ROM, RAM, MMIO handler, and the stray counter). Call
 * once before loading a program; re-registers no device — set one with
 * arcade1_set_mmio. */
void arcade1_bus_init(arcade1_bus_t *bus);

/* Reset for a new run: clears RAM and the stray counter, leaves ROM and any
 * MMIO handler intact. */
void arcade1_bus_reset(arcade1_bus_t *bus);

/* --- MMIO device registration -------------------------------------------- *
 * Attach a device handler to the MMIO region (pass NULL to detach). Call after
 * arcade1_bus_init(), which clears any handler. The handler services register
 * accesses by byte offset within the MMIO window. */
void arcade1_set_mmio(arcade1_bus_t *bus,
                      arcade1_mmio_read_fn read_fn,
                      arcade1_mmio_write_fn write_fn,
                      void *ctx);

/* --- ROM loading --------------------------------------------------------- */

/* Copy a program image into ROM. Truncates if the image is larger than ROM,
 * leaves the remainder zeroed. Returns the number of bytes loaded. */
size_t arcade1_load_rom(arcade1_bus_t *bus, const void *data, size_t size);

/* --- Bus accesses (the one clear interface) ------------------------------ *
 * Widths: 8, 16, and 32 bits, little-endian (RV32I loads/stores need all
 * three). A read returns 0 if the address is out of range or the access
 * straddles a region boundary. A write is accepted only when the whole access
 * lands in RAM (ROM and out-of-range writes are ignored). Deterministic;
 * never faults.
 *
 * MMIO: 32-bit accesses (lw/sw) dispatch to the registered device handler.
 * Sub-word (8/16-bit) accesses to the MMIO window are no-ops — reads return 0,
 * writes are ignored — so device registers are always accessed as 32-bit.
 *
 * Every access the bus ignores or answers with a default also bumps the
 * diagnostic bus->stray_accesses counter (see memory.c for the case list);
 * this is why the read entry points are non-const — they record that counter.
 * No returned value or accepted write changes because of it. */

uint8_t  arcade1_read8 (arcade1_bus_t *bus, uint32_t addr);
uint16_t arcade1_read16(arcade1_bus_t *bus, uint32_t addr);
uint32_t arcade1_read32(arcade1_bus_t *bus, uint32_t addr);

void arcade1_write8 (arcade1_bus_t *bus, uint32_t addr, uint8_t value);
void arcade1_write16(arcade1_bus_t *bus, uint32_t addr, uint16_t value);
void arcade1_write32(arcade1_bus_t *bus, uint32_t addr, uint32_t value);

#endif /* ARCADE1_MEMORY_H */
