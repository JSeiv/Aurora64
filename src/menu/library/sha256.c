#include "sha256.h"

#include <limits.h>
#include <string.h>

/*
 * Original implementation written from the algorithms and constants specified
 * by FIPS PUB 180-4, Secure Hash Standard (August 2015). No third-party SHA-256
 * implementation was copied or adapted.
 */

#define SHA256_ACTIVE UINT32_C(0x53484132)
#define SHA256_DONE UINT32_C(0x53484144)
#define SHA256_FAILED UINT32_C(0x53484146)

static const uint32_t round_constants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
};

static uint32_t rotate_right(uint32_t value, unsigned int amount)
{
    return (value >> amount) | (value << (32U - amount));
}

static uint32_t read_u32_be(const uint8_t bytes[4])
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static void transform(sha256_ctx_t *ctx, const uint8_t block[SHA256_BLOCK_SIZE])
{
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    size_t index;

    for (index = 0U; index < 16U; ++index) words[index] = read_u32_be(block + index * 4U);
    for (index = 16U; index < 64U; ++index) {
        uint32_t s0 = rotate_right(words[index - 15U], 7U) ^
                      rotate_right(words[index - 15U], 18U) ^
                      (words[index - 15U] >> 3U);
        uint32_t s1 = rotate_right(words[index - 2U], 17U) ^
                      rotate_right(words[index - 2U], 19U) ^
                      (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (index = 0U; index < 64U; ++index) {
        uint32_t sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temporary1 = h + sum1 + choose + round_constants[index] + words[index];
        uint32_t sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temporary1;
        d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

bool sha256_begin(sha256_ctx_t *ctx)
{
    static const uint32_t initial[8] = {
        UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85), UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
        UINT32_C(0x510e527f), UINT32_C(0x9b05688c), UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)
    };
    if (ctx == NULL) return false;
    memset(ctx, 0, sizeof(*ctx));
    memcpy(ctx->state, initial, sizeof(initial));
    ctx->lifecycle = SHA256_ACTIVE;
    return true;
}

bool sha256_update(sha256_ctx_t *ctx, const uint8_t *bytes, size_t length)
{
    size_t consumed = 0U;
    if (ctx == NULL || ctx->lifecycle != SHA256_ACTIVE) return false;
    if (length != 0U && bytes == NULL) {
        ctx->lifecycle = SHA256_FAILED;
        return false;
    }
    if ((uint64_t)length > UINT64_MAX - ctx->total_bytes ||
        (uint64_t)length > UINT64_MAX / UINT64_C(8) - ctx->total_bytes) {
        ctx->lifecycle = SHA256_FAILED;
        return false;
    }
    ctx->total_bytes += (uint64_t)length;
    while (consumed < length) {
        size_t amount = SHA256_BLOCK_SIZE - ctx->block_length;
        if (amount > length - consumed) amount = length - consumed;
        memcpy(ctx->block + ctx->block_length, bytes + consumed, amount);
        ctx->block_length += amount;
        consumed += amount;
        if (ctx->block_length == SHA256_BLOCK_SIZE) {
            transform(ctx, ctx->block);
            ctx->block_length = 0U;
        }
    }
    return true;
}

bool sha256_finish(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE])
{
    uint64_t bit_length;
    size_t index;
    if (digest != NULL) memset(digest, 0, SHA256_DIGEST_SIZE);
    if (ctx == NULL || digest == NULL || ctx->lifecycle != SHA256_ACTIVE) {
        if (ctx != NULL && ctx->lifecycle == SHA256_ACTIVE) ctx->lifecycle = SHA256_FAILED;
        return false;
    }
    bit_length = ctx->total_bytes * UINT64_C(8);
    ctx->block[ctx->block_length++] = 0x80U;
    if (ctx->block_length > 56U) {
        memset(ctx->block + ctx->block_length, 0, SHA256_BLOCK_SIZE - ctx->block_length);
        transform(ctx, ctx->block);
        ctx->block_length = 0U;
    }
    memset(ctx->block + ctx->block_length, 0, 56U - ctx->block_length);
    for (index = 0U; index < 8U; ++index) {
        ctx->block[63U - index] = (uint8_t)(bit_length >> (index * 8U));
    }
    transform(ctx, ctx->block);
    for (index = 0U; index < 8U; ++index) {
        digest[index * 4U] = (uint8_t)(ctx->state[index] >> 24);
        digest[index * 4U + 1U] = (uint8_t)(ctx->state[index] >> 16);
        digest[index * 4U + 2U] = (uint8_t)(ctx->state[index] >> 8);
        digest[index * 4U + 3U] = (uint8_t)ctx->state[index];
    }
    memset(ctx->block, 0, sizeof(ctx->block));
    ctx->block_length = 0U;
    ctx->lifecycle = SHA256_DONE;
    return true;
}
