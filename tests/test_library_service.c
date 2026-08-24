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
#include "support/fake_library_fs.h"
#include "support/rom_fixture_builder.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SERVICE_GUARD 20000U
#define SERVICE_LIVE_HEAP_CAP (224U * 1024U)
#define TARGET_SERVICE_BYTES 640U
#define TARGET_ADAPTER_BYTES 14088U
#define TARGET_SCANNER_BYTES 111960U
#define TARGET_SCANNER_BACKED_SNAPSHOT_BYTES 98432U
#define TARGET_SCANNER_BACKED_REPLACEMENT_BYTES 221896U

typedef struct allocation_header {
    size_t size;
} allocation_header_t;

typedef struct {
    size_t calls;
    size_t fail_at;
    size_t live_blocks;
    size_t live_bytes;
    size_t peak_bytes;
    library_service_t *service;
    bool freed_while_active;
    bool quiesced_during_allocation;
} service_alloc_t;

typedef struct {
    fake_library_fs_t fake;
    library_fs_t interface;
    library_fs_t base;
    library_service_t *service;
    size_t callback_depth;
    bool quiesced_in_callback;
    size_t entries_this_poll;
    size_t bytes_this_poll;
    size_t fail_dir_closes;
    size_t fail_file_closes;
    size_t dir_close_attempts;
    size_t file_close_attempts;
} service_fs_t;

typedef struct {
    service_fs_t fs;
    service_alloc_t alloc;
    library_allocator_t allocator;
    library_scan_budget_t budget;
    library_service_t *service;
} service_fixture_t;

static void *service_malloc(void *context, size_t size)
{
    service_alloc_t *state = context;
    allocation_header_t *header;
    if (state->service != NULL && library_service_is_quiesced(state->service))
        state->quiesced_during_allocation = true;
    if (state->calls++ == state->fail_at) return NULL;
    header = malloc(sizeof(*header) + size);
    if (header == NULL) return NULL;
    header->size = size;
    ++state->live_blocks;
    state->live_bytes += size;
    if (state->live_bytes > state->peak_bytes) state->peak_bytes = state->live_bytes;
    return header + 1;
}

static void *service_calloc(void *context, size_t count, size_t size)
{
    size_t total;
    void *result;
    if (size != 0U && count > SIZE_MAX / size) return NULL;
    total = count * size;
    result = service_malloc(context, total);
    if (result != NULL) memset(result, 0, total);
    return result;
}

static void service_free_alloc(void *context, void *pointer)
{
    service_alloc_t *state = context;
    allocation_header_t *header;
    if (pointer == NULL) return;
    if (state->service != NULL && !library_service_is_quiesced(state->service))
        state->freed_while_active = true;
    header = (allocation_header_t *)pointer - 1;
    --state->live_blocks;
    state->live_bytes -= header->size;
    free(header);
}

static void callback_enter(service_fs_t *fs)
{
    ++fs->callback_depth;
    if (fs->service != NULL && library_service_is_quiesced(fs->service))
        fs->quiesced_in_callback = true;
}

static void callback_leave(service_fs_t *fs)
{
    --fs->callback_depth;
}

static int wrapped_dir_open(void *context, const char *path, void **handle,
                            library_dirent_t *first)
{
    service_fs_t *fs = context;
    int result;
    callback_enter(fs);
    result = fs->base.dir_open(fs->base.context, path, handle, first);
    if (result == LIBRARY_FS_ENTRY) ++fs->entries_this_poll;
    callback_leave(fs);
    return result;
}

static int wrapped_dir_next(void *context, void *handle, library_dirent_t *next)
{
    service_fs_t *fs = context;
    int result;
    callback_enter(fs);
    result = fs->base.dir_next(fs->base.context, handle, next);
    if (result == LIBRARY_FS_ENTRY) ++fs->entries_this_poll;
    callback_leave(fs);
    return result;
}

static int wrapped_dir_close(void *context, void *handle)
{
    service_fs_t *fs = context;
    int result;
    callback_enter(fs);
    ++fs->dir_close_attempts;
    if (fs->fail_dir_closes != 0U) {
        --fs->fail_dir_closes;
        result = LIBRARY_FS_ERROR;
    } else {
        result = fs->base.dir_close(fs->base.context, handle);
    }
    callback_leave(fs);
    return result;
}

static int wrapped_file_open(void *context, const char *path, void **handle)
{
    service_fs_t *fs = context;
    int result;
    callback_enter(fs);
    result = fs->base.file_open_read(fs->base.context, path, handle);
    callback_leave(fs);
    return result;
}

static int64_t wrapped_file_read(void *context, void *handle, void *buffer,
                                 size_t length)
{
    service_fs_t *fs = context;
    int64_t result;
    callback_enter(fs);
    result = fs->base.file_read(fs->base.context, handle, buffer, length);
    if (result > 0) fs->bytes_this_poll += (size_t)result;
    callback_leave(fs);
    return result;
}

static int wrapped_file_close(void *context, void *handle)
{
    service_fs_t *fs = context;
    int result;
    callback_enter(fs);
    ++fs->file_close_attempts;
    if (fs->fail_file_closes != 0U) {
        --fs->fail_file_closes;
        result = LIBRARY_FS_ERROR;
    } else {
        result = fs->base.file_close(fs->base.context, handle);
    }
    callback_leave(fs);
    return result;
}

