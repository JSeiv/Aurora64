#if LAYER1_FOUNDATION_FOCUSED_TESTS
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "acutest.h"
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#endif
#include "menu/library/library_metrics.h"
#include "menu/library/library_service.h"
#include "menu/menu_state.h"
#include "menu/path.h"
#include "menu/views/views.h"
#include "support/fake_library_fs.h"
#include "support/layer1_view_host_shims.h"
#include "support/rom_fixture_builder.h"
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void test_layer1_fs_activity_lazy_open_close(void);
void test_layer1_fs_activity_cross_instance_rejection(void);
void test_layer1_fs_activity_pool_exhaustion(void);
void test_layer1_fs_activity_deinit_forced_close(void);
void test_layer1_scanner_poll_stats_budgets(void);
void test_layer1_scanner_poll_stats_tick_expiry(void);
void test_layer1_scanner_failure_filesystem(void);
void test_layer1_scanner_failure_candidate_io(void);
void test_layer1_scanner_failure_mutation_and_precedence(void);
void test_layer1_snapshot_builder_failure_taxonomy(void);
void test_layer1_snapshot_deferred_detail(void);
void test_layer1_snapshot_publication_metrics(void);
void test_layer1_snapshot_allocation_ledger(void);
void test_layer1_snapshot_saturation_boundaries(void);
void test_layer1_metrics_terminal_emission(void);
void test_layer1_metrics_post_poll_summary(void);
void test_layer1_metrics_scan_duration_excludes_pause(void);
void test_layer1_metrics_pause_cancel_latency(void);
void test_layer1_metrics_lifecycle_reset(void);
void test_layer1_metrics_aggregate_faults_and_reset(void);
void test_phase4_trace_protocol_context_id_reuse(void);
void test_phase4_transition_action_decisions_and_supersession(void);
void test_phase4_output_critical_usb_retry_stable_record(void);
void test_phase4_seam_service_poll_detail_usb_emission(void);
void test_phase4_writer_abi_summary_detail_trace_payloads(void);
void test_phase4_render_home_all_games_first_frame_navigation_overlay(void);
void test_phase4_context_path_roundtrip_cleanup_timing(void);

void menu_library_transition_reset(menu_t *owner);
void menu_library_coordinate_frame(menu_t *owner);
void menu_library_poll_usb_and_emit(menu_t *owner,
                                    library_metrics_writer_t writer,
                                    void *writer_context);
void library_view_model_step(menu_t *menu);

#define UI_GUARD 20000U

typedef struct {
    fake_library_fs_t fs;
    library_service_t *service;
    menu_t menu;
} ui_fixture_t;

typedef struct {
    size_t live;
    size_t allocations;
    size_t frees;
} ui_allocator_tracker_t;

typedef struct {
    unsigned char before;
    char bytes[LIBRARY_METRICS_TERMINAL_RECORD_BYTES];
    unsigned char after;
    const char *pointer;
    size_t length;
    size_t calls;
} ui_writer_t;

static void *ui_tracked_malloc(void *context, size_t size)
{
    ui_allocator_tracker_t *tracker = context;
    void *pointer = malloc(size == 0U ? 1U : size);
    if (pointer != NULL) {
        ++tracker->live;
        ++tracker->allocations;
    }
    return pointer;
}

static void *ui_tracked_calloc(void *context, size_t count, size_t size)
{
    ui_allocator_tracker_t *tracker = context;
    void *pointer;
    if (count != 0U && size > SIZE_MAX / count) return NULL;
    pointer = calloc(count == 0U ? 1U : count, size == 0U ? 1U : size);
    if (pointer != NULL) {
        ++tracker->live;
        ++tracker->allocations;
    }
    return pointer;
}

static void ui_tracked_free(void *context, void *pointer)
{
    ui_allocator_tracker_t *tracker = context;
    if (pointer == NULL) return;
    free(pointer);
    TEST_ASSERT(tracker->live != 0U);
    --tracker->live;
    ++tracker->frees;
}

