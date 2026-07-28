#define TEST_NO_MAIN
#include "acutest.h"

#include "menu/library/library_scanner.h"
#include "menu/library/library_snapshot.h"
#include "support/fake_library_fs.h"
#include "support/rom_fixture_builder.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    fake_library_fs_t fake;
    library_fs_t interface;
    library_fs_t base;
    size_t poll_read_bytes;
    size_t largest_read;
    size_t short_limit;
    size_t fail_read_call;
    size_t reads;
    size_t directory_entries_returned;
    uint32_t tick_per_call;
    bool mutate_each_second_open;
    bool mutate_once_on_second_open;
    size_t opens_for_target;
    size_t dir_close_calls;
    size_t file_close_calls;
    size_t fail_dir_closes;
    size_t fail_file_closes;
    size_t truncate_read_call;
    size_t remove_read_call;
    size_t reads_this_open;
    bool truncate_each_open;
} scanner_fs_t;

typedef struct {
    size_t calls;
    size_t live;
    size_t fail_at;
} scanner_alloc_t;

static void *scanner_malloc(void *context, size_t size)
{
    scanner_alloc_t *alloc = context;
    void *result;
    if (alloc->calls++ == alloc->fail_at) return NULL;
    result = malloc(size);
    if (result != NULL) ++alloc->live;
    return result;
}

static void *scanner_calloc(void *context, size_t count, size_t size)
{
    scanner_alloc_t *alloc = context;
    void *result;
    if (alloc->calls++ == alloc->fail_at) return NULL;
    result = calloc(count, size);
    if (result != NULL) ++alloc->live;
    return result;
}

static void scanner_free(void *context, void *pointer)
{
    scanner_alloc_t *alloc = context;
    if (pointer != NULL) --alloc->live;
    free(pointer);
}

static library_allocator_t allocator_for(scanner_alloc_t *alloc)
{
    library_allocator_t result;
    result.context = alloc;
    result.malloc_fn = scanner_malloc;
    result.calloc_fn = scanner_calloc;
    result.free_fn = scanner_free;
    return result;
}

static int shim_dir_open(void *context, const char *path, void **handle,
                         library_dirent_t *first)
{
    scanner_fs_t *shim = context;
    int result = shim->base.dir_open(shim->base.context, path, handle, first);
    if (result == LIBRARY_FS_ENTRY) ++shim->directory_entries_returned;
    return result;
}

static int shim_dir_next(void *context, void *handle, library_dirent_t *next)
{
    scanner_fs_t *shim = context;
    int result = shim->base.dir_next(shim->base.context, handle, next);
    if (result == LIBRARY_FS_ENTRY) ++shim->directory_entries_returned;
    return result;
}

static int shim_dir_close(void *context, void *handle)
{
    scanner_fs_t *shim = context;
    ++shim->dir_close_calls;
    if (shim->fail_dir_closes != 0U) {
        --shim->fail_dir_closes;
        return LIBRARY_FS_ERROR;
    }
    return shim->base.dir_close(shim->base.context, handle);
}

static int shim_file_open(void *context, const char *path, void **handle)
{
    scanner_fs_t *shim = context;
    int result;
    shim->reads_this_open = 0U;
    if (shim->truncate_each_open) shim->fake.files[0].length = 512U;
    if (strcmp(path, "/game.z64") == 0) {
        ++shim->opens_for_target;
        if ((shim->mutate_once_on_second_open && shim->opens_for_target == 2U) ||
            (shim->mutate_each_second_open && (shim->opens_for_target % 2U) == 0U)) {
            shim->fake.files[0].data[200] ^= 0x5aU;
        }
    }
    result = shim->base.file_open_read(shim->base.context, path, handle);
    return result;
}

static int64_t shim_file_read(void *context, void *handle, void *buffer, size_t length)
{
    scanner_fs_t *shim = context;
    int64_t result;
    ++shim->reads;
    ++shim->reads_this_open;
    if (shim->truncate_each_open && shim->reads_this_open == 2U) {
        shim->fake.files[0].length = 64U;
    }
    if (shim->truncate_read_call == shim->reads) shim->fake.files[0].length = 64U;
    if (shim->remove_read_call == shim->reads) shim->fake.file_count = 0U;
    if (shim->fail_read_call != 0U && shim->reads == shim->fail_read_call) return -1;
    if (shim->short_limit != 0U && length > shim->short_limit) length = shim->short_limit;
    shim->poll_read_bytes += length;
    if (length > shim->largest_read) shim->largest_read = length;
    shim->fake.ticks += shim->tick_per_call;
    result = shim->base.file_read(shim->base.context, handle, buffer, length);
    return result;
}

