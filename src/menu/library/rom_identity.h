#ifndef ROM_IDENTITY_H__
#define ROM_IDENTITY_H__

#include "rom_header.h"
#include "sha256.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t bytes[SHA256_DIGEST_SIZE]; } rom_fingerprint_t;

/* Fixed-size streaming context: no heap or whole-file allocation is used. */
typedef struct rom_identity_ctx {
    sha256_ctx_t sha256;
    rom_byte_order_t order;
    uint8_t carry[4];
    size_t carry_length;
    uint32_t lifecycle;
} rom_identity_ctx_t;

bool rom_identity_begin(rom_identity_ctx_t *ctx, rom_byte_order_t order);
bool rom_identity_update(rom_identity_ctx_t *ctx, const uint8_t *bytes, size_t length);
/* On failure, a non-null output fingerprint is zeroed. */
bool rom_identity_finish(rom_identity_ctx_t *ctx, rom_fingerprint_t *out);
/* Null fingerprints are never equal, including two null pointers. */
bool rom_fingerprint_equal(const rom_fingerprint_t *a, const rom_fingerprint_t *b);

#endif
