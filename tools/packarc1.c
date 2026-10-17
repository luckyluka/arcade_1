/* ARCADE-1 host tool — pack a flat guest ROM into a `.arc1` image
 * (milestone 17).
 *
 *   packarc1 input.bin output.arc1
 *
 * Reads the flat RV32IM binary (1..ARCADE1_ROM_SIZE bytes), computes its
 * CRC-32 and writes the 16-byte ARC1 header plus the payload. Shares the
 * header writer and CRC with the VM itself (src/arcade1/rom.c), so the
 * packer and the reader can never disagree about the layout.
 *
 * Host-only: built by the Makefile into build/packarc1, not part of the
 * libretro core. Exits 0 on success, 1 on any error.
 */

#include "rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    /* +1: one byte past the ROM lets us detect an oversized input. */
    static uint8_t buf[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE + 1];
    FILE *in, *out;
    size_t n;
    uint32_t crc;

    if (argc != 3) {
        fprintf(stderr, "usage: packarc1 input.bin output.arc1\n");
        return 1;
    }

    in = fopen(argv[1], "rb");
    if (!in) {
        fprintf(stderr, "packarc1: cannot read %s\n", argv[1]);
        return 1;
    }
    n = fread(buf + ARCADE1_IMAGE_HEADER_SIZE, 1, ARCADE1_ROM_SIZE + 1, in);
    fclose(in);

    /* n == 0 (empty) or n > ROM (would not fit) are both fatal. */
    if (n == 0 || n > ARCADE1_ROM_SIZE) {
        fprintf(stderr, "packarc1: %s is %s (%zu bytes, ROM is %u)\n",
                argv[1], n == 0 ? "empty" : "larger than the ROM",
                n, (unsigned)ARCADE1_ROM_SIZE);
        return 1;
    }

    crc = arcade1_crc32(buf + ARCADE1_IMAGE_HEADER_SIZE, n);
    arcade1_image_header(buf, (uint32_t)n, crc);

    out = fopen(argv[2], "wb");
    if (!out) {
        fprintf(stderr, "packarc1: cannot write %s\n", argv[2]);
        return 1;
    }
    if (fwrite(buf, 1, ARCADE1_IMAGE_HEADER_SIZE + n, out) !=
        ARCADE1_IMAGE_HEADER_SIZE + n) {
        fprintf(stderr, "packarc1: short write on %s\n", argv[2]);
        fclose(out);
        return 1;
    }
    fclose(out);

    printf("packarc1: %s <- %s (%zu bytes payload, crc32 0x%08X)\n",
           argv[2], argv[1], n, crc);
    return 0;
}
