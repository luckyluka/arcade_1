/* ARCADE-1 memory subsystem — implementation.
 *
 * See memory.h for the interface. All accesses route through a single region
 * resolver so ROM/RAM (and future MMIO) share one decode path. No host
 * dependency; deterministic and fault-free by construction.
 */

#include "memory.h"

#include <string.h>

/* --- Region resolution --------------------------------------------------- */

/* Which region an address belongs to. */
typedef enum {
    REGION_NONE,
    REGION_ROM,
    REGION_RAM,
    REGION_MMIO,
} region_t;

/* Decode an address to a region. Callers pass the access width in bytes so we
 * can reject accesses that straddle a region boundary.
 *
 * Uses the overflow-safe "addr - base <= size - width" idiom: if addr < base,
 * the unsigned subtraction wraps to a large value that fails the test, so no
 * explicit lower-bound comparison (and no "unsigned >= 0" warning) is needed.
 * Callers must pass width <= region size, which holds for 32-bit accesses. */
static region_t resolve(const arcade1_bus_t *bus, uint32_t addr,
                        uint32_t width)
{
    (void)bus;   /* Regions are fixed by the map, not by bus contents. */

    if (addr - ARCADE1_ROM_BASE <= ARCADE1_ROM_SIZE - width)
        return REGION_ROM;

    if (addr - ARCADE1_RAM_BASE <= ARCADE1_RAM_SIZE - width)
        return REGION_RAM;

    if (addr - ARCADE1_MMIO_BASE <= ARCADE1_MMIO_SIZE - width)
        return REGION_MMIO;

    return REGION_NONE;
}

/* --- Lifecycle ----------------------------------------------------------- */

void arcade1_bus_init(arcade1_bus_t *bus)
{
    memset(bus, 0, sizeof(*bus));
}

void arcade1_bus_reset(arcade1_bus_t *bus)
{
    /* RAM is volatile; ROM and the attached MMIO handler persist across runs.
     * The stray counter is per-run diagnostics, so it resets too. */
    memset(bus->ram, 0, sizeof(bus->ram));
    bus->stray_accesses = 0;
}

void arcade1_set_mmio(arcade1_bus_t *bus,
                      arcade1_mmio_read_fn read_fn,
                      arcade1_mmio_write_fn write_fn,
                      void *ctx)
{
    bus->mmio_read  = read_fn;
    bus->mmio_write = write_fn;
    bus->mmio_ctx   = ctx;
}

/* --- ROM loading --------------------------------------------------------- */

size_t arcade1_load_rom(arcade1_bus_t *bus, const void *data, size_t size)
{
    size_t n = size;

    if (n > ARCADE1_ROM_SIZE)
        n = ARCADE1_ROM_SIZE;

    /* Zero first so any tail beyond the image is defined. */
    memset(bus->rom, 0, sizeof(bus->rom));
    if (n && data)
        memcpy(bus->rom, data, n);

    return n;
}

/* --- Bus accesses -------------------------------------------------------- *
 *
 * Stray-access counter (bus->stray_accesses): incremented whenever the bus
 * ignores an access or answers it with a default, i.e. every access it cannot
 * service through a normal region. The complete case list:
 *
 *   1. sub-word (8/16-bit) access to the MMIO window (read -> 0, write ignored)
 *   2. read or write outside ROM, RAM and MMIO — out-of-range or straddling a
 *      region boundary (read -> 0, write ignored)
 *   3. write to ROM (read-only; ignored)
 *   4. 32-bit MMIO access with no device attached (mmio_read / mmio_write are
 *      NULL; read -> 0, write ignored)
 *
 * A 32-bit MMIO access with a handler attached is dispatched to the device and
 * is NOT a stray — the bus did its job. The counter is diagnostic only: it
 * never changes a returned value or whether a write is accepted. */

uint8_t arcade1_read8(arcade1_bus_t *bus, uint32_t addr)
{
    const uint8_t *base;

    switch (resolve(bus, addr, 1)) {
    case REGION_ROM:
        base = bus->rom + (addr - ARCADE1_ROM_BASE);
        break;
    case REGION_RAM:
        base = bus->ram + (addr - ARCADE1_RAM_BASE);
        break;
    case REGION_MMIO:   /* Stray (case 1): sub-word MMIO unsupported -> 0. */
    case REGION_NONE:   /* Stray (case 2): out of range -> 0. */
    default:
        bus->stray_accesses++;
        return 0;
    }
    return base[0];
}

