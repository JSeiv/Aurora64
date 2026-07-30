#define TEST_NO_MAIN
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "acutest.h"
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "menu/library/library_service.h"
#include "menu/library/library_snapshot.h"
#include "menu/path.h"
#include "support/fake_library_fs.h"
#include "support/rom_fixture_builder.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_GUARD 20000U

enum {
    LIBRARY_VIEW_TRANSITION_IDLE,
    LIBRARY_VIEW_TRANSITION_LAUNCH,
    LIBRARY_VIEW_TRANSITION_EXIT,
    LIBRARY_VIEW_TRANSITION_SUBMITTED
};

enum {
    LIBRARY_VIEW_MESSAGE_NONE,
    LIBRARY_VIEW_MESSAGE_REMOVED,
    LIBRARY_VIEW_MESSAGE_INVALID_SOURCE,
    LIBRARY_VIEW_MESSAGE_OOM
};

enum {
    LIBRARY_VIEW_MOVE_UP,
    LIBRARY_VIEW_MOVE_DOWN,
    LIBRARY_VIEW_MOVE_LEFT,
    LIBRARY_VIEW_MOVE_RIGHT
};

typedef struct {
    menu_mode_t mode;
    menu_mode_t next_mode;
    const char *storage_prefix;
    library_service_t *library_service;
    struct {
        bool go_up;
        bool go_down;
        bool go_left;
        bool go_right;
        bool enter;
        bool back;
    } actions;
    struct {
        rom_fingerprint_t selected_fingerprint;
        rom_fingerprint_t pending_fingerprint;
        uint32_t last_resolved_index;
        uint32_t visual_offset;
        uint32_t observed_generation;
        menu_mode_t pending_destination;
        uint8_t transition;
        uint8_t message;
        bool selected_valid;
    } library_view;
} library_view_menu_t;

typedef struct {
    fake_library_fs_t fs;
    library_service_t *service;
} view_fixture_t;

uint32_t library_view_model_move(uint32_t selected, size_t count, uint8_t move);
uint32_t library_view_model_page_offset(uint32_t selected, size_t count);
uint32_t library_view_model_resolve(const library_record_t *records, size_t count,
                                    const rom_fingerprint_t *selected,
                                    bool selected_valid, uint32_t previous_index,
                                    bool *found);
void library_view_model_title(const library_record_t *record,
                              const char *logical_path, char *out,
                              size_t out_size);
bool library_view_model_source_path(const library_snapshot_t *snapshot,
                                    const library_record_t *record,
                                    const char **logical_path);
void library_view_model_step(library_view_menu_t *menu);
void library_view_host_snapshot_counts_reset(void);
size_t library_view_host_snapshot_acquire_count(void);
size_t library_view_host_snapshot_release_count(void);

static path_t *handoff_path;
static menu_mode_t handoff_return_mode;
static size_t handoff_calls;

void view_load_rom_set_pending_path(library_view_menu_t *menu, path_t *rom_path,
                                    menu_mode_t return_mode)
{
    (void)menu;
    path_free(handoff_path);
    handoff_path = rom_path;
    handoff_return_mode = return_mode;
    ++handoff_calls;
}

static void reset_handoff(void)
{
    path_free(handoff_path);
    handoff_path = NULL;
    handoff_return_mode = MENU_MODE_NONE;
    handoff_calls = 0U;
    path_host_test_reset();
}

static library_record_t record_with_id(uint8_t id)
{
    library_record_t record;
    memset(&record, 0, sizeof(record));
    record.fingerprint.bytes[0] = id;
    return record;
}

static library_dirent_t file_entry(const char *name)
{
    library_dirent_t entry;
    memset(&entry, 0, sizeof(entry));
    (void)strncpy(entry.basename, name, sizeof(entry.basename) - 1U);
    entry.type = LIBRARY_FS_ENTRY_FILE;
    entry.size = 512U;
    entry.modified_time = 7;
    return entry;
}