static int shim_file_close(void *context, void *handle)
{
    scanner_fs_t *shim = context;
    ++shim->file_close_calls;
    if (shim->fail_file_closes != 0U) {
        --shim->fail_file_closes;
        return LIBRARY_FS_ERROR;
    }
    return shim->base.file_close(shim->base.context, handle);
}

static int shim_stat(void *context, const char *path, library_stat_t *out)
{
    scanner_fs_t *shim = context;
    return shim->base.stat(shim->base.context, path, out);
}

static uint32_t shim_ticks(void *context)
{
    scanner_fs_t *shim = context;
    return shim->fake.ticks;
}

static void scanner_fs_init(scanner_fs_t *shim)
{
    memset(shim, 0, sizeof(*shim));
    fake_library_fs_init(&shim->fake);
    shim->base = *fake_library_fs_interface(&shim->fake);
    shim->interface.context = shim;
    shim->interface.dir_open = shim_dir_open;
    shim->interface.dir_next = shim_dir_next;
    shim->interface.dir_close = shim_dir_close;
    shim->interface.file_open_read = shim_file_open;
    shim->interface.file_read = shim_file_read;
    shim->interface.file_close = shim_file_close;
    shim->interface.stat = shim_stat;
    shim->interface.ticks_now = shim_ticks;
}

static library_dirent_t entry(const char *name, library_fs_entry_type_t type,
                              size_t size)
{
    library_dirent_t result;
    memset(&result, 0, sizeof(result));
    (void)strncpy(result.basename, name, sizeof(result.basename) - 1U);
    result.type = type;
    result.size = size;
    result.modified_time = 7;
    return result;
}

static void add_rom(scanner_fs_t *shim, const char *path, const char *basename,
                    uint8_t tweak)
{
    uint8_t full[ROM_HEADER_WITH_IPL3_BYTES];
    library_dirent_t root_entry = entry(basename, LIBRARY_FS_ENTRY_FILE, 512U);
    rom_fixture_build_canonical(full);
    full[200] ^= tweak;
    TEST_ASSERT(fake_library_fs_add_directory(&shim->fake, "/", &root_entry, 1U));
    TEST_ASSERT(fake_library_fs_add_file(&shim->fake, path, full, 512U, 7));
}

static library_scanner_t *new_scanner(scanner_fs_t *fs, scanner_alloc_t *tracking)
{
    library_scanner_t *scanner = NULL;
    library_allocator_t allocator = allocator_for(tracking);
    TEST_ASSERT(library_scanner_create(&scanner, &fs->interface,
                                       library_roots_default(), &allocator));
    TEST_ASSERT(library_scanner_start(scanner));
    return scanner;
}

static void poll_to_terminal(library_scanner_t *scanner, uint32_t entries,
                             uint32_t bytes)
{
    library_scan_budget_t budget = { entries, bytes, 1000U };
    size_t guard;
    for (guard = 0U; guard < 10000U; ++guard) {
        library_scanner_state_t state = library_scanner_state(scanner);
        if (state == LIBRARY_SCANNER_COMPLETE || state == LIBRARY_SCANNER_FAILED ||
            state == LIBRARY_SCANNER_QUIESCED || state == LIBRARY_SCANNER_IDLE) return;
        (void)library_scanner_poll(scanner, &budget);
    }
    TEST_CHECK_(false, "scanner did not reach a terminal state");
}

