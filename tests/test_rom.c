/* ARCADE-1 — Milestone 17 test harness.
 *
 * Unit tests for the `.arc1` game image format. Links rom.c + memory.c
 * only (no libretro, no machine, no .so), proving the format code is
 * frontend-independent. Images are hand-built byte by byte so every
 * expected outcome — valid and invalid — is known exactly.
 *
 * Covers what the roadmap requires: a real, minimal image format with a
 * header, a payload and a checksum, validated before the VM ever sees it.
 */

#include "rom.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A small RV32IM payload: the counter program's first instructions will
 * do; the test only cares about bytes surviving the round trip. */
static const uint8_t payload[] = {
    0x13, 0x05, 0x00, 0xC0,   /* ADDI x10, x0, -1024-ish (arbitrary) */
    0x33, 0x00, 0xA5, 0x00,   /* ADD  x1, x10, x10                   */
    0x6F, 0x00, 0x00, 0x00,   /* JAL  x0, 0 (park)                   */
};

/* Build a valid image around `pl`. `declared` overrides the size field,
 * `crc` overrides the checksum field (0 = compute the real one). */
static size_t build_image(uint8_t *out, const uint8_t *pl, size_t pl_size,
                          uint32_t declared, uint32_t crc, uint8_t version)
{
    arcade1_image_header(out, (uint32_t)pl_size, arcade1_crc32(pl, pl_size));
    out[4] = version;
    if (declared != 0) {
        out[8]  = (uint8_t)(declared & 0xFFu);
        out[9]  = (uint8_t)((declared >> 8) & 0xFFu);
        out[10] = (uint8_t)((declared >> 16) & 0xFFu);
        out[11] = (uint8_t)((declared >> 24) & 0xFFu);
    }
    if (crc != 0) {
        out[12] = (uint8_t)(crc & 0xFFu);
        out[13] = (uint8_t)((crc >> 8) & 0xFFu);
        out[14] = (uint8_t)((crc >> 16) & 0xFFu);
        out[15] = (uint8_t)((crc >> 24) & 0xFFu);
    }
    memcpy(out + ARCADE1_IMAGE_HEADER_SIZE, pl, pl_size);
    return ARCADE1_IMAGE_HEADER_SIZE + pl_size;
}