static int wrapped_stat(void *context, const char *path, library_stat_t *out)
{
    service_fs_t *fs = context;
    int result;
    callback_enter(fs);
    result = fs->base.stat(fs->base.context, path, out);
    callback_leave(fs);
    return result;
}

static uint32_t wrapped_ticks(void *context)
{
    service_fs_t *fs = context;
    uint32_t result;
    callback_enter(fs);
    result = fs->base.ticks_now(fs->base.context);
    callback_leave(fs);
    return result;
}

static library_dirent_t service_entry(const char *name,
                                      library_fs_entry_type_t type,
                                      size_t size)
{
    library_dirent_t result;
    memset(&result, 0, sizeof(result));
    (void)strncpy(result.basename, name, sizeof(result.basename) - 1U);
    result.type = type;
    result.size = size;
    result.modified_time = 9;
    return result;
}

static void fixture_init(service_fixture_t *fixture, bool with_rom)
{
    library_service_config_t config;
    library_dirent_t root[2];
    uint8_t rom[ROM_HEADER_WITH_IPL3_BYTES];

    memset(fixture, 0, sizeof(*fixture));
    fixture->alloc.fail_at = SIZE_MAX;
    fixture->allocator.context = &fixture->alloc;
    fixture->allocator.malloc_fn = service_malloc;
    fixture->allocator.calloc_fn = service_calloc;
    fixture->allocator.free_fn = service_free_alloc;
    fixture->budget.max_directory_entries = 1U;
    fixture->budget.max_read_bytes = 64U;
    fixture->budget.max_ticks = 10U;

    fake_library_fs_init(&fixture->fs.fake);
    fixture->fs.base = *fake_library_fs_interface(&fixture->fs.fake);
    fixture->fs.interface.context = &fixture->fs;
    fixture->fs.interface.dir_open = wrapped_dir_open;
    fixture->fs.interface.dir_next = wrapped_dir_next;
    fixture->fs.interface.dir_close = wrapped_dir_close;
    fixture->fs.interface.file_open_read = wrapped_file_open;
    fixture->fs.interface.file_read = wrapped_file_read;
    fixture->fs.interface.file_close = wrapped_file_close;
    fixture->fs.interface.stat = wrapped_stat;
    fixture->fs.interface.ticks_now = wrapped_ticks;

    if (with_rom) {
        root[0] = service_entry("game.z64", LIBRARY_FS_ENTRY_FILE, 512U);
        root[1] = service_entry("notes.txt", LIBRARY_FS_ENTRY_FILE, 4U);
        TEST_ASSERT(fake_library_fs_add_directory(&fixture->fs.fake, "/", root, 2U));
        rom_fixture_build_canonical(rom);
        TEST_ASSERT(fake_library_fs_add_file(&fixture->fs.fake, "/game.z64",
                                             rom, 512U, 9));
    } else {
        TEST_ASSERT(fake_library_fs_add_directory(&fixture->fs.fake, "/", NULL, 0U));
    }

    memset(&config, 0, sizeof(config));
    config.fs = &fixture->fs.interface;
    config.roots = library_roots_default();
    config.budget = fixture->budget;
    config.allocator = &fixture->allocator;
    config.storage_prefix = "sd:/";
    TEST_ASSERT(library_service_init(&fixture->service, &config));
    fixture->alloc.service = fixture->service;
    fixture->fs.service = fixture->service;
}

static void begin_poll(service_fixture_t *fixture, menu_mode_t mode)
{
    fixture->fs.entries_this_poll = 0U;
    fixture->fs.bytes_this_poll = 0U;
    library_service_poll(fixture->service, mode);
    TEST_CHECK(fixture->fs.entries_this_poll <= fixture->budget.max_directory_entries);
    TEST_CHECK(fixture->fs.bytes_this_poll <= fixture->budget.max_read_bytes);
    TEST_CHECK(fixture->fs.callback_depth == 0U);
}

static void poll_until_quiesced(service_fixture_t *fixture, menu_mode_t mode)
{
    size_t guard;
    for (guard = 0U; guard < SERVICE_GUARD; ++guard) {
        if (library_service_is_quiesced(fixture->service)) return;
        begin_poll(fixture, mode);
    }
    TEST_CHECK_(false, "service did not quiesce within the deterministic guard");
}

static const library_snapshot_t *poll_until_published(service_fixture_t *fixture)
{
    const library_snapshot_t *snapshot;
    size_t guard;
    for (guard = 0U; guard < SERVICE_GUARD; ++guard) {
        snapshot = library_service_snapshot_acquire(fixture->service);
        if (snapshot != NULL && library_snapshot_status(snapshot) == LIBRARY_SNAPSHOT_FRESH)
            return snapshot;
        if (snapshot != NULL) library_service_snapshot_release(snapshot);
        begin_poll(fixture, MENU_MODE_HOME);
    }
    TEST_CHECK_(false, "service did not publish within the deterministic guard");
    return NULL;
}

static void fixture_destroy(service_fixture_t *fixture)
{
    if (!library_service_is_quiesced(fixture->service)) {
        library_service_request_cancel(fixture->service);
        poll_until_quiesced(fixture, MENU_MODE_BOOT);
    }
    library_service_free(fixture->service);
    fixture->service = NULL;
    fixture->alloc.service = NULL;
    fixture->fs.service = NULL;
    TEST_CHECK(fixture->fs.fake.active_dir_handles == 0U);
    TEST_CHECK(fixture->fs.fake.active_file_handles == 0U);
    TEST_CHECK(fixture->alloc.live_blocks == 0U);
    TEST_CHECK(fixture->alloc.live_bytes == 0U);
}