void test_library_scanner_cooperative_traversal_and_validation(void)
{
    scanner_fs_t fs;
    scanner_alloc_t alloc = { 0U, 0U, SIZE_MAX };
    library_scanner_t *scanner;
    library_dirent_t root[3];
    library_dirent_t nested[2];
    uint8_t full[ROM_HEADER_WITH_IPL3_BYTES];
    uint8_t encoded[512];
    library_scan_budget_t budget = { 1U, 37U, 1000U };
    size_t previous_entries;
    size_t guard;

    scanner_fs_init(&fs);
    rom_fixture_build_canonical(full);
    root[0] = entry("folder", LIBRARY_FS_ENTRY_DIRECTORY, 0U);
    root[1] = entry("notes.txt", LIBRARY_FS_ENTRY_FILE, 1U);
    root[2] = entry("bad.rom", LIBRARY_FS_ENTRY_FILE, 64U);
    nested[0] = entry("game.z64", LIBRARY_FS_ENTRY_FILE, 512U);
    nested[1] = entry("other.V64", LIBRARY_FS_ENTRY_FILE, 512U);
    TEST_ASSERT(fake_library_fs_add_directory(&fs.fake, "/", root, 3U));
    TEST_ASSERT(fake_library_fs_add_directory(&fs.fake, "/folder", nested, 2U));
    TEST_ASSERT(fake_library_fs_add_file(&fs.fake, "/bad.rom", (const uint8_t *)"not a rom", 9U, 7));
    TEST_ASSERT(fake_library_fs_add_file(&fs.fake, "/folder/game.z64", full, 512U, 7));
    rom_fixture_encode(ROM_BYTE_ORDER_V64, full, 512U, encoded);
    TEST_ASSERT(fake_library_fs_add_file(&fs.fake, "/folder/other.V64", encoded, 512U, 7));
    scanner = new_scanner(&fs, &alloc);

    for (guard = 0U; guard < 10000U && library_scanner_state(scanner) == LIBRARY_SCANNER_SCANNING; ++guard) {
        previous_entries = fs.directory_entries_returned;
        fs.poll_read_bytes = 0U;
        (void)library_scanner_poll(scanner, &budget);
        TEST_CHECK(fs.directory_entries_returned - previous_entries <= 1U);
        TEST_CHECK(fs.poll_read_bytes <= 37U);
    }
    TEST_CHECK(library_scanner_state(scanner) == LIBRARY_SCANNER_COMPLETE);
    TEST_CHECK(library_scanner_result_count(scanner) == 2U);
    TEST_CHECK(strcmp(library_scanner_result_at(scanner, 0U)->logical_path,
                      "/folder/game.z64") == 0);
    TEST_CHECK(library_scanner_stats(scanner)->candidate_failures == 1U);
    TEST_CHECK(!library_scanner_stats(scanner)->clean);
    TEST_CHECK(fs.largest_read <= 37U);
    TEST_CHECK(fs.fake.active_dir_handles == 0U);
    TEST_CHECK(fs.fake.active_file_handles == 0U);
    library_scanner_destroy(scanner);
    TEST_CHECK(alloc.live == 0U);
}

void test_library_scanner_two_pass_retry_and_replacement(void)
{
    scanner_fs_t fs;
    scanner_alloc_t alloc = { 0U, 0U, SIZE_MAX };
    library_scanner_t *scanner;
    rom_fingerprint_t first;

    scanner_fs_init(&fs);
    add_rom(&fs, "/game.z64", "game.z64", 0U);
    fs.mutate_once_on_second_open = true;
    scanner = new_scanner(&fs, &alloc);
    poll_to_terminal(scanner, 1U, 64U);
    TEST_CHECK(library_scanner_state(scanner) == LIBRARY_SCANNER_COMPLETE);
    TEST_CHECK(library_scanner_result_count(scanner) == 1U);
    TEST_CHECK(fs.opens_for_target == 4U);
    first = library_scanner_result_at(scanner, 0U)->fingerprint;

    fs.mutate_once_on_second_open = false;
    fs.fake.files[0].data[300] ^= 1U;
    TEST_ASSERT(library_scanner_restart(scanner));
    poll_to_terminal(scanner, 1U, 64U);
    TEST_CHECK(library_scanner_generation(scanner) == 2U);
    TEST_CHECK(!rom_fingerprint_equal(&first,
        &library_scanner_result_at(scanner, 0U)->fingerprint));
    library_scanner_destroy(scanner);
    TEST_CHECK(alloc.live == 0U);
}