static library_dirent_t ui_rom_entry(void)
{
    library_dirent_t entry;
    memset(&entry, 0, sizeof(entry));
    (void)strncpy(entry.basename, "closure.z64",
                  sizeof(entry.basename) - 1U);
    entry.type = LIBRARY_FS_ENTRY_FILE;
    entry.size = ROM_HEADER_WITH_IPL3_BYTES;
    entry.modified_time = 17;
    return entry;
}

static void ui_fixture_init(ui_fixture_t *fixture, bool with_rom,
                            const library_allocator_t *allocator)
{
    library_service_config_t config;
    library_dirent_t entry;
    uint8_t rom[ROM_HEADER_WITH_IPL3_BYTES];

    memset(fixture, 0, sizeof(*fixture));
    fake_library_fs_init(&fixture->fs);
    if (with_rom) {
        entry = ui_rom_entry();
        TEST_ASSERT(fake_library_fs_add_directory(
            &fixture->fs, "/", &entry, 1U));
        rom_fixture_build_canonical(rom);
        TEST_ASSERT(fake_library_fs_add_file(
            &fixture->fs, "/closure.z64", rom, sizeof(rom), 17));
    } else {
        TEST_ASSERT(fake_library_fs_add_directory(
            &fixture->fs, "/", NULL, 0U));
    }
    memset(&config, 0, sizeof(config));
    config.fs = fake_library_fs_interface(&fixture->fs);
    config.roots = library_roots_default();
    config.budget.max_directory_entries = 1U;
    config.budget.max_read_bytes = 64U;
    config.budget.max_ticks = 10U;
    config.allocator = allocator;
    config.storage_prefix = "sd:/";
    TEST_ASSERT(library_service_init(&fixture->service, &config));
    memset(&fixture->menu, 0, sizeof(fixture->menu));
    fixture->menu.mode = MENU_MODE_HOME;
    fixture->menu.next_mode = MENU_MODE_HOME;
    fixture->menu.storage_prefix = "sd:/";
    fixture->menu.library_service = fixture->service;
    menu_library_transition_reset(&fixture->menu);
    usb_comm_transition_reset();
}

static uint32_t ui_poll_until_published(ui_fixture_t *fixture)
{
    size_t guard;
    for (guard = 0U; guard < UI_GUARD; ++guard) {
        const library_snapshot_t *snapshot =
            library_service_snapshot_acquire(fixture->service);
        if (snapshot != NULL &&
            library_snapshot_status(snapshot) == LIBRARY_SNAPSHOT_FRESH &&
            library_snapshot_record_count(snapshot) == 1U) {
            uint32_t generation = library_snapshot_generation(snapshot);
            library_service_snapshot_release(snapshot);
            return generation;
        }
        if (snapshot != NULL) library_service_snapshot_release(snapshot);
        library_service_poll(fixture->service, MENU_MODE_HOME);
    }
    TEST_ASSERT(false);
    return 0U;
}

static void ui_fixture_destroy(ui_fixture_t *fixture)
{
    size_t guard;
    if (fixture->service == NULL) return;
    library_service_request_cancel(fixture->service);
    for (guard = 0U; guard < UI_GUARD &&
         !library_service_is_quiesced(fixture->service); ++guard)
        library_service_poll(fixture->service, MENU_MODE_BOOT);
    TEST_CHECK(guard < UI_GUARD);
    library_service_free(fixture->service);
    fixture->service = NULL;
    fixture->menu.library_service = NULL;
}

static int ui_writer_capture(void *context, const char *bytes, size_t length)
{
    ui_writer_t *writer = context;
    size_t copy_length = length;
    ++writer->calls;
    writer->pointer = bytes;
    writer->length = length;
    if (copy_length >= sizeof(writer->bytes))
        copy_length = sizeof(writer->bytes) - 1U;
    memcpy(writer->bytes, bytes, copy_length);
    writer->bytes[copy_length] = '\0';
    return (int)length;
}

static void ui_writer_reset(ui_writer_t *writer)
{
    memset(writer, 0, sizeof(*writer));
    writer->before = 0xa5U;
    writer->after = 0x5aU;
}

static uint8_t ui_event_bit(library_metrics_event_t event)
{
    return (uint8_t)(UINT8_C(1) << (unsigned int)event);
}