uint16_t arcade1_read16(arcade1_bus_t *bus, uint32_t addr)
{
    const uint8_t *base;

    switch (resolve(bus, addr, 2)) {
    case REGION_ROM:
        base = bus->rom + (addr - ARCADE1_ROM_BASE);
        break;
    case REGION_RAM:
        base = bus->ram + (addr - ARCADE1_RAM_BASE);
        break;
    case REGION_MMIO:   /* Stray (case 1): sub-word MMIO unsupported -> 0. */
    case REGION_NONE:   /* Stray (case 2): out of range -> 0. */
    default:
        bus->stray_accesses++;
        return 0;
    }
    /* Little-endian assemble. */
    return (uint16_t)base[0] | ((uint16_t)base[1] << 8);
}

uint32_t arcade1_read32(arcade1_bus_t *bus, uint32_t addr)
{
    const uint8_t *base;

    switch (resolve(bus, addr, 4)) {
    case REGION_ROM:
        base = bus->rom + (addr - ARCADE1_ROM_BASE);
        break;
    case REGION_RAM:
        base = bus->ram + (addr - ARCADE1_RAM_BASE);
        break;
    case REGION_MMIO:
        /* Dispatch to the registered device; no device (case 4) -> stray 0. */
        if (bus->mmio_read)
            return bus->mmio_read(bus->mmio_ctx, addr - ARCADE1_MMIO_BASE);
        bus->stray_accesses++;
        return 0;
    case REGION_NONE:   /* Stray (case 2): out of range -> 0. */
    default:
        bus->stray_accesses++;
        return 0;
    }

    /* Little-endian assemble. */
    return (uint32_t)base[0] |
           ((uint32_t)base[1] << 8) |
           ((uint32_t)base[2] << 16) |
           ((uint32_t)base[3] << 24);
}

void arcade1_write8(arcade1_bus_t *bus, uint32_t addr, uint8_t value)
{
    /* Only RAM accepts writes: ROM (case 3), a sub-word MMIO write (case 1),
     * and out-of-range writes (case 2) are all ignored — each counts a stray. */
    if (resolve(bus, addr, 1) != REGION_RAM) {
        bus->stray_accesses++;
        return;
    }

    bus->ram[addr - ARCADE1_RAM_BASE] = value;
}

void arcade1_write16(arcade1_bus_t *bus, uint32_t addr, uint16_t value)
{
    uint8_t *base;

    if (resolve(bus, addr, 2) != REGION_RAM) {
        bus->stray_accesses++;   /* Cases 1 (sub-word MMIO), 2, 3. */
        return;
    }

    base = bus->ram + (addr - ARCADE1_RAM_BASE);

    /* Little-endian scatter. */
    base[0] = (uint8_t)(value & 0xFFu);
    base[1] = (uint8_t)(value >> 8);
}

void arcade1_write32(arcade1_bus_t *bus, uint32_t addr, uint32_t value)
{
    const region_t region = resolve(bus, addr, 4);
    uint8_t *base;

    if (region == REGION_MMIO) {
        /* Dispatch to the registered device; no device (case 4) -> stray. */
        if (bus->mmio_write) {
            bus->mmio_write(bus->mmio_ctx, addr - ARCADE1_MMIO_BASE, value);
            return;
        }
        bus->stray_accesses++;
        return;
    }

    /* ROM is read-only (case 3) and only RAM accepts writes; out-of-range
     * writes (case 2) are ignored too — each counts a stray. */
    if (region != REGION_RAM) {
        bus->stray_accesses++;
        return;
    }

    base = bus->ram + (addr - ARCADE1_RAM_BASE);

    /* Little-endian scatter. */
    base[0] = (uint8_t)(value & 0xFFu);
    base[1] = (uint8_t)((value >> 8) & 0xFFu);
    base[2] = (uint8_t)((value >> 16) & 0xFFu);
    base[3] = (uint8_t)((value >> 24) & 0xFFu);
}