static void fixture_init(view_fixture_t *fixture, bool with_rom)
{
    library_service_config_t config;
    library_dirent_t entry;
    uint8_t rom[ROM_HEADER_WITH_IPL3_BYTES];

    memset(fixture, 0, sizeof(*fixture));
    fake_library_fs_init(&fixture->fs);
    if (with_rom) {
        entry = file_entry("game_one.z64");
        TEST_ASSERT(fake_library_fs_add_directory(&fixture->fs, "/", &entry, 1U));
        rom_fixture_build_canonical(rom);
        TEST_ASSERT(fake_library_fs_add_file(&fixture->fs, "/game_one.z64",
                                             rom, 512U, 7));
    } else {
        TEST_ASSERT(fake_library_fs_add_directory(&fixture->fs, "/", NULL, 0U));
    }

    memset(&config, 0, sizeof(config));
    config.fs = fake_library_fs_interface(&fixture->fs);
    config.roots = library_roots_default();
    config.budget.max_directory_entries = 1U;
    config.budget.max_read_bytes = 64U;
    config.budget.max_ticks = 10U;
    config.storage_prefix = "sd:/";
    TEST_ASSERT(library_service_init(&fixture->service, &config));
}

static void poll_once(view_fixture_t *fixture, menu_mode_t mode)
{
    library_service_poll(fixture->service, mode);
}

static uint32_t published_generation(view_fixture_t *fixture,
                                     size_t expected_records)
{
    size_t guard;
    for (guard = 0U; guard < VIEW_GUARD; ++guard) {
        const library_snapshot_t *snapshot =
            library_service_snapshot_acquire(fixture->service);
        if (snapshot != NULL &&
            library_snapshot_status(snapshot) == LIBRARY_SNAPSHOT_FRESH &&
            library_snapshot_record_count(snapshot) == expected_records) {
            uint32_t generation = library_snapshot_generation(snapshot);
            library_service_snapshot_release(snapshot);
            return generation;
        }
        if (snapshot != NULL) library_service_snapshot_release(snapshot);
        poll_once(fixture, MENU_MODE_LIBRARY);
    }
    TEST_CHECK_(false, "view fixture did not publish within guard");
    return 0U;
}

static void settle_quiesced(view_fixture_t *fixture, menu_mode_t mode)
{
    size_t guard;
    for (guard = 0U; guard < VIEW_GUARD; ++guard) {
        if (library_service_is_quiesced(fixture->service)) return;
        poll_once(fixture, mode);
    }
    TEST_CHECK_(false, "view fixture did not quiesce within guard");
}

static void fixture_destroy(view_fixture_t *fixture)
{
    if (!library_service_is_quiesced(fixture->service)) {
        library_service_request_cancel(fixture->service);
        settle_quiesced(fixture, MENU_MODE_BOOT);
    }
    library_service_free(fixture->service);
    fixture->service = NULL;
    TEST_CHECK(fixture->fs.active_dir_handles == 0U);
    TEST_CHECK(fixture->fs.active_file_handles == 0U);
}

static library_view_menu_t menu_for(view_fixture_t *fixture)
{
    library_view_menu_t menu;
    memset(&menu, 0, sizeof(menu));
    menu.mode = MENU_MODE_LIBRARY;
    menu.next_mode = MENU_MODE_LIBRARY;
    menu.storage_prefix = "sd:/";
    menu.library_service = fixture->service;
    menu.library_view.pending_destination = MENU_MODE_LIBRARY;
    return menu;
}

void test_library_view_counts_navigation_and_pages(void)
{
    static const size_t counts[] = { 0U, 1U, 4U, 6U, 7U, 13U, 256U };
    size_t i;

    for (i = 0U; i < sizeof(counts) / sizeof(counts[0]); ++i) {
        size_t count = counts[i];
        uint32_t last = count == 0U ? 0U : (uint32_t)(count - 1U);
        TEST_CHECK(library_view_model_move(0U, count, LIBRARY_VIEW_MOVE_UP) == 0U);
        TEST_CHECK(library_view_model_move(0U, count, LIBRARY_VIEW_MOVE_LEFT) == 0U);
        TEST_CHECK(library_view_model_move(last, count, LIBRARY_VIEW_MOVE_RIGHT) == last);
        TEST_CHECK(library_view_model_move(last, count, LIBRARY_VIEW_MOVE_DOWN) == last);
        TEST_CHECK(library_view_model_page_offset(last, count) <= last);
        TEST_CHECK(library_view_model_page_offset(last, count) % 6U == 0U);
    }
    TEST_CHECK(library_view_model_page_offset(5U, 6U) == 0U);
    TEST_CHECK(library_view_model_page_offset(6U, 7U) == 6U);
    TEST_CHECK(library_view_model_page_offset(255U, 256U) == 252U);
    TEST_CHECK(library_view_model_move(2U, 7U, LIBRARY_VIEW_MOVE_DOWN) == 5U);
    TEST_CHECK(library_view_model_move(4U, 7U, LIBRARY_VIEW_MOVE_DOWN) == 4U);
    TEST_CHECK(library_view_model_move(5U, 256U, LIBRARY_VIEW_MOVE_DOWN) == 8U);
    TEST_CHECK(library_view_model_move(8U, 256U, LIBRARY_VIEW_MOVE_LEFT) == 7U);
    TEST_CHECK(library_view_model_move(7U, 256U, LIBRARY_VIEW_MOVE_LEFT) == 6U);
}