void test_library_service_init_is_lazy_and_safe_modes_start(void)
{
    service_fixture_t fixture;
    const library_snapshot_t *snapshot;
    fixture_init(&fixture, true);

    TEST_CHECK(library_service_is_quiesced(fixture.service));
    TEST_CHECK(fixture.fs.fake.dir_open_calls == 0U);
    TEST_CHECK(fixture.fs.fake.file_open_calls == 0U);
    snapshot = library_service_snapshot_acquire(fixture.service);
    TEST_CHECK(snapshot == NULL);

    begin_poll(&fixture, MENU_MODE_HOME);
    TEST_CHECK(fixture.fs.fake.dir_open_calls != 0U);
    library_service_request_cancel(fixture.service);
    poll_until_quiesced(&fixture, MENU_MODE_BROWSER);
    library_service_restart(fixture.service);
    begin_poll(&fixture, MENU_MODE_LIBRARY);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));
    fixture_destroy(&fixture);
}

void test_library_service_unsafe_modes_never_start_work(void)
{
    service_fixture_t fixture;
    menu_mode_t mode;
    fixture_init(&fixture, true);
    for (mode = MENU_MODE_NONE; mode <= MENU_MODE_EXTRACT_FILE;
         mode = (menu_mode_t)(mode + 1)) {
        if (mode == MENU_MODE_HOME || mode == MENU_MODE_LIBRARY) continue;
        begin_poll(&fixture, mode);
        TEST_CHECK_(fixture.fs.fake.dir_open_calls == 0U,
                    "unsafe mode %d started discovery", (int)mode);
        TEST_CHECK(fixture.fs.fake.file_open_calls == 0U);
    }
    fixture_destroy(&fixture);
}

void test_library_service_one_bounded_unit_per_poll(void)
{
    service_fixture_t fixture;
    size_t guard;
    fixture_init(&fixture, true);
    for (guard = 0U; guard < SERVICE_GUARD; ++guard) {
        const library_snapshot_t *snapshot;
        begin_poll(&fixture, MENU_MODE_HOME);
        snapshot = library_service_snapshot_acquire(fixture.service);
        if (snapshot != NULL) {
            library_service_snapshot_release(snapshot);
            break;
        }
    }
    TEST_CHECK(guard < SERVICE_GUARD);
    TEST_CHECK(!fixture.fs.quiesced_in_callback);
    TEST_CHECK(fixture.alloc.peak_bytes <= SERVICE_LIVE_HEAP_CAP);
    TEST_CHECK(library_service_allocation_size(fixture.service) <= SERVICE_LIVE_HEAP_CAP);
    TEST_CHECK(TARGET_SCANNER_BACKED_SNAPSHOT_BYTES + TARGET_SCANNER_BYTES +
                   TARGET_ADAPTER_BYTES + TARGET_SERVICE_BYTES == 225120U);
    TEST_CHECK(TARGET_SCANNER_BACKED_SNAPSHOT_BYTES + TARGET_SCANNER_BYTES +
                   TARGET_ADAPTER_BYTES + TARGET_SERVICE_BYTES <=
               SERVICE_LIVE_HEAP_CAP);
    TEST_CHECK(TARGET_SCANNER_BACKED_REPLACEMENT_BYTES +
                   TARGET_SERVICE_BYTES == 222536U);
    TEST_CHECK(TARGET_SCANNER_BACKED_REPLACEMENT_BYTES +
                   TARGET_SERVICE_BYTES <= SERVICE_LIVE_HEAP_CAP);
    fixture_destroy(&fixture);
}

void test_library_service_pause_quiesce_resume_and_retry_close(void)
{
    service_fixture_t fixture;
    size_t guard;
    fixture_init(&fixture, true);
    for (guard = 0U; guard < SERVICE_GUARD && fixture.fs.fake.active_dir_handles == 0U; ++guard)
        begin_poll(&fixture, MENU_MODE_HOME);
    TEST_ASSERT(fixture.fs.fake.active_dir_handles != 0U);
    fixture.fs.fail_dir_closes = 2U;
    library_service_request_pause(fixture.service);
    begin_poll(&fixture, MENU_MODE_BROWSER);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));
    TEST_CHECK(fixture.fs.fake.active_dir_handles != 0U);
    poll_until_quiesced(&fixture, MENU_MODE_FAULT);
    TEST_CHECK(fixture.fs.dir_close_attempts >= 3U);
    TEST_CHECK(fixture.fs.fake.active_dir_handles == 0U);

    library_service_resume(fixture.service);
    begin_poll(&fixture, MENU_MODE_HOME);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));
    fixture_destroy(&fixture);
}

void test_library_service_cancel_quiesce_restart_and_file_close(void)
{
    service_fixture_t fixture;
    size_t guard;
    fixture_init(&fixture, true);
    for (guard = 0U; guard < SERVICE_GUARD && fixture.fs.fake.active_file_handles == 0U; ++guard)
        begin_poll(&fixture, MENU_MODE_HOME);
    TEST_ASSERT(fixture.fs.fake.active_file_handles != 0U);
    fixture.fs.fail_file_closes = 2U;
    library_service_request_cancel(fixture.service);
    begin_poll(&fixture, MENU_MODE_LOAD_ROM);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));
    poll_until_quiesced(&fixture, MENU_MODE_BOOT);
    TEST_CHECK(fixture.fs.file_close_attempts >= 3U);
    TEST_CHECK(library_service_snapshot_acquire(fixture.service) == NULL);

    library_service_restart(fixture.service);
    begin_poll(&fixture, MENU_MODE_HOME);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));
    fixture_destroy(&fixture);
}