void test_phase4_writer_abi_summary_detail_trace_payloads(void)
{
    static const char expected[] =
        "[AURORA64 L1] ev=9 att=9 pub=0 state=6 "
        "heap=200/200/300 alloc_valid=0 owned=n/a allocs=n/a "
        "adapt=0/0 task=0/0 hdl=d0/0,f0/0 "
        "polls=1 ent=2/2/2 bytes=3/3/3 maxpoll=4 "
        "scan=10:1 pause=0:0 cancel=0:0 agap=5 ugap=10 "
        "publication_facts_valid=0 retained_path_detail_valid=0 "
        "records=n/a paths=n/a/n/a warn=n/a err=n/a sfail=0 bfail=0 "
        "trace=id1,g9,vfb,afb,p0,t=100,110,0,120,130,140,150,160 "
        "flt=alloc0,count0,time0,overlay0\n";
    library_scanner_poll_stats_t poll_stats;
    library_scanner_stats_t scanner_stats;
    library_metrics_snapshot_t snapshot;
    ui_writer_t writer;
    uint32_t id;

    _Static_assert(LIBRARY_METRICS_TERMINAL_RECORD_BYTES == 896U,
                   "terminal ABI is one 896-byte record");
    _Static_assert(LIBRARY_METRICS_TERMINAL_RECORD_BYTES - 1U == 895U,
                   "legal terminal payload maximum is 895 bytes before NUL");

    memset(&poll_stats, 0, sizeof(poll_stats));
    memset(&scanner_stats, 0, sizeof(scanner_stats));
    poll_stats.directory_entries = 2U;
    poll_stats.read_bytes = 3U;
    library_metrics_reset();
    library_metrics_scan_attempt(9U, 10U);
    library_metrics_record_scanner_poll(10U, 14U, &poll_stats);
    library_metrics_scan_terminal(&scanner_stats, LIBRARY_SCANNER_FAILED,
                                  true, 20U);
    library_metrics_heap_sample(
        LIBRARY_METRICS_HEAP_POST_LIBRARY_INIT, 300U);
    library_metrics_heap_sample(LIBRARY_METRICS_HEAP_HOME, 250U);
    library_metrics_heap_sample(LIBRARY_METRICS_HEAP_ALL_GAMES, 200U);
    library_metrics_record_action_opportunity(20U);
    library_metrics_record_action_opportunity(25U);

    library_metrics_host_set_ticks(100U);
    library_metrics_host_set_tick_step(10U);
    id = library_metrics_trace_begin(9U);
    TEST_ASSERT(library_metrics_trace_accept_transition(id, 9U, false));
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER, id, 9U));
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_EXIT, id, 9U));
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_BEGIN, id, 9U));
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED, id, 9U));
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_NONEMPTY_FRAME_SUBMITTED,
        id, 9U));
    library_metrics_record_usb_opportunity(30U);
    library_metrics_record_usb_opportunity(40U);

    ui_writer_reset(&writer);
    TEST_CHECK(library_metrics_emit_pending(ui_writer_capture, &writer));
    TEST_CHECK(writer.calls == 1U);
    TEST_CHECK(writer.pointer != writer.bytes);
    TEST_CHECK(writer.length == sizeof(expected) - 1U);
    TEST_CHECK(memcmp(writer.bytes, expected, sizeof(expected)) == 0);
    TEST_CHECK(writer.before == 0xa5U && writer.after == 0x5aU);
    TEST_CHECK(strncmp(writer.bytes, "[AURORA64 L1]", 13U) == 0);
    TEST_CHECK(strstr(writer.bytes, " ev=9 att=9 pub=0 state=6 ") != NULL);
    TEST_CHECK(strstr(writer.bytes,
                      "publication_facts_valid=0 retained_path_detail_valid=0 ") != NULL);
    TEST_CHECK(strstr(writer.bytes,
                      "trace=id1,g9,vfb,afb,p0,t=100,110,0,120,130,140,150,160 ") != NULL);

    library_metrics_snapshot(&snapshot);
    TEST_CHECK(!snapshot.terminal_pending);
    library_metrics_reset();
    library_metrics_scan_attempt(10U, 1U);
    library_metrics_scan_terminal(NULL, LIBRARY_SCANNER_FAILED, false, 2U);
    library_metrics_record_usb_opportunity(3U);
    ui_writer_reset(&writer);
    TEST_CHECK(library_metrics_emit_pending(ui_writer_capture, &writer));
    TEST_CHECK(strstr(writer.bytes, " att=10 ") != NULL);
    TEST_CHECK(strstr(writer.bytes, " att=9 ") == NULL);
}

