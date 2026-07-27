#ifndef SHA256_H__
#define SHA256_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHA256_BLOCK_SIZE 64U
#define SHA256_DIGEST_SIZE 32U

/*
 * Original portable implementation derived directly from the SHA-256
 * specification in FIPS PUB 180-4; no third-party source code was adapted.
 * The definition is public so callers can allocate contexts without a heap.
 */
typedef struct {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[SHA256_BLOCK_SIZE];
    size_t block_length;
    uint32_t lifecycle;
} sha256_ctx_t;

bool sha256_begin(sha256_ctx_t *ctx);
bool sha256_update(sha256_ctx_t *ctx, const uint8_t *bytes, size_t length);
/* On failure, a non-null digest is zeroed. A context is finalized at most once. */
bool sha256_finish(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

#endif