void test_library_scanner_mutation_failure_and_candidate_io(void)
{
    scanner_fs_t fs;
    scanner_alloc_t alloc = { 0U, 0U, SIZE_MAX };
    library_scanner_t *scanner;

    scanner_fs_init(&fs);
    add_rom(&fs, "/game.z64", "game.z64", 0U);
    fs.mutate_each_second_open = true;
    scanner = new_scanner(&fs, &alloc);
    poll_to_terminal(scanner, 1U, 71U);
    TEST_CHECK(library_scanner_state(scanner) == LIBRARY_SCANNER_COMPLETE);
    TEST_CHECK(library_scanner_result_count(scanner) == 0U);
    TEST_CHECK(library_scanner_stats(scanner)->mutation_failures == 1U);
    TEST_CHECK(library_scanner_stats(scanner)->candidate_failures == 1U);
    TEST_CHECK(!library_scanner_stats(scanner)->clean);
    TEST_CHECK(fs.opens_for_target == 4U);
    library_scanner_destroy(scanner);

    scanner_fs_init(&fs);
    add_rom(&fs, "/game.z64", "game.z64", 0U);
    fs.short_limit = 13U;
    fs.fail_read_call = 3U;
    scanner = new_scanner(&fs, &alloc);
    poll_to_terminal(scanner, 1U, 64U);
    TEST_CHECK(library_scanner_state(scanner) == LIBRARY_SCANNER_COMPLETE);
    TEST_CHECK(library_scanner_stats(scanner)->candidate_failures == 1U);
    TEST_CHECK(fs.fake.active_file_handles == 0U);
    library_scanner_destroy(scanner);
    TEST_CHECK(alloc.live == 0U);
}

void test_library_scanner_pause_resume_cancel_and_ticks(void)
{
    scanner_fs_t fs;
    scanner_alloc_t alloc = { 0U, 0U, SIZE_MAX };
    library_scanner_t *scanner;
    library_scan_budget_t budget = { 1U, 64U, 5U };
    size_t opens_before;

    scanner_fs_init(&fs);
    add_rom(&fs, "/game.z64", "game.z64", 0U);
    fs.tick_per_call = 6U;
    fs.fake.ticks = UINT32_MAX - 2U;
    scanner = new_scanner(&fs, &alloc);
    (void)library_scanner_poll(scanner, &budget);
    (void)library_scanner_poll(scanner, &budget);
    library_scanner_request_pause(scanner);
    (void)library_scanner_poll(scanner, &budget);
    TEST_CHECK(library_scanner_state(scanner) == LIBRARY_SCANNER_QUIESCED);
    TEST_CHECK(fs.fake.active_dir_handles == 0U);
    TEST_CHECK(fs.fake.active_file_handles == 0U);
    opens_before = fs.fake.dir_open_calls;
    TEST_ASSERT(library_scanner_resume(scanner));
    poll_to_terminal(scanner, 1U, 64U);
    TEST_CHECK(library_scanner_result_count(scanner) == 1U);
    TEST_CHECK(fs.fake.dir_open_calls > opens_before);

    TEST_ASSERT(library_scanner_restart(scanner));
    (void)library_scanner_poll(scanner, &budget);
    library_scanner_request_cancel(scanner);
    (void)library_scanner_poll(scanner, &budget);
    TEST_CHECK(library_scanner_state(scanner) == LIBRARY_SCANNER_IDLE);
    TEST_CHECK(library_scanner_result_count(scanner) == 0U);
    TEST_CHECK(fs.fake.active_dir_handles == 0U);
    TEST_CHECK(fs.fake.active_file_handles == 0U);
    library_scanner_destroy(scanner);
    TEST_CHECK(alloc.live == 0U);
}

void test_library_scanner_fatal_directory_and_capacity(void)
{
    scanner_fs_t fs;
    scanner_alloc_t alloc = { 0U, 0U, SIZE_MAX };
    library_scanner_t *scanner;
    library_dirent_t root;
    library_roots_t roots;
    library_allocator_t allocator;
    char long_root[LIBRARY_ROOT_PATH_CAPACITY];

    scanner_fs_init(&fs);
    TEST_ASSERT(fake_library_fs_add_directory(&fs.fake, "/", NULL, 0U));
    fake_library_fs_fail_dir_open(&fs.fake, "/");
    scanner = new_scanner(&fs, &alloc);
    poll_to_terminal(scanner, 1U, 64U);
    TEST_CHECK(library_scanner_state(scanner) == LIBRARY_SCANNER_FAILED);
    TEST_CHECK(library_scanner_stats(scanner)->fatal_errors == 1U);
    library_scanner_destroy(scanner);

    scanner_fs_init(&fs);
    memset(&roots, 0, sizeof(roots));
    memset(long_root, 'a', sizeof(long_root));
    long_root[0] = '/';
    long_root[500] = '\0';
    roots.count = 1U;
    memcpy(roots.paths[0], long_root, 501U);
    root = entry("aaaaaaaaaaaaaaa.z64", LIBRARY_FS_ENTRY_FILE, 512U);
    TEST_ASSERT(fake_library_fs_add_directory(&fs.fake, long_root, &root, 1U));
    allocator = allocator_for(&alloc);
    TEST_ASSERT(library_scanner_create(&scanner, &fs.interface, &roots, &allocator));
    TEST_ASSERT(library_scanner_start(scanner));
    poll_to_terminal(scanner, 1U, 64U);
    TEST_CHECK(library_scanner_state(scanner) == LIBRARY_SCANNER_FAILED);
    TEST_CHECK(library_scanner_stats(scanner)->capacity_failures == 1U);
    library_scanner_destroy(scanner);
    TEST_CHECK(alloc.live == 0U);
}