static size_t ui_text_index(const char *text)
{
    size_t index;
    for (index = 0U; index < layer1_view_host_text_count(); ++index) {
        const char *candidate = layer1_view_host_text_at(index);
        if (candidate != NULL && strcmp(candidate, text) == 0) return index;
    }
    return SIZE_MAX;
}

void test_phase4_render_home_all_games_first_frame_navigation_overlay(void)
{
    ui_fixture_t fixture;
    ui_fixture_t loading;
    library_metrics_snapshot_t snapshot;
    uint8_t first_mask;
    uint32_t generation;
    size_t before_overlay;
    size_t index;
    size_t overlay_rows = 0U;
    int x0;
    int y0;
    int x1;
    int y1;

    ui_fixture_init(&fixture, true, NULL);
    generation = ui_poll_until_published(&fixture);
    library_metrics_reset();
    library_metrics_host_set_ticks(100U);
    library_metrics_host_set_tick_step(1U);
    layer1_view_host_reset();
    layer1_view_host_observe_menu(&fixture.menu);
    fixture.menu.home.selected = 5;
    fixture.menu.actions.enter = true;
    view_home_display(&fixture.menu, NULL);
    fixture.menu.actions.enter = false;

    TEST_CHECK(layer1_view_host_text_occurrences("Last Played") == 1U);
    TEST_CHECK(layer1_view_host_text_occurrences("Favorites") == 1U);
    TEST_CHECK(layer1_view_host_text_occurrences("History") == 1U);
    TEST_CHECK(layer1_view_host_text_occurrences("Utilities") == 1U);
    TEST_CHECK(layer1_view_host_text_occurrences("Browse") == 1U);
    TEST_CHECK(layer1_view_host_text_occurrences("All Games") == 1U);
    TEST_CHECK(layer1_view_host_box_count() == 6U);
    TEST_ASSERT(layer1_view_host_box_at(0U, &x0, &y0, &x1, &y1));
    TEST_CHECK(x0 == 52 && y0 == 82 && x1 == 220 && y1 == 214);
    TEST_ASSERT(layer1_view_host_box_at(5U, &x0, &y0, &x1, &y1));
    TEST_CHECK(x0 == 420 && y0 == 230 && x1 == 588 && y1 == 362);
    TEST_CHECK(layer1_view_host_sound_saw_input_trace());
    TEST_CHECK(layer1_view_host_sound_next_mode() == MENU_MODE_HOME);
    TEST_CHECK(fixture.menu.next_mode == MENU_MODE_LIBRARY);

    menu_library_coordinate_frame(&fixture.menu);
    TEST_CHECK(fixture.menu.next_mode == MENU_MODE_HOME);
    menu_library_coordinate_frame(&fixture.menu);
    TEST_CHECK(fixture.menu.next_mode == MENU_MODE_LIBRARY);
    fixture.menu.mode = MENU_MODE_LIBRARY;

    layer1_view_host_reset();
    layer1_view_host_observe_menu(&fixture.menu);
    view_all_games_init(&fixture.menu);
    view_all_games_display(&fixture.menu, NULL);
    TEST_CHECK(layer1_view_host_attach_saw_frame_begin());
    TEST_CHECK(!layer1_view_host_detach_saw_frame_submitted());
    TEST_CHECK(layer1_view_host_saw_text("ABCDEFGHIJKLMNOPQRST"));
    TEST_CHECK(layer1_view_host_saw_text("A Select   B Back"));
    TEST_CHECK(ui_text_index("ABCDEFGHIJKLMNOPQRST") <
               ui_text_index("A Select   B Back"));
    TEST_ASSERT(layer1_view_host_box_at(0U, &x0, &y0, &x1, &y1));
    TEST_CHECK(x0 == 52 && y0 == 82 && x1 == 220 && y1 == 214);
    before_overlay = layer1_view_host_text_count();
    for (index = 0U; index < before_overlay; ++index) {
        const char *text = layer1_view_host_text_at(index);
        TEST_CHECK(text == NULL || strncmp(text, "L1 ", 3U) != 0);
    }

    library_metrics_snapshot(&snapshot);
    TEST_CHECK(snapshot.trace.generation == generation);
    TEST_CHECK((snapshot.trace.valid_mask & ui_event_bit(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER)) != 0U);
    TEST_CHECK((snapshot.trace.valid_mask & ui_event_bit(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_EXIT)) != 0U);
    TEST_CHECK((snapshot.trace.valid_mask & ui_event_bit(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED)) != 0U);
    TEST_CHECK((snapshot.trace.valid_mask & ui_event_bit(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_NONEMPTY_FRAME_SUBMITTED)) != 0U);
    first_mask = snapshot.trace.valid_mask;

    view_all_games_display(&fixture.menu, NULL);
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(snapshot.trace.valid_mask == first_mask);
    for (index = before_overlay;
         index < layer1_view_host_text_count(); ++index) {
        const char *text = layer1_view_host_text_at(index);
        if (text != NULL && strncmp(text, "L1 ", 3U) == 0) {
            float expected_y = 124.0f + (float)overlay_rows * 12.0f;
            TEST_CHECK(layer1_view_host_text_x(index) == 52.0f);
            TEST_CHECK(layer1_view_host_text_y(index) == expected_y);
            ++overlay_rows;
        }
    }
    TEST_CHECK(overlay_rows > 0U && overlay_rows <= 4U);

    ui_fixture_init(&loading, false, NULL);
    library_metrics_reset();
    library_metrics_host_set_ticks(500U);
    library_metrics_host_set_tick_step(1U);
    generation = library_metrics_trace_begin(0U);
    TEST_ASSERT(library_metrics_trace_accept_transition(generation, 0U, false));
    loading.menu.mode = MENU_MODE_LIBRARY;
    loading.menu.next_mode = MENU_MODE_LIBRARY;
    layer1_view_host_reset();
    view_all_games_init(&loading.menu);
    view_all_games_display(&loading.menu, NULL);
    TEST_CHECK(layer1_view_host_saw_text("Scanning..."));
    TEST_CHECK(!layer1_view_host_saw_text("ABCDEFGHIJKLMNOPQRST"));
    library_metrics_snapshot(&snapshot);
    TEST_CHECK((snapshot.trace.valid_mask & ui_event_bit(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED)) != 0U);
    TEST_CHECK((snapshot.trace.valid_mask & ui_event_bit(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_NONEMPTY_FRAME_SUBMITTED)) == 0U);

    ui_fixture_destroy(&loading);
    ui_fixture_destroy(&fixture);
}

