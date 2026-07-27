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
void test_sha256_published_vectors(void);
void test_sha256_incremental_chunking(void);
void test_sha256_misuse_and_overflow(void);
void test_rom_identity_orders_oracle_and_chunks(void);
void test_rom_identity_names_mutation_and_exactly_once(void);
void test_rom_identity_malformed_tail(void);
void test_rom_identity_invalid_and_lifecycle(void);
void test_rom_fingerprint_equality_policy(void);
void test_library_roots_normalization(void);
void test_library_roots_bounds_and_atomicity(void);
void test_library_roots_reduction_and_limits(void);
void test_library_roots_deterministic_zeroed_output(void);
void test_library_roots_default(void);
void test_library_exclusions(void);
void test_fake_library_fs_directory_lifecycle(void);
void test_fake_library_fs_errors_and_balance(void);
void test_fake_library_fs_file_stat_ticks_and_ownership(void);
void test_fake_library_fs_two_instance_handle_provenance(void);
void test_fake_library_fs_pool_stale_ownership_and_bounds(void);
void test_libdragon_adapter_paths_outputs_and_errors(void);
void test_libdragon_adapter_lifecycle_pool_and_stale_tokens(void);
void test_libdragon_adapter_next_errors_cleanup_and_deinit(void);
void test_libdragon_adapter_close_failure_retries(void);
void test_libdragon_adapter_open_rollback_quarantines_close_failure(void);
void test_libdragon_adapter_init_bounds_and_zeroing(void);
void test_libdragon_adapter_two_instance_handle_provenance(void);
void test_libdragon_adapter_files_stat_cross_tokens_and_pool(void);
void test_libdragon_adapter_token_exhaustion_preflight(void);

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
    { "sha256/published-vectors", test_sha256_published_vectors },
    { "sha256/incremental-chunking", test_sha256_incremental_chunking },
    { "sha256/misuse-overflow", test_sha256_misuse_and_overflow },
    { "rom-identity/orders-oracle-chunks", test_rom_identity_orders_oracle_and_chunks },
    { "rom-identity/names-mutation-exactly-once", test_rom_identity_names_mutation_and_exactly_once },
    { "rom-identity/malformed-tail", test_rom_identity_malformed_tail },
    { "rom-identity/invalid-lifecycle", test_rom_identity_invalid_and_lifecycle },
    { "rom-identity/equality-policy", test_rom_fingerprint_equality_policy },
    { "library-roots/normalization", test_library_roots_normalization },
    { "library-roots/bounds-atomicity", test_library_roots_bounds_and_atomicity },
    { "library-roots/reduction-limits", test_library_roots_reduction_and_limits },
    { "library-roots/deterministic-zeroed-output", test_library_roots_deterministic_zeroed_output },
    { "library-roots/default", test_library_roots_default },
    { "library-roots/exclusions", test_library_exclusions },
    { "library-fs/directory-lifecycle", test_fake_library_fs_directory_lifecycle },
    { "library-fs/errors-balance", test_fake_library_fs_errors_and_balance },
    { "library-fs/file-stat-ticks", test_fake_library_fs_file_stat_ticks_and_ownership },
    { "library-fs/fake-two-instance-provenance", test_fake_library_fs_two_instance_handle_provenance },
    { "library-fs/fake-pool-stale-ownership-bounds", test_fake_library_fs_pool_stale_ownership_and_bounds },
    { "library-fs/libdragon-paths-outputs-errors", test_libdragon_adapter_paths_outputs_and_errors },
    { "library-fs/libdragon-pool-stale", test_libdragon_adapter_lifecycle_pool_and_stale_tokens },
    { "library-fs/libdragon-cleanup-deinit", test_libdragon_adapter_next_errors_cleanup_and_deinit },
    { "library-fs/libdragon-close-failure-retries", test_libdragon_adapter_close_failure_retries },
    { "library-fs/libdragon-open-rollback-quarantine", test_libdragon_adapter_open_rollback_quarantines_close_failure },
    { "library-fs/libdragon-init-bounds", test_libdragon_adapter_init_bounds_and_zeroing },
    { "library-fs/libdragon-two-instance-provenance", test_libdragon_adapter_two_instance_handle_provenance },
    { "library-fs/libdragon-files-stat-cross-pool", test_libdragon_adapter_files_stat_cross_tokens_and_pool },
    { "library-fs/libdragon-token-exhaustion-preflight", test_libdragon_adapter_token_exhaustion_preflight },
    { NULL, NULL }
};
