#define TEST_NO_MAIN
#include "acutest.h"
#include "menu/library/rom_identity.h"

#include <stdint.h>
#include <string.h>

#define SYNTHETIC_ROM_SIZE 131072U

static uint8_t canonical_rom[SYNTHETIC_ROM_SIZE];
static uint8_t encoded_rom[SYNTHETIC_ROM_SIZE];
static uint8_t encoded_before[SYNTHETIC_ROM_SIZE];
static const uint8_t oracle[32] = {
    0xf6U, 0x06U, 0x11U, 0xa9U, 0xc0U, 0x11U, 0x54U, 0xf5U,
    0x70U, 0x74U, 0x87U, 0xd2U, 0x4fU, 0x40U, 0xcfU, 0xd2U,
    0x0aU, 0x62U, 0x2dU, 0xacU, 0x37U, 0x0cU, 0xa1U, 0xc7U,
    0x6eU, 0xd3U, 0x08U, 0xfcU, 0x3dU, 0x4cU, 0x9dU, 0x1fU
};

static void build_canonical(void)
{
    size_t index;
    for (index = 0U; index < SYNTHETIC_ROM_SIZE; ++index) {
        canonical_rom[index] = (uint8_t)((index * 37U + 11U) & 0xffU);
    }
    canonical_rom[0] = 0x80U;
    canonical_rom[1] = 0x37U;
    canonical_rom[2] = 0x12U;
    canonical_rom[3] = 0x40U;
}

/* Independent test encoding; production normalization is not used here. */
static void encode_rom(rom_byte_order_t order)
{
    size_t index;
    if (order == ROM_BYTE_ORDER_Z64) {
        memcpy(encoded_rom, canonical_rom, sizeof(encoded_rom));
    } else if (order == ROM_BYTE_ORDER_V64) {
        for (index = 0U; index < sizeof(encoded_rom); index += 2U) {
            encoded_rom[index] = canonical_rom[index + 1U];
            encoded_rom[index + 1U] = canonical_rom[index];
        }
    } else {
        for (index = 0U; index < sizeof(encoded_rom); index += 4U) {
            encoded_rom[index] = canonical_rom[index + 3U];
            encoded_rom[index + 1U] = canonical_rom[index + 2U];
            encoded_rom[index + 2U] = canonical_rom[index + 1U];
            encoded_rom[index + 3U] = canonical_rom[index];
        }
    }
}

static rom_fingerprint_t fingerprint_chunks(rom_byte_order_t order, size_t chunk)
{
    rom_identity_ctx_t ctx;
    rom_fingerprint_t result;
    size_t offset = 0U;

    TEST_ASSERT(rom_identity_begin(&ctx, order));
    TEST_ASSERT(rom_identity_update(&ctx, NULL, 0U));
    while (offset < sizeof(encoded_rom)) {
        size_t amount = chunk;
        if (amount > sizeof(encoded_rom) - offset) amount = sizeof(encoded_rom) - offset;
        TEST_ASSERT(rom_identity_update(&ctx, encoded_rom + offset, amount));
        offset += amount;
    }
    TEST_ASSERT(rom_identity_finish(&ctx, &result));
    return result;
}

static rom_fingerprint_t fingerprint_named_path(const char *path, rom_byte_order_t order)
{
    (void)path;
    return fingerprint_chunks(order, 4096U);
}

void test_rom_identity_orders_oracle_and_chunks(void)
{
    static const size_t large_chunks[] = { 4095U, 4096U, 65536U };
    rom_fingerprint_t reference;
    rom_byte_order_t order;
    size_t chunk;
    size_t index;

    build_canonical();
    for (order = ROM_BYTE_ORDER_Z64; order <= ROM_BYTE_ORDER_N64; ++order) {
        encode_rom(order);
        memcpy(encoded_before, encoded_rom, sizeof(encoded_before));
        for (chunk = 1U; chunk <= 17U; ++chunk) {
            rom_fingerprint_t actual = fingerprint_chunks(order, chunk);
            TEST_CHECK_(memcmp(actual.bytes, oracle, sizeof(oracle)) == 0,
                        "order %d chunk %zu", (int)order, chunk);
            if (order == ROM_BYTE_ORDER_Z64 && chunk == 1U) reference = actual;
            else TEST_CHECK(rom_fingerprint_equal(&reference, &actual));
        }
        for (index = 0U; index < sizeof(large_chunks) / sizeof(large_chunks[0]); ++index) {
            rom_fingerprint_t actual = fingerprint_chunks(order, large_chunks[index]);
            TEST_CHECK_(memcmp(actual.bytes, oracle, sizeof(oracle)) == 0,
                        "order %d chunk %zu", (int)order, large_chunks[index]);
            TEST_CHECK(rom_fingerprint_equal(&reference, &actual));
        }
        TEST_CHECK(memcmp(encoded_rom, encoded_before, sizeof(encoded_rom)) == 0);
    }
}

