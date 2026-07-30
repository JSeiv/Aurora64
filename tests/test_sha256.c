#define TEST_NO_MAIN
#include "acutest.h"
#include "menu/library/sha256.h"

#include <stdint.h>
#include <string.h>

static uint8_t hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return (uint8_t)(value - '0');
    return (uint8_t)(value - 'a' + 10);
}

static void expect_hex(const uint8_t digest[SHA256_DIGEST_SIZE], const char *hex)
{
    size_t index;
    for (index = 0U; index < SHA256_DIGEST_SIZE; ++index) {
        uint8_t expected = (uint8_t)((hex_nibble(hex[index * 2U]) << 4) |
                                     hex_nibble(hex[index * 2U + 1U]));
        TEST_CHECK_(digest[index] == expected, "digest byte %zu", index);
    }
}

static void hash_text(const char *text, uint8_t digest[SHA256_DIGEST_SIZE])
{
    sha256_ctx_t ctx;
    TEST_ASSERT(sha256_begin(&ctx));
    TEST_ASSERT(sha256_update(&ctx, (const uint8_t *)text, strlen(text)));
    TEST_ASSERT(sha256_finish(&ctx, digest));
}

void test_sha256_published_vectors(void)
{
    static const char multi[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t digest[SHA256_DIGEST_SIZE];

    hash_text("", digest);
    expect_hex(digest, "e3b0c44298fc1c149afbf4c8996fb924"
                       "27ae41e4649b934ca495991b7852b855");
    hash_text("abc", digest);
    expect_hex(digest, "ba7816bf8f01cfea414140de5dae2223"
                       "b00361a396177a9cb410ff61f20015ad");
    hash_text(multi, digest);
    expect_hex(digest, "248d6a61d20638b8e5c026930c3e6039"
                       "a33ce45964ff2167f6ecedd419db06c1");
}

void test_sha256_incremental_chunking(void)
{
    static const char message[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t digest[SHA256_DIGEST_SIZE];
    size_t chunk;

    for (chunk = 1U; chunk <= 17U; ++chunk) {
        sha256_ctx_t ctx;
        size_t offset = 0U;
        TEST_ASSERT(sha256_begin(&ctx));
        TEST_ASSERT(sha256_update(&ctx, NULL, 0U));
        while (offset < sizeof(message) - 1U) {
            size_t amount = chunk;
            if (amount > sizeof(message) - 1U - offset) amount = sizeof(message) - 1U - offset;
            TEST_ASSERT(sha256_update(&ctx, (const uint8_t *)message + offset, amount));
            offset += amount;
        }
        TEST_ASSERT(sha256_finish(&ctx, digest));
        expect_hex(digest, "248d6a61d20638b8e5c026930c3e6039"
                           "a33ce45964ff2167f6ecedd419db06c1");
    }
}

void test_sha256_misuse_and_overflow(void)
{
    sha256_ctx_t ctx;
    uint8_t digest[SHA256_DIGEST_SIZE];
    uint8_t zero[SHA256_DIGEST_SIZE] = { 0U };
    uint8_t byte = 0U;

    TEST_CHECK(!sha256_begin(NULL));
    memset(&ctx, 0, sizeof(ctx));
    TEST_CHECK(!sha256_update(&ctx, &byte, 1U));
    TEST_ASSERT(sha256_begin(&ctx));
    TEST_CHECK(!sha256_update(&ctx, NULL, 1U));
    memset(digest, 0xa5, sizeof(digest));
    TEST_CHECK(!sha256_finish(&ctx, NULL));
    TEST_CHECK(!sha256_finish(&ctx, digest));
    TEST_CHECK(memcmp(digest, zero, sizeof(digest)) == 0);

    TEST_ASSERT(sha256_begin(&ctx));
    ctx.total_bytes = UINT64_MAX / UINT64_C(8);
    ctx.block_length = (size_t)(ctx.total_bytes % SHA256_BLOCK_SIZE);
    TEST_CHECK(!sha256_update(&ctx, &byte, 1U));
    memset(digest, 0xa5, sizeof(digest));
    TEST_CHECK(!sha256_finish(&ctx, digest));
    TEST_CHECK(memcmp(digest, zero, sizeof(digest)) == 0);

    TEST_ASSERT(sha256_begin(&ctx));
    TEST_ASSERT(sha256_finish(&ctx, digest));
    memset(digest, 0xa5, sizeof(digest));
    TEST_CHECK(!sha256_finish(&ctx, digest));
    TEST_CHECK(memcmp(digest, zero, sizeof(digest)) == 0);
    TEST_CHECK(!sha256_update(&ctx, &byte, 1U));
}
