/* sha256.h -- bundled SHA-256 (M4 contract: no external libraries).
 *
 * FIPS 180-4, one-shot. Used for the frame RGBA hashes the golden JSON
 * files pin (arcforge_ref.frame_hashes) -- the bytes are compared, never
 * a file, so this only has to be correct, not fast.
 */
#ifndef ARCFORGE_SHA256_H
#define ARCFORGE_SHA256_H

#include <stddef.h>
#include <stdint.h>

/* out[32] = SHA-256(data[0..len)) */
void af_sha256(const void *data, size_t len, uint8_t out[32]);

/* out[65] = lowercase hex of af_sha256 (64 chars + NUL) */
void af_sha256_hex(const void *data, size_t len, char out[65]);

#endif /* ARCFORGE_SHA256_H */