void test_rom_identity_names_mutation_and_exactly_once(void)
{
    rom_fingerprint_t first;
    rom_fingerprint_t renamed;
    rom_fingerprint_t mutated;

    build_canonical();
    encode_rom(ROM_BYTE_ORDER_Z64);
    first = fingerprint_named_path("old/game.z64", ROM_BYTE_ORDER_Z64);
    renamed = fingerprint_named_path("renamed/game.z64", ROM_BYTE_ORDER_Z64);
    TEST_CHECK(rom_fingerprint_equal(&first, &renamed));
    TEST_CHECK(memcmp(first.bytes, oracle, sizeof(oracle)) == 0);

    encoded_rom[70001U] ^= 0x01U;
    mutated = fingerprint_chunks(ROM_BYTE_ORDER_Z64, 4096U);
    TEST_CHECK(!rom_fingerprint_equal(&first, &mutated));
}

void test_rom_identity_malformed_tail(void)
{
    rom_identity_ctx_t ctx;
    rom_fingerprint_t output;
    rom_fingerprint_t zero = { { 0U } };
    uint8_t bytes[5] = { 0U, 1U, 2U, 3U, 4U };

    TEST_ASSERT(rom_identity_begin(&ctx, ROM_BYTE_ORDER_V64));
    TEST_ASSERT(rom_identity_update(&ctx, bytes, 1U));
    memset(&output, 0xa5, sizeof(output));
    TEST_CHECK(!rom_identity_finish(&ctx, &output));
    TEST_CHECK(memcmp(&output, &zero, sizeof(output)) == 0);

    TEST_ASSERT(rom_identity_begin(&ctx, ROM_BYTE_ORDER_N64));
    TEST_ASSERT(rom_identity_update(&ctx, bytes, 5U));
    memset(&output, 0xa5, sizeof(output));
    TEST_CHECK(!rom_identity_finish(&ctx, &output));
    TEST_CHECK(memcmp(&output, &zero, sizeof(output)) == 0);
}

void test_rom_identity_invalid_and_lifecycle(void)
{
    rom_identity_ctx_t ctx;
    rom_fingerprint_t output;
    rom_fingerprint_t zero = { { 0U } };
    uint8_t byte = 0U;

    TEST_CHECK(!rom_identity_begin(NULL, ROM_BYTE_ORDER_Z64));
    TEST_CHECK(!rom_identity_begin(&ctx, (rom_byte_order_t)99));
    memset(&ctx, 0, sizeof(ctx));
    TEST_CHECK(!rom_identity_update(&ctx, &byte, 1U));
    TEST_ASSERT(rom_identity_begin(&ctx, ROM_BYTE_ORDER_Z64));
    TEST_CHECK(!rom_identity_update(&ctx, NULL, 1U));
    memset(&output, 0xa5, sizeof(output));
    TEST_CHECK(!rom_identity_finish(&ctx, &output));
    TEST_CHECK(memcmp(&output, &zero, sizeof(output)) == 0);

    TEST_ASSERT(rom_identity_begin(&ctx, ROM_BYTE_ORDER_Z64));
    TEST_CHECK(!rom_identity_finish(&ctx, NULL));
    memset(&output, 0xa5, sizeof(output));
    TEST_CHECK(!rom_identity_finish(&ctx, &output));
    TEST_CHECK(memcmp(&output, &zero, sizeof(output)) == 0);

    TEST_ASSERT(rom_identity_begin(&ctx, ROM_BYTE_ORDER_Z64));
    TEST_ASSERT(rom_identity_finish(&ctx, &output));
    memset(&output, 0xa5, sizeof(output));
    TEST_CHECK(!rom_identity_finish(&ctx, &output));
    TEST_CHECK(memcmp(&output, &zero, sizeof(output)) == 0);
    TEST_CHECK(!rom_identity_update(&ctx, &byte, 1U));
}

void test_rom_fingerprint_equality_policy(void)
{
    rom_fingerprint_t a = { { 0U } };
    rom_fingerprint_t b = { { 0U } };

    TEST_CHECK(rom_fingerprint_equal(&a, &b));
    b.bytes[31] = 1U;
    TEST_CHECK(!rom_fingerprint_equal(&a, &b));
    TEST_CHECK(!rom_fingerprint_equal(NULL, &b));
    TEST_CHECK(!rom_fingerprint_equal(&a, NULL));
    TEST_CHECK(!rom_fingerprint_equal(NULL, NULL));
}