void test_library_service_pause_after_completion_drains_publication(void)
{
    service_fixture_t fixture;
    const library_snapshot_t *snapshot;

    fixture_init(&fixture, false);
    begin_poll(&fixture, MENU_MODE_HOME);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));
    library_service_request_pause(fixture.service);
    poll_until_quiesced(&fixture, MENU_MODE_BROWSER);

    snapshot = library_service_snapshot_acquire(fixture.service);
    TEST_ASSERT(snapshot != NULL);
    TEST_CHECK(library_snapshot_status(snapshot) == LIBRARY_SNAPSHOT_FRESH);
    TEST_CHECK(library_snapshot_record_count(snapshot) == 0U);
    library_service_snapshot_release(snapshot);
    fixture_destroy(&fixture);
}

void test_library_service_complete_publication_is_atomic(void)
{
    service_fixture_t fixture;
    const library_snapshot_t *snapshot;
    const library_snapshot_t *probe;
    size_t guard;
    fixture_init(&fixture, true);
    for (guard = 0U; guard < SERVICE_GUARD; ++guard) {
        probe = library_service_snapshot_acquire(fixture.service);
        if (probe != NULL) {
            TEST_CHECK(library_snapshot_status(probe) == LIBRARY_SNAPSHOT_FRESH);
            TEST_CHECK(library_snapshot_record_count(probe) == 1U);
            library_service_snapshot_release(probe);
            break;
        }
        begin_poll(&fixture, MENU_MODE_HOME);
    }
    TEST_ASSERT(guard < SERVICE_GUARD);
    snapshot = library_service_snapshot_acquire(fixture.service);
    TEST_ASSERT(snapshot != NULL);
    TEST_CHECK(library_snapshot_generation(snapshot) == 1U);
    TEST_CHECK(library_snapshot_record_count(snapshot) == 1U);
    TEST_CHECK(library_snapshot_source_count(snapshot) == 1U);
    library_service_snapshot_release(snapshot);
    TEST_CHECK(!fixture.alloc.quiesced_during_allocation);
    fixture_destroy(&fixture);
}

void test_library_service_failure_retains_old_snapshot(void)
{
    service_fixture_t fixture;
    const library_snapshot_t *old;
    const library_snapshot_t *current;
    uint32_t generation;
    size_t guard;
    fixture_init(&fixture, true);
    old = poll_until_published(&fixture);
    TEST_ASSERT(old != NULL);
    generation = library_snapshot_generation(old);

    library_service_restart(fixture.service);
    fake_library_fs_fail_file_read(&fixture.fs.fake, true);
    for (guard = 0U; guard < SERVICE_GUARD; ++guard) {
        begin_poll(&fixture, MENU_MODE_HOME);
        if (library_service_is_quiesced(fixture.service)) break;
    }
    TEST_CHECK(guard < SERVICE_GUARD);
    current = library_service_snapshot_acquire(fixture.service);
    TEST_ASSERT(current != NULL);
    TEST_CHECK(library_snapshot_generation(old) == generation);
    TEST_CHECK(library_snapshot_record_count(old) == 1U);
    TEST_CHECK(library_snapshot_generation(current) == generation);
    TEST_CHECK(library_snapshot_record_count(current) == 1U);
    TEST_CHECK(library_snapshot_status(current) == LIBRARY_SNAPSHOT_FAILED_STALE);
    library_service_snapshot_release(current);
    library_service_snapshot_release(old);
    fixture_destroy(&fixture);
}

void test_library_service_acquisition_and_scanner_transfer_lifetimes(void)
{
    service_fixture_t fixture;
    const library_snapshot_t *first;
    const library_snapshot_t *second;
    uint32_t generation;
    size_t guard;
    fixture_init(&fixture, true);
    first = poll_until_published(&fixture);
    TEST_ASSERT(first != NULL);
    generation = library_snapshot_generation(first);
    second = library_service_snapshot_acquire(fixture.service);
    TEST_ASSERT(second == first);
    library_service_snapshot_release(second);

    library_service_restart(fixture.service);
    for (guard = 0U; guard < SERVICE_GUARD; ++guard) {
        second = library_service_snapshot_acquire(fixture.service);
        TEST_ASSERT(second != NULL);
        if (library_snapshot_generation(second) > generation) {
            library_service_snapshot_release(second);
            break;
        }
        library_service_snapshot_release(second);
        begin_poll(&fixture, MENU_MODE_HOME);
    }
    TEST_CHECK(guard < SERVICE_GUARD);
    TEST_CHECK(library_snapshot_generation(first) == generation);
    TEST_CHECK(library_snapshot_record_count(first) == 1U);
    library_service_snapshot_release(first);
    fixture_destroy(&fixture);
}

