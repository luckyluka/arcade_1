/* ARCADE-1 game image format — `.arc1` (milestone 17) implementation.
 *
 * See rom.h for the pinned layout. Byte-wise little-endian accessors keep
 * the format host-endianness-independent; CRC-32 is a straight bitwise
 * implementation (images are <= 4 KiB, speed is irrelevant, clarity is not).
 */

#include "rom.h"

#include <string.h>

/* --- Little-endian scalars (byte-wise, host-endian-independent) ---------- */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* --- CRC-32 (IEEE 802.3, reflected) -------------------------------------- */

uint32_t arcade1_crc32(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < size; i++) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

/* --- Header --------------------------------------------------------------- */

void arcade1_image_header(uint8_t out[ARCADE1_IMAGE_HEADER_SIZE],
                          uint32_t payload_size, uint32_t payload_crc32)
{
    memcpy(out, ARCADE1_IMAGE_MAGIC, ARCADE1_IMAGE_MAGIC_SIZE);
    out[4] = (uint8_t)ARCADE1_IMAGE_VERSION;
    out[5] = 0;                     /* reserved */
    out[6] = 0;                     /* reserved */
    out[7] = 0;                     /* reserved */
    wr32(out + 8, payload_size);
    wr32(out + 12, payload_crc32);
}

/* --- Parsing -------------------------------------------------------------- */

int arcade1_image_recognized(const void *data, size_t size)
{
    return data && size >= ARCADE1_IMAGE_MAGIC_SIZE &&
           memcmp(data, ARCADE1_IMAGE_MAGIC, ARCADE1_IMAGE_MAGIC_SIZE) == 0;
}

arcade1_image_err_t arcade1_image_parse(const void *data, size_t size,
                                        arcade1_image_t *out)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t declared, crc;

    if (!data || size < ARCADE1_IMAGE_HEADER_SIZE)
        return ARCADE1_IMAGE_ERR_SHORT;
    if (memcmp(p, ARCADE1_IMAGE_MAGIC, ARCADE1_IMAGE_MAGIC_SIZE) != 0)
        return ARCADE1_IMAGE_ERR_MAGIC;
    if (p[4] != (uint8_t)ARCADE1_IMAGE_VERSION)
        return ARCADE1_IMAGE_ERR_VERSION;

    declared = rd32(p + 8);
    if (declared == 0 || declared > ARCADE1_ROM_SIZE)
        return ARCADE1_IMAGE_ERR_SIZE;
    if ((uint64_t)size != (uint64_t)ARCADE1_IMAGE_HEADER_SIZE + declared)
        return ARCADE1_IMAGE_ERR_SIZE;

    crc = rd32(p + 12);
    if (arcade1_crc32(p + ARCADE1_IMAGE_HEADER_SIZE, declared) != crc)
        return ARCADE1_IMAGE_ERR_CRC;

    out->payload = p + ARCADE1_IMAGE_HEADER_SIZE;
    out->payload_size = declared;
    return ARCADE1_IMAGE_OK;
}

const char *arcade1_image_strerror(arcade1_image_err_t err)
{
    switch (err) {
    case ARCADE1_IMAGE_OK:          return "ok";
    case ARCADE1_IMAGE_ERR_SHORT:   return "image too short for a header";
    case ARCADE1_IMAGE_ERR_MAGIC:   return "bad magic (not an ARC1 image)";
    case ARCADE1_IMAGE_ERR_VERSION: return "unsupported format version";
    case ARCADE1_IMAGE_ERR_SIZE:    return "payload size invalid or mismatched";
    case ARCADE1_IMAGE_ERR_CRC:     return "payload checksum mismatch";
    }
    return "unknown error";
}
