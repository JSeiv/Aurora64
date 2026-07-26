#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "acutest.h"
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "support/test_alloc.h"

#include <stddef.h>
#include <string.h>

void test_rom_header_orders_and_fields(void);
void test_rom_header_regions_and_fallback_boundary(void);
void test_rom_header_bounds_invalid_and_zeroing(void);
void test_rom_header_64dd_ipl_compatibility(void);
void test_rom_header_prefix_normalization(void);
void test_rom_header_streaming_boundaries(void);
void test_rom_header_streaming_capacity(void);
void test_rom_header_streaming_fail_closed(void);

static void test_smoke(void)
{
    TEST_CHECK(1);
}

static void test_fail_after_counts_successful_allocations(void)
{
    void *first;
    void *second;

    test_alloc_reset();
    test_alloc_fail_after(1U);
    first = test_malloc(8U);
    second = test_calloc(2U, 4U);

    TEST_CHECK(first != NULL);
    TEST_CHECK(second == NULL);

    test_free(first);
    test_alloc_reset();
}

static void test_reset_restores_allocation(void)
{
    void *allocation;

    test_alloc_reset();
    test_alloc_fail_after(0U);
    TEST_CHECK(test_malloc(1U) == NULL);

    test_alloc_reset();
    allocation = test_malloc(1U);
    TEST_CHECK(allocation != NULL);
    test_free(allocation);
}

static void test_calloc_zero_initializes_memory(void)
{
    unsigned char *allocation;

    test_alloc_reset();
    allocation = test_calloc(4U, sizeof(*allocation));

    TEST_ASSERT(allocation != NULL);
    TEST_CHECK(allocation[0] == 0U);
    TEST_CHECK(allocation[1] == 0U);
    TEST_CHECK(allocation[2] == 0U);
    TEST_CHECK(allocation[3] == 0U);

    test_free(allocation);
}

static void test_realloc_failure_preserves_original_allocation(void)
{
    char *original;
    char *resized;

    test_alloc_reset();
    original = test_malloc(8U);
    TEST_ASSERT(original != NULL);
    memcpy(original, "Aurora", 7U);

    test_alloc_fail_after(0U);
    resized = test_realloc(original, 16U);

    TEST_CHECK(resized == NULL);
    TEST_CHECK(strcmp(original, "Aurora") == 0);

    test_alloc_reset();
    test_free(original);
}

TEST_LIST = {
    { "smoke", test_smoke },
    { "allocator/fail-after", test_fail_after_counts_successful_allocations },
    { "allocator/reset", test_reset_restores_allocation },
    { "allocator/calloc", test_calloc_zero_initializes_memory },
    { "allocator/realloc-failure", test_realloc_failure_preserves_original_allocation },
    { "rom-header/orders-and-fields", test_rom_header_orders_and_fields },
    { "rom-header/regions-and-fallback", test_rom_header_regions_and_fallback_boundary },
    { "rom-header/bounds-invalid-zeroing", test_rom_header_bounds_invalid_and_zeroing },
    { "rom-header/64dd-ipl-compatibility", test_rom_header_64dd_ipl_compatibility },
    { "rom-header/prefix-normalization", test_rom_header_prefix_normalization },
    { "rom-header/streaming-boundaries", test_rom_header_streaming_boundaries },
    { "rom-header/streaming-capacity", test_rom_header_streaming_capacity },
    { "rom-header/streaming-fail-closed", test_rom_header_streaming_fail_closed },
    { NULL, NULL }
};