void test_library_service_reader_backpressure_retries_restart(void)
{
    service_fixture_t fixture;
    const library_snapshot_t *first;
    const library_snapshot_t *second = NULL;
    const library_snapshot_t *third = NULL;
    uint32_t first_generation;
    uint32_t second_generation = 0U;
    size_t opens;
    size_t guard;

    fixture_init(&fixture, true);
    first = poll_until_published(&fixture);
    TEST_ASSERT(first != NULL);
    first_generation = library_snapshot_generation(first);

    library_service_restart(fixture.service);
    for (guard = 0U; guard < SERVICE_GUARD; ++guard) {
        second = library_service_snapshot_acquire(fixture.service);
        TEST_ASSERT(second != NULL);
        if (library_snapshot_generation(second) > first_generation &&
            library_snapshot_status(second) == LIBRARY_SNAPSHOT_FRESH) {
            second_generation = library_snapshot_generation(second);
            break;
        }
        library_service_snapshot_release(second);
        second = NULL;
        begin_poll(&fixture, MENU_MODE_HOME);
    }
    TEST_ASSERT(guard < SERVICE_GUARD);
    TEST_ASSERT(second != NULL);

    library_service_restart(fixture.service);
    opens = fixture.fs.fake.dir_open_calls;
    begin_poll(&fixture, MENU_MODE_HOME);
    TEST_CHECK(fixture.fs.fake.dir_open_calls == opens);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));

    library_service_snapshot_release(second);
    library_service_snapshot_release(first);
    for (guard = 0U; guard < SERVICE_GUARD; ++guard) {
        third = library_service_snapshot_acquire(fixture.service);
        if (third != NULL &&
            library_snapshot_generation(third) > second_generation &&
            library_snapshot_status(third) == LIBRARY_SNAPSHOT_FRESH) {
            break;
        }
        if (third != NULL) library_service_snapshot_release(third);
        third = NULL;
        begin_poll(&fixture, MENU_MODE_HOME);
    }
    TEST_ASSERT(guard < SERVICE_GUARD);
    TEST_ASSERT(third != NULL);
    library_service_snapshot_release(third);
    TEST_CHECK(fixture.alloc.peak_bytes <= SERVICE_LIVE_HEAP_CAP);
    fixture_destroy(&fixture);
}

static void exercise_transition(service_fixture_t *fixture, menu_mode_t current,
                                menu_mode_t destination)
{
    menu_mode_t requested = destination;
    bool ready;
    ready = library_service_coordinate_transition(fixture->service, current,
                                                  &requested);
    TEST_CHECK(!ready);
    TEST_CHECK(requested == current);
    TEST_CHECK(!library_service_is_quiesced(fixture->service));

    requested = (destination == MENU_MODE_BROWSER) ? MENU_MODE_FILE_INFO
                                                   : MENU_MODE_BROWSER;
    while (!library_service_is_quiesced(fixture->service)) {
        begin_poll(fixture, destination);
        ready = library_service_coordinate_transition(fixture->service, current,
                                                      &requested);
        if (!ready) TEST_CHECK(requested == current);
    }
    ready = library_service_coordinate_transition(fixture->service, current,
                                                  &requested);
    TEST_CHECK(ready);
    TEST_CHECK(requested == destination);
}

static void exercise_stale_release_is_invalidated_on_restart(void)
{
    service_fixture_t fixture;
    menu_mode_t requested;
    bool ready;
    size_t guard;

    fixture_init(&fixture, true);
    for (guard = 0U; guard < SERVICE_GUARD &&
         fixture.fs.fake.active_dir_handles == 0U; ++guard)
        begin_poll(&fixture, MENU_MODE_LIBRARY);
    TEST_ASSERT(fixture.fs.fake.active_dir_handles != 0U);

    requested = MENU_MODE_LOAD_ROM;
    ready = library_service_coordinate_transition(fixture.service,
                                                  MENU_MODE_LIBRARY,
                                                  &requested);
    TEST_CHECK(!ready);
    TEST_CHECK(requested == MENU_MODE_LIBRARY);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));

    for (guard = 0U; guard < SERVICE_GUARD &&
         !library_service_is_quiesced(fixture.service); ++guard) {
        begin_poll(&fixture, MENU_MODE_LOAD_ROM);
        requested = MENU_MODE_LOAD_ROM;
        ready = library_service_coordinate_transition(fixture.service,
                                                      MENU_MODE_LIBRARY,
                                                      &requested);
        if (!ready) TEST_CHECK(requested == MENU_MODE_LIBRARY);
    }
    TEST_ASSERT(guard < SERVICE_GUARD);
    requested = MENU_MODE_LOAD_ROM;
    ready = library_service_coordinate_transition(fixture.service,
                                                  MENU_MODE_LIBRARY,
                                                  &requested);
    TEST_CHECK(ready);
    TEST_CHECK(requested == MENU_MODE_LOAD_ROM);

    library_service_request_cancel(fixture.service);
    poll_until_quiesced(&fixture, MENU_MODE_BOOT);
    library_service_restart(fixture.service);
    for (guard = 0U; guard < SERVICE_GUARD &&
         fixture.fs.fake.active_dir_handles == 0U; ++guard)
        begin_poll(&fixture, MENU_MODE_LIBRARY);
    TEST_ASSERT(fixture.fs.fake.active_dir_handles != 0U);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));

    requested = MENU_MODE_LOAD_ROM;
    ready = library_service_coordinate_transition(fixture.service,
                                                  MENU_MODE_LIBRARY,
                                                  &requested);
    TEST_CHECK(!ready);
    TEST_CHECK(requested == MENU_MODE_LIBRARY);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));

    for (guard = 0U; guard < SERVICE_GUARD &&
         !library_service_is_quiesced(fixture.service); ++guard) {
        requested = MENU_MODE_LOAD_ROM;
        ready = library_service_coordinate_transition(fixture.service,
                                                      MENU_MODE_LIBRARY,
                                                      &requested);
        TEST_CHECK(!ready);
        TEST_CHECK(requested == MENU_MODE_LIBRARY);
        begin_poll(&fixture, MENU_MODE_LOAD_ROM);
    }
    TEST_ASSERT(guard < SERVICE_GUARD);
    requested = MENU_MODE_LOAD_ROM;
    ready = library_service_coordinate_transition(fixture.service,
                                                  MENU_MODE_LIBRARY,
                                                  &requested);
    TEST_CHECK(ready);
    TEST_CHECK(requested == MENU_MODE_LOAD_ROM);

    requested = MENU_MODE_LOAD_ROM;
    ready = library_service_coordinate_transition(fixture.service,
                                                  MENU_MODE_LOAD_ROM,
                                                  &requested);
    TEST_CHECK(ready);
    TEST_CHECK(requested == MENU_MODE_LOAD_ROM);
    fixture_destroy(&fixture);
}