void test_library_scanner_oom_every_allocation(void)
{
    size_t fail_at;
    size_t allocation_count = 0U;

    for (fail_at = 0U; fail_at < 16U; ++fail_at) {
        scanner_fs_t fs;
        scanner_alloc_t alloc = { 0U, 0U, fail_at };
        library_allocator_t allocator = allocator_for(&alloc);
        library_scanner_t *scanner = NULL;
        bool created;
        scanner_fs_init(&fs);
        created = library_scanner_create(&scanner, &fs.interface,
                                         library_roots_default(), &allocator);
        if (created) {
            allocation_count = alloc.calls;
            library_scanner_destroy(scanner);
            TEST_CHECK(alloc.live == 0U);
            break;
        }
        TEST_CHECK(scanner == NULL);
        TEST_CHECK(alloc.live == 0U);
    }
    TEST_CHECK(allocation_count > 1U);
    TEST_CHECK(fail_at == allocation_count);
}

void test_library_scanner_removal_truncation_and_close_retry(void)
{
    scanner_fs_t fs;
    scanner_alloc_t alloc = { 0U, 0U, SIZE_MAX };
    library_scanner_t *scanner;
    library_scan_budget_t budget = { 1U, 64U, 1000U };
    size_t calls;

    scanner_fs_init(&fs);
    add_rom(&fs, "/game.z64", "game.z64", 0U);
    fs.remove_read_call = 2U;
    scanner = new_scanner(&fs, &alloc);
    poll_to_terminal(scanner, 1U, 64U);
    TEST_CHECK(library_scanner_result_count(scanner) == 0U);
    TEST_CHECK(library_scanner_stats(scanner)->mutation_failures == 1U);
    library_scanner_destroy(scanner);

    scanner_fs_init(&fs);
    add_rom(&fs, "/game.z64", "game.z64", 0U);
    fs.truncate_each_open = true;
    scanner = new_scanner(&fs, &alloc);
    poll_to_terminal(scanner, 1U, 64U);
    TEST_CHECK(library_scanner_result_count(scanner) == 0U);
    TEST_CHECK(library_scanner_stats(scanner)->mutation_failures == 1U);
    TEST_CHECK(fs.opens_for_target == 2U);
    library_scanner_destroy(scanner);

    scanner_fs_init(&fs);
    add_rom(&fs, "/game.z64", "game.z64", 0U);
    scanner = new_scanner(&fs, &alloc);
    (void)library_scanner_poll(scanner, &budget);
    fs.fail_dir_closes = 1U;
    fs.fail_file_closes = 1U;
    library_scanner_request_pause(scanner);
    TEST_CHECK(library_scanner_poll(scanner, &budget) == LIBRARY_SCAN_YIELDED);
    TEST_CHECK(library_scanner_state(scanner) == LIBRARY_SCANNER_PAUSE_REQUESTED);
    TEST_CHECK(fs.fake.active_dir_handles == 1U);
    TEST_CHECK(fs.fake.active_file_handles == 1U);
    TEST_CHECK(library_scanner_poll(scanner, &budget) == LIBRARY_SCAN_QUIESCED_RESULT);
    calls = fs.directory_entries_returned + fs.reads + fs.dir_close_calls +
            fs.file_close_calls + fs.fake.dir_open_calls + fs.fake.file_open_calls;
    TEST_CHECK(library_scanner_poll(scanner, &budget) == LIBRARY_SCAN_QUIESCED_RESULT);
    TEST_CHECK(calls == fs.directory_entries_returned + fs.reads +
                       fs.dir_close_calls + fs.file_close_calls +
                       fs.fake.dir_open_calls + fs.fake.file_open_calls);
    TEST_ASSERT(library_scanner_resume(scanner));
    poll_to_terminal(scanner, 1U, 64U);
    TEST_CHECK(fs.fake.active_dir_handles == 0U);
    TEST_CHECK(fs.fake.active_file_handles == 0U);
    TEST_CHECK(fs.fake.file_open_calls == fs.fake.backend_file_close_calls);
    TEST_CHECK(fs.fake.dir_open_calls == fs.fake.backend_dir_close_calls);
    library_scanner_destroy(scanner);
    TEST_CHECK(alloc.live == 0U);
}