int main(void)
{
    uint8_t buf[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
    arcade1_image_t img;
    arcade1_bus_t bus;
    size_t n;

    /* === CRC-32 known vectors =========================================== */
    assert(arcade1_crc32("123456789", 9) == 0xCBF43926u &&
           "CRC-32/IEEE test vector must match");
    assert(arcade1_crc32("", 0) == 0u && "CRC of nothing is 0");
    assert(arcade1_crc32(payload, sizeof payload) ==
               arcade1_crc32(payload, sizeof payload) &&
           "CRC must be deterministic");

    /* === Magic detection ================================================ */
    n = build_image(buf, payload, sizeof payload, 0, 0, ARCADE1_IMAGE_VERSION);
    assert(arcade1_image_recognized(buf, n) && "magic must be recognized");
    assert(!arcade1_image_recognized(buf, 3) && "short buffer: no magic");
    assert(!arcade1_image_recognized(NULL, 16) && "NULL: no magic");
    /* A raw RV32IM image starts with an instruction word, never "ARC1". */
    {
        static const uint8_t raw[4] = { 0x13, 0x05, 0x00, 0xC0 };
        assert(!arcade1_image_recognized(raw, sizeof raw) &&
               "raw flat ROM must not be mistaken for an image");
    }

    /* === Valid image: parse, then load its payload into the bus ======== */
    assert(arcade1_image_parse(buf, n, &img) == ARCADE1_IMAGE_OK &&
           "valid image must parse");
    assert(img.payload == buf + ARCADE1_IMAGE_HEADER_SIZE &&
           "payload must point at byte 16 of the buffer");
    assert(img.payload_size == sizeof payload && "payload size must match");
    arcade1_bus_init(&bus);
    assert(arcade1_load_rom(&bus, img.payload, img.payload_size) ==
               img.payload_size);
    assert(memcmp(bus.rom, payload, sizeof payload) == 0 &&
           "payload bytes must land in the ROM untouched");

    /* === Rejections ===================================================== */
    assert(arcade1_image_parse(buf, ARCADE1_IMAGE_HEADER_SIZE - 1, &img) ==
               ARCADE1_IMAGE_ERR_SHORT && "15 bytes cannot hold a header");
    assert(arcade1_image_parse(NULL, n, &img) == ARCADE1_IMAGE_ERR_SHORT &&
           "NULL input is a short image");
    {
        uint8_t bad[sizeof buf];
        size_t bn;

        bn = build_image(bad, payload, sizeof payload, 0, 0,
                         ARCADE1_IMAGE_VERSION);
        bad[1] = 'X';                       /* corrupt the magic */
        assert(arcade1_image_parse(bad, bn, &img) == ARCADE1_IMAGE_ERR_MAGIC);

        bn = build_image(bad, payload, sizeof payload, 0, 0, 2 /* future */);
        assert(arcade1_image_parse(bad, bn, &img) ==
                   ARCADE1_IMAGE_ERR_VERSION &&
               "unknown version must be rejected");

        bn = build_image(bad, payload, sizeof payload, 0, 0,
                         ARCADE1_IMAGE_VERSION);
        bad[8] = 0; bad[9] = 0; bad[10] = 0; bad[11] = 0;  /* size = 0 */
        assert(arcade1_image_parse(bad, bn, &img) == ARCADE1_IMAGE_ERR_SIZE &&
               "zero payload is invalid");

        bn = build_image(bad, payload, sizeof payload, 0, 0,
                         ARCADE1_IMAGE_VERSION);
        bad[8] = 8; bad[9] = 0; bad[10] = 0; bad[11] = 0;  /* 8 != 12 */
        assert(arcade1_image_parse(bad, bn, &img) == ARCADE1_IMAGE_ERR_SIZE &&
               "declared size must equal file length - 16");

        bn = build_image(bad, payload, sizeof payload, 4097, 0,
                         ARCADE1_IMAGE_VERSION);           /* > the ROM */
        assert(arcade1_image_parse(bad, bn, &img) == ARCADE1_IMAGE_ERR_SIZE &&
               "payload larger than the ROM is invalid");

        bn = build_image(bad, payload, sizeof payload, 0, 0,
                         ARCADE1_IMAGE_VERSION);
        bad[ARCADE1_IMAGE_HEADER_SIZE + 3] ^= 0xFFu;  /* flip a payload bit */
        assert(arcade1_image_parse(bad, bn, &img) == ARCADE1_IMAGE_ERR_CRC &&
               "a corrupted payload must fail the checksum");
    }

    /* === A full-size (4 KiB) payload is legal =========================== */
    {
        static uint8_t zeros[ARCADE1_ROM_SIZE];
        static uint8_t big[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
        size_t bn = build_image(big, zeros, ARCADE1_ROM_SIZE, 0, 0,
                                ARCADE1_IMAGE_VERSION);
        assert(arcade1_image_parse(big, bn, &img) == ARCADE1_IMAGE_OK &&
               "a full 4 KiB payload must be accepted");
        assert(img.payload_size == ARCADE1_ROM_SIZE);
    }

    /* === Errors explain themselves ====================================== */
    for (int e = ARCADE1_IMAGE_OK; e <= ARCADE1_IMAGE_ERR_CRC; e++)
        assert(arcade1_image_strerror((arcade1_image_err_t)e) != NULL &&
               "every error code must have a message");

    printf("milestone17: all assertions passed\n");
    printf("  verified: CRC-32 vectors, magic detection, header round-trip,\n");
    printf("            payload -> ROM load, short/magic/version/size/crc\n");
    printf("            rejections, 4 KiB payload, error strings\n");

    return 0;
}