void test_library_service_transition_coordinator_defers_all_safe_exits(void)
{
    service_fixture_t fixture;
    size_t guard;

    exercise_stale_release_is_invalidated_on_restart();

    fixture_init(&fixture, true);
    for (guard = 0U; guard < SERVICE_GUARD && fixture.fs.fake.active_dir_handles == 0U; ++guard)
        begin_poll(&fixture, MENU_MODE_HOME);
    TEST_ASSERT(fixture.fs.fake.active_dir_handles != 0U);
    exercise_transition(&fixture, MENU_MODE_HOME, MENU_MODE_BROWSER);
    fixture_destroy(&fixture);

    fixture_init(&fixture, true);
    for (guard = 0U; guard < SERVICE_GUARD && fixture.fs.fake.active_dir_handles == 0U; ++guard)
        begin_poll(&fixture, MENU_MODE_HOME);
    TEST_ASSERT(fixture.fs.fake.active_dir_handles != 0U);
    exercise_transition(&fixture, MENU_MODE_HOME, MENU_MODE_LIBRARY);
    fixture_destroy(&fixture);

    fixture_init(&fixture, true);
    for (guard = 0U; guard < SERVICE_GUARD && fixture.fs.fake.active_dir_handles == 0U; ++guard)
        begin_poll(&fixture, MENU_MODE_LIBRARY);
    TEST_ASSERT(fixture.fs.fake.active_dir_handles != 0U);
    exercise_transition(&fixture, MENU_MODE_LIBRARY, MENU_MODE_LOAD_ROM);
    fixture_destroy(&fixture);

    fixture_init(&fixture, true);
    for (guard = 0U; guard < SERVICE_GUARD && fixture.fs.fake.active_dir_handles == 0U; ++guard)
        begin_poll(&fixture, MENU_MODE_LIBRARY);
    TEST_ASSERT(fixture.fs.fake.active_dir_handles != 0U);
    exercise_transition(&fixture, MENU_MODE_LIBRARY, MENU_MODE_HOME);
    fixture_destroy(&fixture);
}

void test_library_service_free_is_fail_closed_until_quiesced(void)
{
    service_fixture_t fixture;
    size_t live_before;
    fixture_init(&fixture, true);
    begin_poll(&fixture, MENU_MODE_HOME);
    TEST_ASSERT(!library_service_is_quiesced(fixture.service));
    live_before = fixture.alloc.live_blocks;
    library_service_free(fixture.service);
    TEST_CHECK(fixture.alloc.live_blocks == live_before);
    TEST_CHECK(!fixture.alloc.freed_while_active);

    library_service_request_cancel(fixture.service);
    poll_until_quiesced(&fixture, MENU_MODE_BOOT);
    fixture_destroy(&fixture);
}

void test_library_service_init_and_owned_allocation_failures_are_atomic(void)
{
    size_t fail_at;
    bool observed_success = false;
    for (fail_at = 0U; fail_at < 64U; ++fail_at) {
        fake_library_fs_t fake;
        library_service_t *service = (library_service_t *)(uintptr_t)1U;
        library_service_config_t config;
        service_alloc_t state;
        library_allocator_t allocator;
        bool result;
        memset(&state, 0, sizeof(state));
        state.fail_at = fail_at;
        allocator.context = &state;
        allocator.malloc_fn = service_malloc;
        allocator.calloc_fn = service_calloc;
        allocator.free_fn = service_free_alloc;
        fake_library_fs_init(&fake);
        memset(&config, 0, sizeof(config));
        config.fs = fake_library_fs_interface(&fake);
        config.roots = library_roots_default();
        config.budget.max_directory_entries = 1U;
        config.budget.max_read_bytes = 64U;
        config.budget.max_ticks = 10U;
        config.allocator = &allocator;
        config.storage_prefix = "sd:/";
        result = library_service_init(&service, &config);
        if (result) {
            observed_success = true;
            state.service = service;
            TEST_CHECK(library_service_is_quiesced(service));
            library_service_free(service);
            state.service = NULL;
            TEST_CHECK(state.live_blocks == 0U);
            break;
        }
        TEST_CHECK(service == NULL);
        TEST_CHECK(state.live_blocks == 0U);
        TEST_CHECK(state.live_bytes == 0U);
    }
    TEST_CHECK(observed_success);
}