void test_library_scanner_invalid_header_close_retry(void)
{
    scanner_fs_t fs;
    scanner_alloc_t alloc = { 0U, 0U, SIZE_MAX };
    library_scanner_t *scanner;
    library_dirent_t root_entry;
    library_scan_budget_t budget = { 1U, 64U, 1000U };
    uint8_t invalid[ROM_HEADER_METADATA_BYTES] = { 0U };
    size_t guard;

    scanner_fs_init(&fs);
    root_entry = entry("bad.z64", LIBRARY_FS_ENTRY_FILE, sizeof(invalid));
    TEST_ASSERT(fake_library_fs_add_directory(&fs.fake, "/", &root_entry, 1U));
    TEST_ASSERT(fake_library_fs_add_file(&fs.fake, "/bad.z64", invalid,
                                         sizeof(invalid), 7));
    fs.fail_file_closes = 1U;
    scanner = new_scanner(&fs, &alloc);

    for (guard = 0U; guard < 32U &&
         library_scanner_state(scanner) == LIBRARY_SCANNER_SCANNING; ++guard) {
        (void)library_scanner_poll(scanner, &budget);
    }
    TEST_CHECK(guard < 32U);
    TEST_CHECK(library_scanner_state(scanner) == LIBRARY_SCANNER_COMPLETE);
    TEST_CHECK(library_scanner_result_count(scanner) == 0U);
    TEST_CHECK(library_scanner_stats(scanner)->candidate_failures == 1U);
    TEST_CHECK(fs.file_close_calls == 2U);
    TEST_CHECK(fs.fake.backend_file_close_calls == 1U);
    TEST_CHECK(fs.fake.active_file_handles == 0U);
    library_scanner_destroy(scanner);
    TEST_CHECK(alloc.live == 0U);
}

void test_library_scanner_destroy_retries_directory_close(void)
{
    scanner_fs_t fs;
    scanner_alloc_t alloc = { 0U, 0U, SIZE_MAX };
    library_scanner_t *scanner;
    library_dirent_t root_entry;
    library_scan_budget_t budget = { 1U, 0U, 1000U };

    scanner_fs_init(&fs);
    root_entry = entry("notes.txt", LIBRARY_FS_ENTRY_FILE, 1U);
    TEST_ASSERT(fake_library_fs_add_directory(&fs.fake, "/", &root_entry, 1U));
    scanner = new_scanner(&fs, &alloc);
    (void)library_scanner_poll(scanner, &budget);
    TEST_CHECK(fs.fake.active_dir_handles == 1U);

    fs.fail_dir_closes = 1U;
    TEST_CHECK(!library_scanner_destroy(scanner));
    TEST_CHECK(fs.fake.active_dir_handles == 1U);
    TEST_CHECK(fs.dir_close_calls == 1U);
    TEST_CHECK(alloc.live != 0U);

    TEST_CHECK(library_scanner_destroy(scanner));
    TEST_CHECK(fs.fake.active_dir_handles == 0U);
    TEST_CHECK(fs.dir_close_calls == 2U);
    TEST_CHECK(fs.fake.backend_dir_close_calls == 1U);
    TEST_CHECK(alloc.live == 0U);
}

void test_library_scanner_tick_expiry_stops_reads(void)
{
    scanner_fs_t fs;
    scanner_alloc_t alloc = { 0U, 0U, SIZE_MAX };
    library_scanner_t *scanner;
    library_scan_budget_t budget = { 1U, 4096U, 5U };
    size_t before;
    scanner_fs_init(&fs);
    add_rom(&fs, "/game.z64", "game.z64", 0U);
    fs.tick_per_call = 6U;
    scanner = new_scanner(&fs, &alloc);
    before = fs.reads;
    (void)library_scanner_poll(scanner, &budget);
    TEST_CHECK(fs.reads - before == 1U);
    before = fs.reads;
    (void)library_scanner_poll(scanner, &budget);
    TEST_CHECK(fs.reads - before <= 1U);
    library_scanner_request_cancel(scanner);
    (void)library_scanner_poll(scanner, &budget);
    library_scanner_destroy(scanner);
    TEST_CHECK(alloc.live == 0U);
}

