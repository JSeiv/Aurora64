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
void test_library_scanner_cooperative_traversal_and_validation(void);
void test_library_scanner_two_pass_retry_and_replacement(void);
void test_library_scanner_mutation_failure_and_candidate_io(void);
void test_library_scanner_pause_resume_cancel_and_ticks(void);
void test_library_scanner_fatal_directory_and_capacity(void);
void test_library_scanner_oom_every_allocation(void);
void test_library_scanner_removal_truncation_and_close_retry(void);
void test_library_scanner_invalid_header_close_retry(void);
void test_library_scanner_destroy_retries_directory_close(void);
void test_library_scanner_tick_expiry_stops_reads(void);
void test_library_scanner_directory_capacity_classes(void);
void test_library_scanner_512_compatible_sources(void);
void test_library_snapshot_atomic_generations(void);
void test_library_snapshot_collapse_order_lookup(void);
void test_library_snapshot_conflict_and_status(void);
void test_library_snapshot_status_immutability_and_failure(void);
void test_library_snapshot_selection_reconciliation(void);
void test_library_snapshot_capacity_poison_and_cancel(void);
void test_library_snapshot_oom_retains_publication(void);
void test_library_snapshot_heap_accounting(void);
void test_library_service_init_is_lazy_and_safe_modes_start(void);
void test_library_service_unsafe_modes_never_start_work(void);
void test_library_service_one_bounded_unit_per_poll(void);
void test_library_service_pause_quiesce_resume_and_retry_close(void);
void test_library_service_cancel_quiesce_restart_and_file_close(void);
void test_library_service_pause_after_completion_drains_publication(void);
void test_library_service_complete_publication_is_atomic(void);
void test_library_service_failure_retains_old_snapshot(void);
void test_library_service_acquisition_and_scanner_transfer_lifetimes(void);
void test_library_service_reader_backpressure_retries_restart(void);
void test_library_service_transition_coordinator_defers_all_safe_exits(void);
void test_library_service_free_is_fail_closed_until_quiesced(void);
void test_library_service_init_and_owned_allocation_failures_are_atomic(void);
void test_library_service_every_refresh_oom_retains_publication(void);
void test_library_view_counts_navigation_and_pages(void);
void test_library_view_title_precedence_and_bounds(void);
void test_library_view_identity_reconciliation(void);
void test_path_fallible_success_clone_and_every_oom(void);
void test_path_asserting_apis_still_work(void);
void test_library_view_source_validation(void);
void test_library_view_launch_quiescence_and_ownership(void);
void test_library_view_launch_failure_recovery(void);
void test_library_view_selection_removed_while_pausing(void);
void test_library_view_exit_gating_and_balanced_snapshots(void);

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
    { "library-scanner/cooperation-traversal-validation", test_library_scanner_cooperative_traversal_and_validation },
    { "library-scanner/two-pass-retry-replacement", test_library_scanner_two_pass_retry_and_replacement },
    { "library-scanner/mutation-candidate-io", test_library_scanner_mutation_failure_and_candidate_io },
    { "library-scanner/pause-resume-cancel-ticks", test_library_scanner_pause_resume_cancel_and_ticks },
    { "library-scanner/fatal-directory-capacity", test_library_scanner_fatal_directory_and_capacity },
    { "library-scanner/oom-every-allocation", test_library_scanner_oom_every_allocation },
    { "library-scanner/removal-truncation-close-retry", test_library_scanner_removal_truncation_and_close_retry },
    { "library-scanner/invalid-header-close-retry", test_library_scanner_invalid_header_close_retry },
    { "library-scanner/destroy-directory-close-retry", test_library_scanner_destroy_retries_directory_close },
    { "library-scanner/tick-expiry-stops-reads", test_library_scanner_tick_expiry_stops_reads },
    { "library-scanner/directory-capacity-classes", test_library_scanner_directory_capacity_classes },
    { "library-scanner/512-compatible-sources", test_library_scanner_512_compatible_sources },
    { "library-snapshot/atomic-generations", test_library_snapshot_atomic_generations },
    { "library-snapshot/collapse-order-lookup", test_library_snapshot_collapse_order_lookup },
    { "library-snapshot/conflict-status", test_library_snapshot_conflict_and_status },
    { "library-snapshot/status-immutability-failure", test_library_snapshot_status_immutability_and_failure },
    { "library-snapshot/selection-reconciliation", test_library_snapshot_selection_reconciliation },
    { "library-snapshot/capacity-poison-cancel", test_library_snapshot_capacity_poison_and_cancel },
    { "library-snapshot/oom-retains-publication", test_library_snapshot_oom_retains_publication },
    { "library-snapshot/heap-accounting", test_library_snapshot_heap_accounting },
    { "library-service/lazy-init-safe-modes", test_library_service_init_is_lazy_and_safe_modes_start },
    { "library-service/unsafe-modes", test_library_service_unsafe_modes_never_start_work },
    { "library-service/one-bounded-unit", test_library_service_one_bounded_unit_per_poll },
    { "library-service/pause-quiesce-resume-close", test_library_service_pause_quiesce_resume_and_retry_close },
    { "library-service/cancel-quiesce-restart-close", test_library_service_cancel_quiesce_restart_and_file_close },
    { "library-service/pause-after-completion", test_library_service_pause_after_completion_drains_publication },
    { "library-service/atomic-publication", test_library_service_complete_publication_is_atomic },
    { "library-service/failure-retains-old", test_library_service_failure_retains_old_snapshot },
    { "library-service/acquisition-transfer-lifetimes", test_library_service_acquisition_and_scanner_transfer_lifetimes },
    { "library-service/reader-backpressure-retry", test_library_service_reader_backpressure_retries_restart },
    { "library-service/transition-coordinator", test_library_service_transition_coordinator_defers_all_safe_exits },
    { "library-service/free-only-quiesced", test_library_service_free_is_fail_closed_until_quiesced },
    { "library-service/init-oom-atomic", test_library_service_init_and_owned_allocation_failures_are_atomic },
    { "library-service/refresh-oom-retains", test_library_service_every_refresh_oom_retains_publication },
    { "library-view/counts-navigation-pages", test_library_view_counts_navigation_and_pages },
    { "library-view/title-precedence-bounds", test_library_view_title_precedence_and_bounds },
    { "library-view/identity-reconciliation", test_library_view_identity_reconciliation },
    { "path/fallible-success-every-oom", test_path_fallible_success_clone_and_every_oom },
    { "path/asserting-apis", test_path_asserting_apis_still_work },
    { "library-view/source-validation", test_library_view_source_validation },
    { "library-view/launch-quiescence-ownership", test_library_view_launch_quiescence_and_ownership },
    { "library-view/launch-failure-recovery", test_library_view_launch_failure_recovery },
    { "library-view/selection-removed-while-pausing", test_library_view_selection_removed_while_pausing },
    { "library-view/exit-gating-balanced-snapshots", test_library_view_exit_gating_and_balanced_snapshots },
    { NULL, NULL }
};