void test_library_view_title_precedence_and_bounds(void)
{
    library_record_t record = record_with_id(1U);
    char title[24];

    memcpy(record.title, "Normalized Header   ", 20U);
    library_view_model_title(&record, "/games/file_name (USA).z64",
                             title, sizeof(title));
    TEST_CHECK(strcmp(title, "Normalized Header") == 0);

    memset(record.title, 0, sizeof(record.title));
    library_view_model_title(&record, "/games/file_name (USA).z64",
                             title, sizeof(title));
    TEST_CHECK(strcmp(title, "file name (USA)") == 0);
    library_view_model_title(&record, "/games/.z64", title, sizeof(title));
    TEST_CHECK(strcmp(title, ".z64") == 0);
    library_view_model_title(&record, "/", title, sizeof(title));
    TEST_CHECK(strcmp(title, "Untitled") == 0);

    memset(record.title, 'X', sizeof(record.title));
    memset(title, 'Y', sizeof(title));
    library_view_model_title(&record, "/ignored.z64", title, 8U);
    TEST_CHECK(title[7] == '\0');
    TEST_CHECK(strlen(title) == 7U);
}

void test_library_view_identity_reconciliation(void)
{
    library_record_t records[LIBRARY_SNAPSHOT_MAX_RECORDS];
    rom_fingerprint_t selected;
    bool found = false;
    size_t i;

    for (i = 0U; i < LIBRARY_SNAPSHOT_MAX_RECORDS; ++i)
        records[i] = record_with_id((uint8_t)i);
    memset(records[200].fingerprint.bytes, 0xA5,
           sizeof(records[200].fingerprint.bytes));
    selected = records[200].fingerprint;

    TEST_CHECK(library_view_model_resolve(records, 256U, &selected, true,
                                          200U, &found) == 200U);
    TEST_CHECK(found);
    records[3] = records[200];
    records[200] = record_with_id(99U);
    TEST_CHECK(library_view_model_resolve(records, 256U, &selected, true,
                                          200U, &found) == 3U);
    TEST_CHECK(found);

    records[3] = record_with_id(3U);
    TEST_CHECK(library_view_model_resolve(records, 4U, &selected, true,
                                          200U, &found) == 3U);
    TEST_CHECK(!found);
    TEST_CHECK(library_view_model_resolve(records, 0U, &selected, true,
                                          5U, &found) == 0U);
    TEST_CHECK(!found);
}

void test_path_fallible_success_clone_and_every_oom(void)
{
    path_t *path = NULL;
    path_t *clone = NULL;
    size_t fail_at;

    path_host_test_reset();
    TEST_ASSERT(path_try_init(&path, "sd:", "/games/test.z64"));
    TEST_CHECK(strcmp(path_get(path), "sd:/games/test.z64") == 0);
    TEST_ASSERT(path_try_clone(&clone, path));
    TEST_CHECK(strcmp(path_get(clone), path_get(path)) == 0);
    path_free(clone);
    path_free(path);
    TEST_CHECK(path_host_test_live_allocations() == 0U);

    for (fail_at = 0U; fail_at < 2U; ++fail_at) {
        path = (path_t *)(uintptr_t)1U;
        path_host_test_reset();
        path_host_test_fail_after(fail_at);
        TEST_CHECK(!path_try_init(&path, "sd:", "/games/test.z64"));
        TEST_CHECK(path == NULL);
        TEST_CHECK(path_host_test_live_allocations() == 0U);
    }

    path_host_test_reset();
    TEST_ASSERT(path_try_init(&path, "sd:", "/games/test.z64"));
    for (fail_at = 0U; fail_at < 2U; ++fail_at) {
        clone = (path_t *)(uintptr_t)1U;
        path_host_test_fail_after(fail_at);
        TEST_CHECK(!path_try_clone(&clone, path));
        TEST_CHECK(clone == NULL);
        TEST_CHECK(path_host_test_live_allocations() == 2U);
    }
    path_host_test_reset();
    path_free(path);
    TEST_CHECK(path_host_test_live_allocations() == 0U);
}