void test_library_service_every_refresh_oom_retains_publication(void)
{
    service_fixture_t fixture;
    const library_snapshot_t *old;
    size_t fail_offset;
    size_t baseline_calls;
    bool reached_success = false;
    fixture_init(&fixture, true);
    old = poll_until_published(&fixture);
    TEST_ASSERT(old != NULL);
    baseline_calls = fixture.alloc.calls;

    for (fail_offset = 0U; fail_offset < 64U; ++fail_offset) {
        const library_snapshot_t *current;
        size_t guard;
        fixture.alloc.fail_at = baseline_calls + fail_offset;
        library_service_restart(fixture.service);
        for (guard = 0U; guard < SERVICE_GUARD; ++guard) {
            begin_poll(&fixture, MENU_MODE_HOME);
            if (library_service_is_quiesced(fixture.service)) break;
        }
        TEST_CHECK(guard < SERVICE_GUARD);
        current = library_service_snapshot_acquire(fixture.service);
        TEST_ASSERT(current != NULL);
        TEST_CHECK(library_snapshot_record_count(old) == 1U);
        if (library_snapshot_generation(current) > library_snapshot_generation(old)) {
            reached_success = true;
            library_service_snapshot_release(current);
            break;
        }
        TEST_CHECK(library_snapshot_generation(current) == library_snapshot_generation(old));
        library_service_snapshot_release(current);
        fixture.alloc.fail_at = SIZE_MAX;
        baseline_calls = fixture.alloc.calls;
        library_service_restart(fixture.service);
        library_service_request_cancel(fixture.service);
        poll_until_quiesced(&fixture, MENU_MODE_BOOT);
    }
    TEST_CHECK(reached_success);
    TEST_CHECK(fixture.alloc.peak_bytes <= SERVICE_LIVE_HEAP_CAP);
    TEST_CHECK(library_service_allocation_size(fixture.service) <= SERVICE_LIVE_HEAP_CAP);
    library_service_snapshot_release(old);
    fixture.alloc.fail_at = SIZE_MAX;
    fixture_destroy(&fixture);
}

#if LAYER1_FOUNDATION_FOCUSED_TESTS
typedef struct {
    char bytes[LIBRARY_METRICS_TERMINAL_RECORD_BYTES];
    size_t length;
    size_t calls;
} metrics_writer_capture_t;

static int metrics_capture_writer(void *context, const char *bytes,
                                  size_t length)
{
    metrics_writer_capture_t *capture = context;
    if (capture == NULL || bytes == NULL || length > sizeof(capture->bytes))
        return -1;
    memcpy(capture->bytes, bytes, length);
    capture->length = length;
    ++capture->calls;
    return (int)length;
}

void test_layer1_metrics_terminal_emission(void)
{
    metrics_writer_capture_t capture;
    library_metrics_snapshot_t snapshot;

    memset(&capture, 0, sizeof(capture));
    library_metrics_reset();
    library_metrics_record_usb_opportunity(10U);
    library_metrics_scan_terminal(NULL, LIBRARY_SCANNER_FAILED, true, 20U);
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(snapshot.terminal_pending);
    TEST_CHECK(!snapshot.terminal_eligible);
    TEST_CHECK(snapshot.terminal_event_mask == LIBRARY_METRICS_TERMINAL_SCAN);
    TEST_CHECK(!library_metrics_emit_pending(metrics_capture_writer, &capture));
    TEST_CHECK(capture.calls == 0U);

    library_metrics_record_usb_opportunity(30U);
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(snapshot.terminal_eligible);
    TEST_ASSERT(library_metrics_emit_pending(metrics_capture_writer, &capture));
    TEST_CHECK(capture.calls == 1U);
    TEST_CHECK(capture.length != 0U);
    TEST_CHECK(capture.length < sizeof(capture.bytes));
    TEST_CHECK(memcmp(capture.bytes, "[AURORA64 L1]", 13U) == 0);
    TEST_CHECK(!library_metrics_emit_pending(metrics_capture_writer, &capture));
    TEST_CHECK(capture.calls == 1U);
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(!snapshot.terminal_pending);
    TEST_CHECK(!snapshot.terminal_eligible);
    TEST_CHECK(snapshot.terminal_event_mask == 0U);
}

void test_layer1_metrics_post_poll_summary(void)
{
    service_fixture_t fixture;
    library_scanner_poll_stats_t poll_stats = { 2U, 64U };
    library_metrics_snapshot_t snapshot;

    fixture_init(&fixture, false);
    library_metrics_record_scanner_poll(100U, 107U, &poll_stats);
    library_metrics_record_scanner_poll(107U, 112U, &poll_stats);
    library_metrics_set_scanner_failure(LIBRARY_SCANNER_FAILURE_FILESYSTEM);
    library_service_layer1_summary(fixture.service, &snapshot);
    TEST_CHECK(snapshot.poll_count == 2U);
    TEST_CHECK(snapshot.last_poll_entries == 2U);
    TEST_CHECK(snapshot.max_poll_entries == 2U);
    TEST_CHECK(snapshot.total_entries == 4U);
    TEST_CHECK(snapshot.last_poll_read_bytes == 64U);
    TEST_CHECK(snapshot.max_poll_read_bytes == 64U);
    TEST_CHECK(snapshot.total_read_bytes == 128U);
    TEST_CHECK(snapshot.max_poll_ticks == 7U);
    TEST_CHECK(snapshot.first_scanner_failure ==
               LIBRARY_SCANNER_FAILURE_FILESYSTEM);
    fixture_destroy(&fixture);
}