typedef enum {
    GENERATED_TOP,
    GENERATED_DEPTH,
    GENERATED_QUEUE,
    GENERATED_ARENA,
    GENERATED_RECORD_UNIQUE,
    GENERATED_SOURCES
} generated_mode_t;

typedef struct {
    library_fs_t interface;
    generated_mode_t mode;
    size_t count;
    size_t offset;
    size_t file_offset;
    size_t file_index;
    bool active;
    uint8_t rom[ROM_HEADER_WITH_IPL3_BYTES];
} generated_fs_t;

static void generated_entry(generated_fs_t *fs, size_t index,
                            library_dirent_t *out)
{
    size_t name_length;
    memset(out, 0, sizeof(*out));
    out->type = LIBRARY_FS_ENTRY_DIRECTORY;
    if (fs->mode == GENERATED_RECORD_UNIQUE || fs->mode == GENERATED_SOURCES) {
        out->type = LIBRARY_FS_ENTRY_FILE;
        out->size = sizeof(fs->rom);
        (void)snprintf(out->basename, sizeof(out->basename), "r%03lu.z64",
                       (unsigned long)index);
        return;
    }
    if (fs->mode == GENERATED_ARENA && fs->count != 1U) {
        name_length = sizeof(out->basename) - 2U;
        memset(out->basename, (int)('a' + (int)(index % 26U)), name_length);
        out->basename[name_length] = '\0';
    } else {
        (void)snprintf(out->basename, sizeof(out->basename), "d%03lu",
                       (unsigned long)index);
    }
}

static int generated_dir_open(void *context, const char *path, void **handle,
                              library_dirent_t *first)
{
    generated_fs_t *fs = context;
    size_t slash_count = 0U;
    const char *at;
    size_t count = 0U;
    for (at = path; *at != '\0'; ++at) if (*at == '/') ++slash_count;
    if (fs->mode == GENERATED_TOP && strcmp(path, "/") == 0) count = 33U;
    if (fs->mode == GENERATED_RECORD_UNIQUE && strcmp(path, "/") == 0) count = 257U;
    if (fs->mode == GENERATED_SOURCES && strcmp(path, "/") == 0) count = 512U;
    if (fs->mode == GENERATED_DEPTH && slash_count <= 9U) count = 1U;
    if ((fs->mode == GENERATED_QUEUE || fs->mode == GENERATED_ARENA) &&
        strcmp(path, "/") == 0) count = 1U;
    if (fs->mode == GENERATED_QUEUE && strcmp(path, "/d000") == 0) count = 512U;
    if (fs->mode == GENERATED_ARENA && strcmp(path, "/d000") == 0) count = 140U;
    if (count == 0U) {
        *handle = NULL;
        return LIBRARY_FS_EOF;
    }
    fs->count = count;
    fs->offset = 1U;
    fs->active = true;
    *handle = fs;
    generated_entry(fs, 0U, first);
    return LIBRARY_FS_ENTRY;
}

static int generated_dir_next(void *context, void *handle,
                              library_dirent_t *next)
{
    generated_fs_t *fs = context;
    (void)handle;
    if (!fs->active) return LIBRARY_FS_ERROR;
    if (fs->offset < fs->count) {
        generated_entry(fs, fs->offset++, next);
        return LIBRARY_FS_ENTRY;
    }
    fs->active = false;
    return LIBRARY_FS_EOF;
}

static int generated_dir_close(void *context, void *handle)
{
    generated_fs_t *fs = context;
    (void)handle;
    fs->active = false;
    return LIBRARY_FS_ENTRY;
}

static int generated_file_open(void *context, const char *path, void **handle)
{
    generated_fs_t *fs = context;
    unsigned long parsed = 0U;
    (void)sscanf(path, "/r%lu.z64", &parsed);
    fs->file_index = (size_t)parsed;
    fs->file_offset = 0U;
    *handle = &fs->file_offset;
    return LIBRARY_FS_ENTRY;
}