void test_phase4_context_path_roundtrip_cleanup_timing(void)
{
    ui_fixture_t fixture;
    ui_allocator_tracker_t tracker;
    library_allocator_t allocator;
    library_metrics_snapshot_t metrics;
    const library_snapshot_t *retained;
    path_t *rejected = (path_t *)(uintptr_t)1U;
    menu_t wrong_owner;
    rom_fingerprint_t valid_fingerprint;
    uint32_t generation;
    uint32_t transition_id;
    size_t calls_before;
    size_t frees_before;

    memset(&tracker, 0, sizeof(tracker));
    allocator.context = &tracker;
    allocator.malloc_fn = ui_tracked_malloc;
    allocator.calloc_fn = ui_tracked_calloc;
    allocator.free_fn = ui_tracked_free;
    ui_fixture_init(&fixture, true, &allocator);
    generation = ui_poll_until_published(&fixture);

    library_metrics_reset();
    library_metrics_host_set_ticks(1000U);
    library_metrics_host_set_tick_step(1U);
    layer1_view_host_reset();
    layer1_view_host_observe_menu(&fixture.menu);
    fixture.menu.home.selected = 5;
    fixture.menu.actions.enter = true;
    view_home_display(&fixture.menu, NULL);
    fixture.menu.actions.enter = false;
    menu_library_coordinate_frame(&fixture.menu);
    menu_library_coordinate_frame(&fixture.menu);
    fixture.menu.mode = MENU_MODE_LIBRARY;
    fixture.menu.next_mode = MENU_MODE_LIBRARY;
    view_all_games_init(&fixture.menu);
    view_all_games_display(&fixture.menu, NULL);
    TEST_ASSERT(fixture.menu.library_view.selected_valid);
    valid_fingerprint = fixture.menu.library_view.selected_fingerprint;

    library_metrics_snapshot(&metrics);
    transition_id = metrics.trace.transition_id;
    TEST_CHECK(transition_id != 0U);
    TEST_CHECK(metrics.trace.generation == generation);
    TEST_CHECK(!metrics.publication_facts_valid);
    TEST_CHECK(!metrics.retained_path_detail_valid);

    layer1_view_host_set_path_success(true);
    fixture.menu.actions.enter = true;
    library_view_model_step(&fixture.menu);
    fixture.menu.actions.enter = false;
    library_view_model_step(&fixture.menu);
    TEST_CHECK(fixture.menu.next_mode == MENU_MODE_LOAD_ROM);
    TEST_CHECK(strcmp(layer1_view_host_path_prefix(), "sd:/") == 0);
    TEST_CHECK(strcmp(layer1_view_host_path_logical(), "/closure.z64") == 0);
    TEST_CHECK(layer1_view_host_pending_path_matches(
        &fixture.menu, "sd://closure.z64", MENU_MODE_LIBRARY));
    TEST_CHECK(layer1_view_host_path_live_count() == 1U);

    TEST_CHECK(!path_try_init(&rejected, "sd:/", NULL));
    TEST_CHECK(rejected == NULL);
    memset(&wrong_owner, 0, sizeof(wrong_owner));
    TEST_CHECK(!layer1_view_host_pending_path_matches(
        &wrong_owner, "sd://closure.z64", MENU_MODE_LIBRARY));

    frees_before = layer1_view_host_path_free_count();
    calls_before = layer1_view_host_path_calls();
    fixture.menu.mode = MENU_MODE_LIBRARY;
    fixture.menu.next_mode = MENU_MODE_LIBRARY;
    fixture.menu.library_view.transition = 0U;
    fixture.menu.library_view.selected_valid = true;
    fixture.menu.library_view.selected_fingerprint = valid_fingerprint;
    fixture.menu.actions.enter = true;
    library_view_model_step(&fixture.menu);
    fixture.menu.actions.enter = false;
    memset(&fixture.menu.library_view.pending_fingerprint, 0,
           sizeof(fixture.menu.library_view.pending_fingerprint));
    library_view_model_step(&fixture.menu);
    TEST_CHECK(layer1_view_host_path_calls() == calls_before);
    TEST_CHECK(layer1_view_host_path_free_count() == frees_before + 1U);
    TEST_CHECK(layer1_view_host_path_live_count() == 0U);
    TEST_CHECK(fixture.menu.next_mode == MENU_MODE_LIBRARY);

    fixture.menu.library_view.transition = 0U;
    fixture.menu.library_view.selected_valid = true;
    fixture.menu.library_view.selected_fingerprint = valid_fingerprint;
    fixture.menu.actions.enter = true;
    library_view_model_step(&fixture.menu);
    fixture.menu.actions.enter = false;
    library_view_model_step(&fixture.menu);
    TEST_CHECK(layer1_view_host_pending_path_matches(
        &fixture.menu, "sd://closure.z64", MENU_MODE_LIBRARY));
    TEST_CHECK(layer1_view_host_path_live_count() == 1U);
    library_metrics_snapshot(&metrics);
    TEST_CHECK(metrics.trace.transition_id == transition_id);
    TEST_CHECK(metrics.trace.generation == generation);
    TEST_CHECK(!metrics.publication_facts_valid);
    TEST_CHECK(!metrics.retained_path_detail_valid);

    retained = library_service_snapshot_acquire(fixture.service);
    TEST_ASSERT(retained != NULL);
    TEST_CHECK(library_snapshot_generation(retained) == generation);
    TEST_CHECK(library_snapshot_record_count(retained) == 1U);
    TEST_CHECK(library_service_is_quiesced(fixture.service));
    library_service_free(fixture.service);
    fixture.service = NULL;
    fixture.menu.library_service = NULL;
    TEST_CHECK(library_snapshot_generation(retained) == generation);
    TEST_CHECK(library_snapshot_source_path(retained, 0U) != NULL);
    TEST_CHECK(tracker.live != 0U);
    library_service_snapshot_release(retained);
    TEST_CHECK(tracker.live == 0U);
    TEST_CHECK(tracker.allocations == tracker.frees);

    layer1_view_host_reset();
    TEST_CHECK(layer1_view_host_path_live_count() == 0U);
}

