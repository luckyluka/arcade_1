/* ARCADE-1 — test-only state fingerprint (housekeeping task 3).
 *
 * Header-only FNV-1a 64-bit hash over the machine state that determinism
 * tests care about: the CPU registers, the PC, the instruction counter, all
 * RAM bytes, and the bus stray-access counter.
 *
 * Integers are hashed byte-by-byte in little-endian order so the result does
 * not depend on the host's byte order; RAM is hashed in address order (bytes
 * are already host-neutral).
 *
 * Test helper only — deliberately NOT promoted to a machine API in src/;
 * that is a roadmap decision for later.
 *
 * Usage: include from a test, then compare two runs:
 *
 *     uint64_t h1 = state_hash(&cpu, &bus);
 *     ... reset, replay ...
 *     uint64_t h2 = state_hash(&cpu, &bus);
 *     assert(h1 == h2);
 */

#ifndef ARCADE1_TEST_STATE_HASH_H
#define ARCADE1_TEST_STATE_HASH_H

#include "exec.h"   /* arcade1_cpu_t; memory.h via exec.h -> arcade1_bus_t */

#include <stddef.h>
#include <stdint.h>

/* FNV-1a 64-bit parameters. */
#define SH_FNV_OFFSET UINT64_C(14695981039346656037)
#define SH_FNV_PRIME  UINT64_C(1099511628211)

/* Mix one byte into the running hash. */
static inline uint64_t sh_byte(uint64_t h, uint8_t b)
{
    return (h ^ (uint64_t)b) * SH_FNV_PRIME;
}

/* Mix a 32-bit integer in little-endian byte order (host-independent). */
static inline uint64_t sh_u32(uint64_t h, uint32_t v)
{
    h = sh_byte(h, (uint8_t)(v & 0xFFu));
    h = sh_byte(h, (uint8_t)((v >> 8) & 0xFFu));
    h = sh_byte(h, (uint8_t)((v >> 16) & 0xFFu));
    h = sh_byte(h, (uint8_t)((v >> 24) & 0xFFu));
    return h;
}

/* Mix a 64-bit integer in little-endian byte order (host-independent). */
static inline uint64_t sh_u64(uint64_t h, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        h = sh_byte(h, (uint8_t)((v >> (8 * i)) & 0xFFu));
    return h;
}

/* Fingerprint of the CPU + RAM + stray-counter state. Returns an FNV-1a
 * 64-bit hash; identical machine state yields an identical hash. */
static inline uint64_t state_hash(const arcade1_cpu_t *cpu,
                                  const arcade1_bus_t *bus)
{
    uint64_t h = SH_FNV_OFFSET;

    for (int i = 0; i < 32; i++)
        h = sh_u32(h, cpu->regs[i]);
    h = sh_u32(h, cpu->pc);
    h = sh_u64(h, cpu->instret);
    for (size_t i = 0; i < ARCADE1_RAM_SIZE; i++)
        h = sh_byte(h, bus->ram[i]);
    h = sh_u32(h, bus->stray_accesses);

    return h;
}

#endif /* ARCADE1_TEST_STATE_HASH_H */
