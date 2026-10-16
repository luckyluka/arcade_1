/* ARCADE-1 game image format — `.arc1` (milestone 17).
 *
 * A real, minimal ROM container for ARCADE-1 games. Layout, pinned in
 * docs.md section 21:
 *
 *   offset  size  field
 *   0       4     magic   "ARC1"
 *   4       1     version (currently 1)
 *   5       1     reserved, must be 0
 *   6       2     reserved, must be 0
 *   8       4     payload_size    (little-endian u32, 1..4096)
 *   12      4     payload_crc32   (little-endian u32, IEEE CRC-32)
 *   16      N     payload         (the flat RV32IM ROM image)
 *
 * Deliberately NOT included (do not over-design): filesystem, metadata
 * text, asset sections, compression, signatures. The reserved bytes and
 * the version byte are the only extension room; a future incompatible
 * layout bumps the version.
 *
 * Pure and stateless: no bus, no machine, no host dependency beyond
 * memcpy/memcmp. Independently testable (tests/test_rom.c); the host
 * packer (tools/packarc1.c) shares this code so the writer and the
 * reader can never disagree on the layout.
 */

#ifndef ARCADE1_ROM_H
#define ARCADE1_ROM_H

#include <stddef.h>
#include <stdint.h>

#include "memory.h"     /* ARCADE1_ROM_SIZE: the payload limit */

#define ARCADE1_IMAGE_MAGIC        "ARC1"
#define ARCADE1_IMAGE_MAGIC_SIZE   4u
#define ARCADE1_IMAGE_VERSION      1u
#define ARCADE1_IMAGE_HEADER_SIZE  16u

/* Parsed image: points INTO the caller's buffer (no copy, no allocation). */
typedef enum arcade1_image_err {
    ARCADE1_IMAGE_OK = 0,
    ARCADE1_IMAGE_ERR_SHORT,    /* fewer bytes than a header            */
    ARCADE1_IMAGE_ERR_MAGIC,    /* not "ARC1"                           */
    ARCADE1_IMAGE_ERR_VERSION,  /* unknown format version               */
    ARCADE1_IMAGE_ERR_SIZE,     /* declared size 0, > ROM, or disagrees
                                   with the actual file length          */
    ARCADE1_IMAGE_ERR_CRC       /* payload does not match its checksum  */
} arcade1_image_err_t;

typedef struct arcade1_image {
    const uint8_t *payload;      /* -> byte 16 of the input buffer      */
    uint32_t       payload_size; /* == header's payload_size            */
} arcade1_image_t;

/* 1 if the buffer starts with the ARC1 magic (size >= 4), else 0.
 * Lets callers (the libretro adapter) choose between image and raw. */
int arcade1_image_recognized(const void *data, size_t size);

/* Validate a whole .arc1 image. On ARCADE1_IMAGE_OK, *out points into
 * `data`; on error *out is untouched. */
arcade1_image_err_t arcade1_image_parse(const void *data, size_t size,
                                        arcade1_image_t *out);

/* Human-readable error name, for logs and tests (never NULL). */
const char *arcade1_image_strerror(arcade1_image_err_t err);

/* IEEE CRC-32 (poly 0xEDB88320, reflected), the checksum the format
 * uses. Known vector: crc32("123456789") == 0xCBF43926. */
uint32_t arcade1_crc32(const void *data, size_t size);

/* Write a 16-byte header (used by the host packer and by tests). */
void arcade1_image_header(uint8_t out[ARCADE1_IMAGE_HEADER_SIZE],
                          uint32_t payload_size, uint32_t payload_crc32);

#endif /* ARCADE1_ROM_H */