static int64_t generated_file_read(void *context, void *handle, void *data,
                                   size_t length)
{
    generated_fs_t *fs = context;
    size_t remaining = sizeof(fs->rom) - fs->file_offset;
    size_t amount = length < remaining ? length : remaining;
    (void)handle;
    memcpy(data, fs->rom + fs->file_offset, amount);
    if (fs->mode == GENERATED_RECORD_UNIQUE) {
        size_t first = fs->file_offset;
        size_t end = first + amount;
        if (first <= 200U && 200U < end)
            ((uint8_t *)data)[200U - first] ^= (uint8_t)fs->file_index;
        if (first <= 201U && 201U < end)
            ((uint8_t *)data)[201U - first] ^= (uint8_t)(fs->file_index >> 8U);
    }
    fs->file_offset += amount;
    return (int64_t)amount;
}

static int generated_file_close(void *context, void *handle)
{
    (void)context; (void)handle; return LIBRARY_FS_ENTRY;
}

static int generated_stat(void *context, const char *path, library_stat_t *out)
{
    generated_fs_t *fs = context;
    (void)path;
    memset(out, 0, sizeof(*out));
    if (fs->mode != GENERATED_RECORD_UNIQUE && fs->mode != GENERATED_SOURCES)
        return LIBRARY_FS_ERROR;
    out->type = LIBRARY_FS_ENTRY_FILE;
    out->size = sizeof(fs->rom);
    out->modified_time = 9;
    return LIBRARY_FS_ENTRY;
}

static void generated_init(generated_fs_t *fs, generated_mode_t mode)
{
    memset(fs, 0, sizeof(*fs));
    fs->mode = mode;
    rom_fixture_build_canonical(fs->rom);
    fs->interface.context = fs;
    fs->interface.dir_open = generated_dir_open;
    fs->interface.dir_next = generated_dir_next;
    fs->interface.dir_close = generated_dir_close;
    fs->interface.file_open_read = generated_file_open;
    fs->interface.file_read = generated_file_read;
    fs->interface.file_close = generated_file_close;
    fs->interface.stat = generated_stat;
}

void test_library_scanner_directory_capacity_classes(void)
{
    generated_mode_t mode;
    for (mode = GENERATED_TOP; mode <= GENERATED_RECORD_UNIQUE;
         mode = (generated_mode_t)(mode + 1)) {
        generated_fs_t fs;
        library_scanner_t *scanner = NULL;
        generated_init(&fs, mode);
        TEST_ASSERT(library_scanner_create(&scanner, &fs.interface,
                                           library_roots_default(), NULL));
        TEST_ASSERT(library_scanner_start(scanner));
        poll_to_terminal(scanner, 1024U, 4096U);
        TEST_CHECK(library_scanner_state(scanner) == LIBRARY_SCANNER_FAILED);
        TEST_CHECK(library_scanner_stats(scanner)->capacity_failures == 1U);
        library_scanner_destroy(scanner);
    }
}

void test_library_scanner_512_compatible_sources(void)
{
    generated_fs_t fs;
    library_scanner_t *scanner = NULL;
    library_snapshot_builder_t *builder = NULL;
    library_snapshot_t *snapshot = NULL;
    generated_init(&fs, GENERATED_SOURCES);
    TEST_ASSERT(library_scanner_create(&scanner, &fs.interface,
                                       library_roots_default(), NULL));
    TEST_ASSERT(library_scanner_start(scanner));
    poll_to_terminal(scanner, 1024U, 4096U);
    TEST_CHECK(library_scanner_state(scanner) == LIBRARY_SCANNER_COMPLETE);
    TEST_CHECK(library_scanner_result_count(scanner) == 512U);
    TEST_CHECK(library_scanner_stats(scanner)->clean);
    TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
    TEST_ASSERT(library_snapshot_builder_add_scanner(builder, scanner));
    TEST_ASSERT(library_snapshot_builder_freeze(builder, 1U, &snapshot));
    TEST_CHECK(library_snapshot_record_count(snapshot) == 1U);
    TEST_CHECK(library_snapshot_source_count(snapshot) == 512U);
    TEST_CHECK(strcmp(library_snapshot_source_path(snapshot, 0U),
                      "/r000.z64") == 0);
    TEST_CHECK(strcmp(library_snapshot_source_path(snapshot, 511U),
                      "/r511.z64") == 0);
    TEST_CHECK(library_scanner_result_at(scanner, 0U) == NULL);
    TEST_CHECK(!library_scanner_restart(scanner));
    library_snapshot_release(snapshot);
    library_snapshot_builder_destroy(builder);
    library_scanner_destroy(scanner);
}