TEST_LIST = {
    { "foundation/fs/activity-lazy-open-close", test_layer1_fs_activity_lazy_open_close },
    { "foundation/fs/cross-instance-rejection", test_layer1_fs_activity_cross_instance_rejection },
    { "foundation/fs/pool-exhaustion", test_layer1_fs_activity_pool_exhaustion },
    { "foundation/fs/deinit-forced-close", test_layer1_fs_activity_deinit_forced_close },
    { "foundation/scanner/poll-stats-budgets", test_layer1_scanner_poll_stats_budgets },
    { "foundation/scanner/poll-stats-tick-expiry", test_layer1_scanner_poll_stats_tick_expiry },
    { "foundation/scanner/failure-filesystem", test_layer1_scanner_failure_filesystem },
    { "foundation/scanner/failure-candidate-io", test_layer1_scanner_failure_candidate_io },
    { "foundation/scanner/failure-mutation-precedence", test_layer1_scanner_failure_mutation_and_precedence },
    { "foundation/snapshot/builder-failure-taxonomy", test_layer1_snapshot_builder_failure_taxonomy },
    { "foundation/snapshot/deferred-detail", test_layer1_snapshot_deferred_detail },
    { "foundation/snapshot/publication-metrics", test_layer1_snapshot_publication_metrics },
    { "foundation/snapshot/allocation-ledger", test_layer1_snapshot_allocation_ledger },
    { "foundation/snapshot/saturation-boundaries", test_layer1_snapshot_saturation_boundaries },
    { "foundation/metrics/terminal-emission", test_layer1_metrics_terminal_emission },
    { "foundation/metrics/post-poll-summary", test_layer1_metrics_post_poll_summary },
    { "foundation/metrics/scan-duration-excludes-pause", test_layer1_metrics_scan_duration_excludes_pause },
    { "foundation/metrics/pause-cancel-latency", test_layer1_metrics_pause_cancel_latency },
    { "foundation/metrics/lifecycle-reset", test_layer1_metrics_lifecycle_reset },
    { "foundation/metrics/aggregate-faults-reset", test_layer1_metrics_aggregate_faults_and_reset },
    { "phase4/trace/protocol-context-id-reuse", test_phase4_trace_protocol_context_id_reuse },
    { "phase4/transition/action-decisions-and-supersession", test_phase4_transition_action_decisions_and_supersession },
    { "phase4/output/critical-usb-retry-stable-record", test_phase4_output_critical_usb_retry_stable_record },
    { "phase4/seam/service-poll-detail-usb-emission", test_phase4_seam_service_poll_detail_usb_emission },
    { "phase4/writer/abi-summary-detail-trace-payloads", test_phase4_writer_abi_summary_detail_trace_payloads },
    { "phase4/render/home-all-games-first-frame-navigation-overlay", test_phase4_render_home_all_games_first_frame_navigation_overlay },
    { "phase4/context/path-roundtrip-cleanup-timing", test_phase4_context_path_roundtrip_cleanup_timing },
    { NULL, NULL }
};
#endif