void test_path_asserting_apis_still_work(void)
{
    path_t *path;
    path_t *clone;

    path_host_test_reset();
    path = path_init("sd:", "/one/two.z64");
    clone = path_clone(path);
    TEST_CHECK(path_are_match(path, clone));
    path_pop(clone);
    TEST_CHECK(strcmp(path_get(clone), "sd:/one") == 0);
    path_free(clone);
    path_free(path);
    TEST_CHECK(path_host_test_live_allocations() == 0U);
}

void test_library_view_source_validation(void)
{
    view_fixture_t fixture;
    const library_snapshot_t *snapshot;
    const library_record_t *published;
    library_record_t record;
    const char *logical_path = (const char *)(uintptr_t)1U;

    fixture_init(&fixture, true);
    (void)published_generation(&fixture, 1U);
    snapshot = library_service_snapshot_acquire(fixture.service);
    TEST_ASSERT(snapshot != NULL);
    published = library_snapshot_record_at(snapshot, 0U);
    TEST_ASSERT(published != NULL);
    record = *published;
    TEST_CHECK(library_view_model_source_path(snapshot, &record, &logical_path));
    TEST_CHECK(strcmp(logical_path, "/game_one.z64") == 0);

    record.source_count = 0U;
    TEST_CHECK(!library_view_model_source_path(snapshot, &record, &logical_path));
    TEST_CHECK(logical_path == NULL);
    record = *published;
    record.primary_source_index = (uint16_t)(record.source_first + record.source_count);
    TEST_CHECK(!library_view_model_source_path(snapshot, &record, &logical_path));
    record = *published;
    record.source_first = (uint32_t)library_snapshot_source_count(snapshot);
    TEST_CHECK(!library_view_model_source_path(snapshot, &record, &logical_path));

    library_service_snapshot_release(snapshot);
    fixture_destroy(&fixture);
}

void test_library_view_launch_quiescence_and_ownership(void)
{
    view_fixture_t fixture;
    library_view_menu_t menu;
    size_t frame;

    reset_handoff();
    fixture_init(&fixture, true);
    (void)published_generation(&fixture, 1U);
    menu = menu_for(&fixture);
    library_view_model_step(&menu);
    TEST_ASSERT(menu.library_view.selected_valid);

    library_service_restart(fixture.service);
    poll_once(&fixture, MENU_MODE_LIBRARY);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));
    menu.actions.enter = true;
    library_view_model_step(&menu);
    menu.actions.enter = false;
    TEST_CHECK(menu.mode == MENU_MODE_LIBRARY);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    TEST_CHECK(menu.library_view.transition == LIBRARY_VIEW_TRANSITION_LAUNCH);
    TEST_CHECK(handoff_path == NULL);

    for (frame = 0U; frame < 3U; ++frame) {
        library_view_model_step(&menu);
        TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
        TEST_CHECK(handoff_path == NULL);
    }
    settle_quiesced(&fixture, MENU_MODE_LIBRARY);
    library_view_model_step(&menu);
    TEST_ASSERT(handoff_path != NULL);
    TEST_CHECK(strcmp(path_get(handoff_path), "sd:/game_one.z64") == 0);
    TEST_CHECK(handoff_return_mode == MENU_MODE_LIBRARY);
    TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_ROM);
    TEST_CHECK(menu.library_view.transition == LIBRARY_VIEW_TRANSITION_SUBMITTED);

    TEST_CHECK(!library_service_coordinate_transition(
        fixture.service, menu.mode, &menu.next_mode));
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    poll_once(&fixture, MENU_MODE_LIBRARY);
    TEST_CHECK(library_service_coordinate_transition(
        fixture.service, menu.mode, &menu.next_mode));
    TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_ROM);

    reset_handoff();
    fixture_destroy(&fixture);
}