void test_layer1_metrics_scan_duration_excludes_pause(void)
{
    library_metrics_snapshot_t snapshot;

    library_metrics_reset();
    library_metrics_scan_attempt(4U, 100U);
    library_metrics_scan_observe(120U);
    library_metrics_pause_request(120U);
    library_metrics_pause_complete(1120U, true);
    library_metrics_scan_observe(1140U);
    library_metrics_scan_terminal(NULL, LIBRARY_SCANNER_COMPLETE, true, 1160U);
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(snapshot.scan_duration_valid);
    TEST_CHECK(snapshot.scan_duration_ticks == 60U);
    TEST_CHECK(snapshot.pause_duration_valid);
    TEST_CHECK(snapshot.pause_ticks == 1000U);
}

void test_layer1_metrics_pause_cancel_latency(void)
{
    library_metrics_snapshot_t snapshot;

    library_metrics_reset();
    library_metrics_pause_request(100U);
    library_metrics_pause_complete(150U, true);
    library_metrics_cancel_request(200U);
    library_metrics_cancel_complete(260U, true);
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(snapshot.pause_duration_valid);
    TEST_CHECK(snapshot.pause_ticks == 50U);
    TEST_CHECK(snapshot.cancel_duration_valid);
    TEST_CHECK(snapshot.cancel_ticks == 60U);
    TEST_CHECK(snapshot.terminal_event_mask ==
               (LIBRARY_METRICS_TERMINAL_PAUSE |
                LIBRARY_METRICS_TERMINAL_CANCEL));
    TEST_CHECK(!snapshot.timing_invalid);

    library_metrics_reset();
    library_metrics_pause_request(0U);
    library_metrics_pause_complete((uint32_t)INT32_MAX + 1U, true);
    library_metrics_cancel_request(0U);
    library_metrics_cancel_complete((uint32_t)INT32_MAX + 1U, true);
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(!snapshot.pause_duration_valid);
    TEST_CHECK(snapshot.pause_ticks == 0U);
    TEST_CHECK(!snapshot.cancel_duration_valid);
    TEST_CHECK(snapshot.cancel_ticks == 0U);
    TEST_CHECK(snapshot.timing_invalid);
}

void test_layer1_metrics_lifecycle_reset(void)
{
    library_metrics_snapshot_t snapshot;

    library_metrics_reset();
    library_metrics_pause_request(10U);
    library_metrics_lifecycle_reset();
    library_metrics_pause_complete(20U, true);
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(!snapshot.pause_duration_valid);
    TEST_CHECK(snapshot.pause_ticks == 0U);
    TEST_CHECK(!snapshot.terminal_pending);

    library_metrics_cancel_request(30U);
    library_metrics_lifecycle_reset();
    library_metrics_cancel_complete(40U, true);
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(!snapshot.cancel_duration_valid);
    TEST_CHECK(snapshot.cancel_ticks == 0U);
    TEST_CHECK(!snapshot.terminal_pending);
    TEST_CHECK(snapshot.terminal_event_mask == 0U);
}

void test_layer1_metrics_aggregate_faults_and_reset(void)
{
    library_scanner_poll_stats_t first = { 3U, 17U };
    library_scanner_poll_stats_t second = { 5U, 29U };
    library_metrics_snapshot_t snapshot;

    library_metrics_reset();
    library_metrics_record_scanner_poll(
        0U, (uint32_t)INT32_MAX + 1U, &first);
    library_metrics_record_scanner_poll(10U, 15U, &second);
    library_metrics_set_scanner_failure(LIBRARY_SCANNER_FAILURE_FILESYSTEM);
    library_metrics_set_scanner_failure(LIBRARY_SCANNER_FAILURE_MUTATION);
    library_metrics_set_builder_failure(
        LIBRARY_SNAPSHOT_BUILD_FAILURE_ALLOCATION);
    library_metrics_set_builder_failure(LIBRARY_SNAPSHOT_BUILD_FAILURE_FREEZE);
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(snapshot.poll_count == 2U);
    TEST_CHECK(snapshot.total_entries == 8U);
    TEST_CHECK(snapshot.total_read_bytes == 46U);
    TEST_CHECK(snapshot.max_poll_entries == 5U);
    TEST_CHECK(snapshot.max_poll_read_bytes == 29U);
    TEST_CHECK(snapshot.max_poll_ticks == 5U);
    TEST_CHECK(snapshot.timing_invalid);
    TEST_CHECK(snapshot.first_scanner_failure ==
               LIBRARY_SCANNER_FAILURE_FILESYSTEM);
    TEST_CHECK(snapshot.first_builder_failure ==
               LIBRARY_SNAPSHOT_BUILD_FAILURE_ALLOCATION);

    library_metrics_reset();
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(snapshot.poll_count == 0U);
    TEST_CHECK(snapshot.total_entries == 0U);
    TEST_CHECK(snapshot.total_read_bytes == 0U);
    TEST_CHECK(!snapshot.timing_invalid);
    TEST_CHECK(!snapshot.allocation_invalid);
    TEST_CHECK(!snapshot.counter_overflow);
    TEST_CHECK(snapshot.first_scanner_failure ==
               LIBRARY_SCANNER_FAILURE_NONE);
    TEST_CHECK(snapshot.first_builder_failure ==
               LIBRARY_SNAPSHOT_BUILD_FAILURE_NONE);
}
#endif
