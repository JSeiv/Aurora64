#include "rom_identity.h"

#include <string.h>

#define ROM_IDENTITY_ACTIVE UINT32_C(0x524f4d41)
#define ROM_IDENTITY_DONE UINT32_C(0x524f4d44)
#define ROM_IDENTITY_FAILED UINT32_C(0x524f4d46)
#define ROM_IDENTITY_SCRATCH_SIZE 256U

static bool valid_order(rom_byte_order_t order)
{
    return order == ROM_BYTE_ORDER_Z64 || order == ROM_BYTE_ORDER_V64 ||
           order == ROM_BYTE_ORDER_N64;
}

bool rom_identity_begin(rom_identity_ctx_t *ctx, rom_byte_order_t order)
{
    if (ctx == NULL) return false;
    memset(ctx, 0, sizeof(*ctx));
    if (!valid_order(order) || !sha256_begin(&ctx->sha256)) {
        ctx->lifecycle = ROM_IDENTITY_FAILED;
        return false;
    }
    ctx->order = order;
    ctx->lifecycle = ROM_IDENTITY_ACTIVE;
    return true;
}

bool rom_identity_update(rom_identity_ctx_t *ctx, const uint8_t *bytes, size_t length)
{
    uint8_t scratch[ROM_IDENTITY_SCRATCH_SIZE];
    size_t consumed = 0U;

    if (ctx == NULL || ctx->lifecycle != ROM_IDENTITY_ACTIVE) return false;
    if (length != 0U && bytes == NULL) {
        ctx->lifecycle = ROM_IDENTITY_FAILED;
        return false;
    }
    while (consumed < length) {
        size_t amount = ROM_IDENTITY_SCRATCH_SIZE - ctx->carry_length;
        size_t produced;
        if (amount > length - consumed) amount = length - consumed;
        produced = rom_normalize_bytes(ctx->order, bytes + consumed, amount,
                                       ctx->carry, &ctx->carry_length, scratch,
                                       sizeof(scratch), false);
        if (produced == SIZE_MAX || !sha256_update(&ctx->sha256, scratch, produced)) {
            ctx->lifecycle = ROM_IDENTITY_FAILED;
            return false;
        }
        consumed += amount;
    }
    return true;
}

bool rom_identity_finish(rom_identity_ctx_t *ctx, rom_fingerprint_t *out)
{
    uint8_t scratch[ROM_IDENTITY_SCRATCH_SIZE];
    size_t produced;

    if (out != NULL) memset(out, 0, sizeof(*out));
    if (ctx == NULL || out == NULL || ctx->lifecycle != ROM_IDENTITY_ACTIVE) {
        if (ctx != NULL && ctx->lifecycle == ROM_IDENTITY_ACTIVE) {
            ctx->lifecycle = ROM_IDENTITY_FAILED;
        }
        return false;
    }
    produced = rom_normalize_bytes(ctx->order, NULL, 0U, ctx->carry,
                                   &ctx->carry_length, scratch,
                                   sizeof(scratch), true);
    if (produced == SIZE_MAX || produced != 0U || ctx->carry_length != 0U ||
        !sha256_finish(&ctx->sha256, out->bytes)) {
        memset(out, 0, sizeof(*out));
        ctx->lifecycle = ROM_IDENTITY_FAILED;
        return false;
    }
    ctx->lifecycle = ROM_IDENTITY_DONE;
    return true;
}

bool rom_fingerprint_equal(const rom_fingerprint_t *a, const rom_fingerprint_t *b)
{
    uint8_t difference = 0U;
    size_t index;
    if (a == NULL || b == NULL) return false;
    for (index = 0U; index < sizeof(a->bytes); ++index) {
        difference |= (uint8_t)(a->bytes[index] ^ b->bytes[index]);
    }
    return difference == 0U;
}