void test_library_view_launch_failure_recovery(void)
{
    view_fixture_t fixture;
    library_view_menu_t menu;

    reset_handoff();
    fixture_init(&fixture, true);
    (void)published_generation(&fixture, 1U);
    menu = menu_for(&fixture);
    library_view_model_step(&menu);
    TEST_ASSERT(menu.library_view.selected_valid);

    TEST_ASSERT(path_try_init(&handoff_path, "sd:", "/stale.z64"));
    path_host_test_fail_after(0U);
    menu.actions.enter = true;
    library_view_model_step(&menu);
    menu.actions.enter = false;
    TEST_CHECK(handoff_path == NULL);
    settle_quiesced(&fixture, MENU_MODE_LIBRARY);
    library_view_model_step(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    TEST_CHECK(menu.library_view.transition == LIBRARY_VIEW_TRANSITION_IDLE);
    TEST_CHECK(menu.library_view.message == LIBRARY_VIEW_MESSAGE_OOM);
    TEST_CHECK(handoff_path == NULL);
    TEST_CHECK(path_host_test_live_allocations() == 0U);

    path_host_test_reset();
    reset_handoff();
    fixture_destroy(&fixture);
}

void test_library_view_selection_removed_while_pausing(void)
{
    view_fixture_t fixture;
    library_view_menu_t menu;
    uint32_t old_generation;
    size_t guard;
    bool publication_window = false;

    reset_handoff();
    fixture_init(&fixture, true);
    old_generation = published_generation(&fixture, 1U);
    menu = menu_for(&fixture);
    library_view_model_step(&menu);
    TEST_ASSERT(menu.library_view.selected_valid);

    fixture.fs.directories[0].entry_count = 0U;
    library_service_restart(fixture.service);
    for (guard = 0U; guard < VIEW_GUARD; ++guard) {
        const library_snapshot_t *snapshot;
        poll_once(&fixture, MENU_MODE_LIBRARY);
        snapshot = library_service_snapshot_acquire(fixture.service);
        if (snapshot != NULL) {
            bool old_is_current =
                library_snapshot_generation(snapshot) == old_generation;
            library_service_snapshot_release(snapshot);
            if (old_is_current && fixture.fs.active_dir_handles == 0U &&
                fixture.fs.active_file_handles == 0U &&
                !library_service_is_quiesced(fixture.service)) {
                publication_window = true;
                break;
            }
        }
    }
    TEST_ASSERT(publication_window);

    menu.actions.enter = true;
    library_view_model_step(&menu);
    menu.actions.enter = false;
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    poll_once(&fixture, MENU_MODE_LIBRARY);
    settle_quiesced(&fixture, MENU_MODE_LIBRARY);
    library_view_model_step(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    TEST_CHECK(menu.library_view.transition == LIBRARY_VIEW_TRANSITION_IDLE);
    TEST_CHECK(menu.library_view.message == LIBRARY_VIEW_MESSAGE_REMOVED);
    TEST_CHECK(handoff_path == NULL);

    fixture_destroy(&fixture);
    reset_handoff();
}

void test_library_view_exit_gating_and_balanced_snapshots(void)
{
    view_fixture_t fixture;
    library_view_menu_t menu;
    uint32_t generation;
    size_t frame;

    reset_handoff();
    fixture_init(&fixture, true);
    generation = published_generation(&fixture, 1U);
    menu = menu_for(&fixture);
    library_view_host_snapshot_counts_reset();
    for (frame = 0U; frame < 20U; ++frame)
        library_view_model_step(&menu);
    TEST_CHECK(library_view_host_snapshot_acquire_count() ==
               library_view_host_snapshot_release_count());
    TEST_CHECK(menu.library_view.observed_generation == generation);
    TEST_CHECK(sizeof(menu.library_view) == 84U);

    {
        rom_fingerprint_t selected = menu.library_view.selected_fingerprint;
        library_service_restart(fixture.service);
        generation = published_generation(&fixture, 1U);
        library_view_model_step(&menu);
        TEST_CHECK(menu.library_view.observed_generation == generation);
        TEST_CHECK(rom_fingerprint_equal(
            &menu.library_view.selected_fingerprint, &selected));
    }

    library_service_restart(fixture.service);
    poll_once(&fixture, MENU_MODE_LIBRARY);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));
    menu.actions.back = true;
    library_view_model_step(&menu);
    menu.actions.back = false;
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    TEST_CHECK(menu.library_view.transition == LIBRARY_VIEW_TRANSITION_EXIT);
    for (frame = 0U; frame < 3U; ++frame) {
        library_view_model_step(&menu);
        TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    }
    settle_quiesced(&fixture, MENU_MODE_LIBRARY);
    library_view_model_step(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_HOME);
    TEST_CHECK(menu.library_view.transition == LIBRARY_VIEW_TRANSITION_SUBMITTED);

    menu.next_mode = MENU_MODE_LIBRARY;
    library_service_resume(fixture.service);
    library_service_restart(fixture.service);
    TEST_CHECK(published_generation(&fixture, 1U) > generation);

    fixture_destroy(&fixture);
    reset_handoff();
}
