#define TEST_NO_MAIN
#include "acutest.h"
#include "menu/library/library_fs.h"

#include <dir.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STUB_PATH_CAPACITY 600U
#define TEST_POOL_SIZE 16U

typedef struct {
    size_t first_calls;
    size_t next_calls;
    size_t close_calls;
    int next_result;
    int close_result;
    int close_errno;
    bool bad_name;
    char first_path[STUB_PATH_CAPACITY];
    char next_path[STUB_PATH_CAPACITY];
    char close_path[STUB_PATH_CAPACITY];
} dir_stub_t;

static dir_stub_t stub;
static uint64_t stub_ticks;
static size_t stub_file_open_calls;

FILE *library_fs_host_test_fopen(const char *path, const char *mode)
{
    ++stub_file_open_calls;
    return fopen(path, mode);
}

static void stub_copy_path(char *out, const char *path)
{
    size_t index;
    memset(out, 0, STUB_PATH_CAPACITY);
    for (index = 0U; index + 1U < STUB_PATH_CAPACITY && path[index] != '\0'; ++index) {
        out[index] = path[index];
    }
}

static void stub_entry(dir_t *dir, const char *name, uint32_t cookie)
{
    memset(dir, 0, sizeof(*dir));
    if (stub.bad_name) {
        memset(dir->d_name, 'x', sizeof(dir->d_name));
    } else {
        memcpy(dir->d_name, name, strlen(name) + 1U);
    }
    dir->d_type = DT_REG;
    dir->d_size = 42;
    dir->d_cookie = cookie;
}

int dir_findfirst(const char *const path, dir_t *dir)
{
    ++stub.first_calls;
    stub_copy_path(stub.first_path, path);
    if (strstr(path, "/empty") != NULL) {
        errno = ENOENT;
        return -1;
    }
    if (strstr(path, "/open-error") != NULL) {
        errno = EACCES;
        return -2;
    }
    stub_entry(dir, "first.z64", 1U);
    return 0;
}

int dir_findnext(const char *const path, dir_t *dir)
{
    ++stub.next_calls;
    stub_copy_path(stub.next_path, path);
    if (stub.next_result == 0) {
        stub_entry(dir, "next.z64", 2U);
        return 0;
    }
    errno = (stub.next_result == -1) ? ENOENT : EIO;
    return stub.next_result;
}

int dir_findclose(const char *const path, dir_t *dir)
{
    (void)dir;
    ++stub.close_calls;
    stub_copy_path(stub.close_path, path);
    errno = stub.close_errno;
    return stub.close_result;
}

uint64_t get_ticks(void)
{
    return stub_ticks;
}

static void reset_stub(void)
{
    memset(&stub, 0, sizeof(stub));
    stub_file_open_calls = 0U;
    stub.next_result = -1;
    stub.close_errno = EIO;
}

static bool all_zero(const void *object, size_t size)
{
    static const unsigned char zeros[sizeof(library_dirent_t)] = { 0U };
    TEST_ASSERT(size <= sizeof(zeros));
    return memcmp(object, zeros, size) == 0;
}

void test_libdragon_adapter_paths_outputs_and_errors(void)
{
    library_fs_t fs;
    library_dirent_t entry;
    library_stat_t stat_value;
    void *handle = (void *)(uintptr_t)99U;
    char prefix[] = "sd:/";
    char logical[] = "/roms/game";
    char overlong[512U];

    reset_stub();
    memset(&fs, 0xa5, sizeof(fs));
    TEST_ASSERT(library_fs_libdragon_init(&fs, prefix));
    TEST_CHECK(fs.dir_open(fs.context, logical, &handle, &entry) == LIBRARY_FS_ENTRY);
    TEST_CHECK(strcmp(stub.first_path, "sd:/roms/game") == 0);
    prefix[0] = 'X';
    logical[1] = 'X';
    TEST_CHECK(fs.dir_next(fs.context, handle, &entry) == LIBRARY_FS_EOF);
    TEST_CHECK(strcmp(stub.next_path, "sd:/roms/game") == 0);
    TEST_CHECK(strcmp(stub.close_path, "sd:/roms/game") == 0);
    TEST_CHECK(errno == ENOENT);
    TEST_CHECK(fs.dir_next(fs.context, handle, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(fs.dir_close(fs.context, handle) == LIBRARY_FS_EOF);
    TEST_CHECK(stub.close_calls == 1U);

    handle = (void *)(uintptr_t)99U;
    memset(&entry, 0xa5, sizeof(entry));
    TEST_CHECK(fs.dir_open(fs.context, "/empty", &handle, &entry) == LIBRARY_FS_EOF);
    TEST_CHECK(handle == NULL);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    memset(&entry, 0xa5, sizeof(entry));
    TEST_CHECK(fs.dir_open(fs.context, "/open-error", &handle, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(handle == NULL);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    memset(&entry, 0xa5, sizeof(entry));
    TEST_CHECK(fs.dir_open(fs.context, NULL, &handle, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(fs.dir_open(fs.context, "/", NULL, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));

    memset(overlong, 'a', sizeof(overlong));
    overlong[0] = '/';
    memset(&entry, 0xa5, sizeof(entry));
    TEST_CHECK(fs.dir_open(fs.context, overlong, &handle, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(fs.dir_open(fs.context, "relative", &handle, &entry) == LIBRARY_FS_ERROR);

    memset(&stat_value, 0xa5, sizeof(stat_value));
    TEST_CHECK(fs.stat(fs.context, "relative", &stat_value) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&stat_value, sizeof(stat_value)));
    memset(&stat_value, 0xa5, sizeof(stat_value));
    TEST_CHECK(fs.stat(fs.context, NULL, &stat_value) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&stat_value, sizeof(stat_value)));
    stub_ticks = UINT64_C(0x12345678abcdef01);
    TEST_CHECK(fs.ticks_now(fs.context) == UINT32_C(0xabcdef01));
    library_fs_libdragon_deinit(&fs);
    TEST_CHECK(all_zero(&fs, sizeof(fs)));

    reset_stub();
    TEST_ASSERT(library_fs_libdragon_init(&fs, "sd:/"));
    TEST_CHECK(fs.dir_open(fs.context, "/", &handle, &entry) == LIBRARY_FS_ENTRY);
    TEST_CHECK(strcmp(stub.first_path, "sd:/") == 0);
    TEST_CHECK(fs.dir_close(fs.context, handle) == LIBRARY_FS_ENTRY);
    library_fs_libdragon_deinit(&fs);
}

void test_libdragon_adapter_lifecycle_pool_and_stale_tokens(void)
{
    library_fs_t fs;
    library_dirent_t entry;
    void *handles[TEST_POOL_SIZE];
    void *stale;
    size_t index;
    size_t calls;

    reset_stub();
    TEST_ASSERT(library_fs_libdragon_init(&fs, "sd:/"));
    for (index = 0U; index < 80U; ++index) {
        void *handle = NULL;
        TEST_ASSERT(fs.dir_open(fs.context, "/games", &handle, &entry) == LIBRARY_FS_ENTRY);
        if (index == 0U) stale = handle;
        TEST_CHECK(fs.dir_close(fs.context, handle) == LIBRARY_FS_ENTRY);
        TEST_CHECK(fs.dir_close(fs.context, handle) == LIBRARY_FS_EOF);
    }
    calls = stub.close_calls;
    TEST_CHECK(fs.dir_next(fs.context, stale, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(fs.dir_close(fs.context, stale) == LIBRARY_FS_ERROR);
    TEST_CHECK(stub.close_calls == calls);
    TEST_CHECK(fs.dir_close(fs.context, (void *)(uintptr_t)UINT64_C(0xfeedbeef)) == LIBRARY_FS_ERROR);

    for (index = 0U; index < TEST_POOL_SIZE; ++index) {
        TEST_ASSERT(fs.dir_open(fs.context, "/full", &handles[index], &entry) == LIBRARY_FS_ENTRY);
    }
    calls = stub.first_calls;
    TEST_CHECK(fs.dir_open(fs.context, "/full", &stale, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(stale == NULL);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(stub.first_calls == calls);
    for (index = 0U; index < TEST_POOL_SIZE; ++index) {
        TEST_CHECK(fs.dir_close(fs.context, handles[index]) == LIBRARY_FS_ENTRY);
    }
    library_fs_libdragon_deinit(&fs);
}

void test_libdragon_adapter_next_errors_cleanup_and_deinit(void)
{
    library_fs_t fs;
    library_dirent_t entry;
    void *handles[3];
    size_t index;

    reset_stub();
    TEST_ASSERT(library_fs_libdragon_init(&fs, "rom:/"));
    TEST_ASSERT(fs.dir_open(fs.context, "/games", &handles[0], &entry) == LIBRARY_FS_ENTRY);
    stub.next_result = -2;
    memset(&entry, 0xa5, sizeof(entry));
    TEST_CHECK(fs.dir_next(fs.context, handles[0], &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(stub.close_calls == 0U);
    TEST_CHECK(fs.dir_close(fs.context, handles[0]) == LIBRARY_FS_ENTRY);
    TEST_CHECK(stub.close_calls == 1U);

    stub.bad_name = true;
    memset(&entry, 0xa5, sizeof(entry));
    TEST_CHECK(fs.dir_open(fs.context, "/bad-name", &handles[0], &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(stub.close_calls == 2U);
    TEST_CHECK(errno == EOVERFLOW);
    stub.bad_name = false;

    for (index = 0U; index < 3U; ++index) {
        TEST_ASSERT(fs.dir_open(fs.context, "/active", &handles[index], &entry) == LIBRARY_FS_ENTRY);
    }
    library_fs_libdragon_deinit(&fs);
    TEST_CHECK(stub.close_calls == 5U);
}

void test_libdragon_adapter_close_failure_retries(void)
{
    library_fs_t fs;
    library_dirent_t entry;
    void *handle = NULL;
    void *replacement = NULL;
    size_t next_calls;
    size_t close_calls;

    reset_stub();
    TEST_ASSERT(library_fs_libdragon_init(&fs, "sd:/"));
    TEST_ASSERT(fs.dir_open(fs.context, "/eof-retry", &handle, &entry) ==
                LIBRARY_FS_ENTRY);
    stub.close_result = -2;
    stub.close_errno = EBUSY;
    memset(&entry, 0xa5, sizeof(entry));
    errno = 0;
    TEST_CHECK(fs.dir_next(fs.context, handle, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(errno == EBUSY);
    TEST_CHECK(stub.next_calls == 1U);
    TEST_CHECK(stub.close_calls == 1U);

    next_calls = stub.next_calls;
    close_calls = stub.close_calls;
    memset(&entry, 0xa5, sizeof(entry));
    TEST_CHECK(fs.dir_next(fs.context, handle, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(stub.next_calls == next_calls);
    TEST_CHECK(stub.close_calls == close_calls);

    stub.close_result = 0;
    TEST_CHECK(fs.dir_close(fs.context, handle) == LIBRARY_FS_ENTRY);
    TEST_CHECK(stub.close_calls == close_calls + 1U);
    TEST_CHECK(fs.dir_close(fs.context, handle) == LIBRARY_FS_EOF);
    TEST_CHECK(stub.close_calls == close_calls + 1U);
    TEST_CHECK(fs.dir_next(fs.context, handle, &entry) == LIBRARY_FS_ERROR);

    TEST_ASSERT(fs.dir_open(fs.context, "/replacement", &replacement, &entry) ==
                LIBRARY_FS_ENTRY);
    TEST_CHECK(fs.dir_close(fs.context, handle) == LIBRARY_FS_ERROR);
    TEST_CHECK(fs.dir_close(fs.context, replacement) == LIBRARY_FS_ENTRY);

    TEST_ASSERT(fs.dir_open(fs.context, "/explicit-retry", &handle, &entry) ==
                LIBRARY_FS_ENTRY);
    stub.close_result = -2;
    stub.close_errno = EACCES;
    errno = 0;
    TEST_CHECK(fs.dir_close(fs.context, handle) == LIBRARY_FS_ERROR);
    TEST_CHECK(errno == EACCES);
    close_calls = stub.close_calls;
    stub.close_result = 0;
    TEST_CHECK(fs.dir_close(fs.context, handle) == LIBRARY_FS_ENTRY);
    TEST_CHECK(stub.close_calls == close_calls + 1U);

    TEST_ASSERT(fs.dir_open(fs.context, "/deinit-retry", &handle, &entry) ==
                LIBRARY_FS_ENTRY);
    stub.close_result = -2;
    stub.close_errno = ENOSPC;
    TEST_CHECK(fs.dir_close(fs.context, handle) == LIBRARY_FS_ERROR);
    close_calls = stub.close_calls;
    errno = 0;
    library_fs_libdragon_deinit(&fs);
    TEST_CHECK(stub.close_calls == close_calls + 1U);
    TEST_CHECK(errno == ENOSPC);
    TEST_CHECK(all_zero(&fs, sizeof(fs)));
}

void test_libdragon_adapter_open_rollback_quarantines_close_failure(void)
{
    library_fs_t fs;
    library_dirent_t entry;
    void *handles[TEST_POOL_SIZE - 1U];
    void *handle = (void *)(uintptr_t)99U;
    size_t first_calls;
    size_t index;

    reset_stub();
    TEST_ASSERT(library_fs_libdragon_init(&fs, "sd:/"));
    stub.bad_name = true;
    stub.close_result = -2;
    stub.close_errno = EBUSY;
    memset(&entry, 0xa5, sizeof(entry));
    errno = 0;
    TEST_CHECK(fs.dir_open(fs.context, "/malformed", &handle, &entry) ==
               LIBRARY_FS_ERROR);
    TEST_CHECK(handle == NULL);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(errno == EOVERFLOW);
    TEST_CHECK(stub.close_calls == 1U);

    stub.bad_name = false;
    stub.close_result = 0;
    for (index = 0U; index < TEST_POOL_SIZE - 1U; ++index) {
        TEST_ASSERT(fs.dir_open(fs.context, "/active", &handles[index], &entry) ==
                    LIBRARY_FS_ENTRY);
    }
    first_calls = stub.first_calls;
    handle = (void *)(uintptr_t)99U;
    memset(&entry, 0xa5, sizeof(entry));
    errno = 0;
    TEST_CHECK(fs.dir_open(fs.context, "/must-not-reuse", &handle, &entry) ==
               LIBRARY_FS_ERROR);
    TEST_CHECK(handle == NULL);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(errno == EMFILE);
    TEST_CHECK(stub.first_calls == first_calls);

    for (index = 0U; index < TEST_POOL_SIZE - 1U; ++index) {
        TEST_CHECK(fs.dir_close(fs.context, handles[index]) == LIBRARY_FS_ENTRY);
    }
    TEST_CHECK(stub.close_calls == TEST_POOL_SIZE);
    library_fs_libdragon_deinit(&fs);
    TEST_CHECK(stub.close_calls == TEST_POOL_SIZE + 1U);
    TEST_CHECK(all_zero(&fs, sizeof(fs)));
}

void test_libdragon_adapter_init_bounds_and_zeroing(void)
{
    library_fs_t fs;
    char prefix[64U];

    memset(&fs, 0xa5, sizeof(fs));
    TEST_CHECK(library_fs_libdragon_init(&fs, NULL) == false);
    TEST_CHECK(all_zero(&fs, sizeof(fs)));
    memset(&fs, 0xa5, sizeof(fs));
    TEST_CHECK(library_fs_libdragon_init(&fs, "") == false);
    TEST_CHECK(all_zero(&fs, sizeof(fs)));
    memset(prefix, 'x', sizeof(prefix));
    memset(&fs, 0xa5, sizeof(fs));
    TEST_CHECK(library_fs_libdragon_init(&fs, prefix) == false);
    TEST_CHECK(all_zero(&fs, sizeof(fs)));
}

void test_libdragon_adapter_two_instance_handle_provenance(void)
{
    library_fs_t first_fs;
    library_fs_t second_fs;
    library_dirent_t entry;
    void *first_dir = NULL;
    void *first_file = NULL;
    void *second_dir = NULL;
    void *second_file = NULL;
    unsigned char bytes[8];
    size_t next_calls;
    size_t close_calls;

    reset_stub();
    TEST_ASSERT(library_fs_libdragon_init(&first_fs, "./"));
    TEST_ASSERT(library_fs_libdragon_init(&second_fs, "./"));
    TEST_ASSERT(first_fs.dir_open(first_fs.context, "/first", &first_dir, &entry) ==
                LIBRARY_FS_ENTRY);
    TEST_ASSERT(second_fs.dir_open(second_fs.context, "/second", &second_dir, &entry) ==
                LIBRARY_FS_ENTRY);
    TEST_ASSERT(first_fs.file_open_read(first_fs.context, "/Makefile", &first_file) ==
                LIBRARY_FS_ENTRY);
    TEST_ASSERT(second_fs.file_open_read(second_fs.context, "/Makefile", &second_file) ==
                LIBRARY_FS_ENTRY);
    TEST_CHECK(first_dir != second_dir);
    TEST_CHECK(first_dir != first_file);
    TEST_CHECK(first_dir != second_file);
    TEST_CHECK(first_file != second_dir);
    TEST_CHECK(first_file != second_file);
    TEST_CHECK(second_dir != second_file);
    TEST_CHECK(((uintptr_t)first_dir & (uintptr_t)1U) != 0U);
    TEST_CHECK(((uintptr_t)first_file & (uintptr_t)1U) != 0U);

    next_calls = stub.next_calls;
    close_calls = stub.close_calls;
    memset(&entry, 0xa5, sizeof(entry));
    TEST_CHECK(second_fs.dir_next(second_fs.context, first_dir, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(second_fs.dir_close(second_fs.context, first_dir) == LIBRARY_FS_ERROR);
    memset(bytes, 0xa5, sizeof(bytes));
    TEST_CHECK(second_fs.file_read(second_fs.context, first_dir, bytes, sizeof(bytes)) == -1);
    TEST_CHECK(bytes[0] == 0xa5U && bytes[sizeof(bytes) - 1U] == 0xa5U);
    TEST_CHECK(second_fs.file_close(second_fs.context, first_dir) == LIBRARY_FS_ERROR);

    memset(&entry, 0xa5, sizeof(entry));
    TEST_CHECK(second_fs.dir_next(second_fs.context, first_file, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(second_fs.dir_close(second_fs.context, first_file) == LIBRARY_FS_ERROR);
    memset(bytes, 0xa5, sizeof(bytes));
    TEST_CHECK(second_fs.file_read(second_fs.context, first_file, bytes, sizeof(bytes)) == -1);
    TEST_CHECK(bytes[0] == 0xa5U && bytes[sizeof(bytes) - 1U] == 0xa5U);
    TEST_CHECK(second_fs.file_close(second_fs.context, first_file) == LIBRARY_FS_ERROR);
    TEST_CHECK(stub.next_calls == next_calls);
    TEST_CHECK(stub.close_calls == close_calls);

    memset(bytes, 0, sizeof(bytes));
    TEST_CHECK(second_fs.file_read(second_fs.context, second_file, bytes, sizeof(bytes)) ==
               (int64_t)sizeof(bytes));
    TEST_CHECK(memcmp(bytes, "CC ?= cc", sizeof(bytes)) == 0);
    TEST_CHECK(second_fs.dir_next(second_fs.context, second_dir, &entry) == LIBRARY_FS_EOF);
    TEST_CHECK(stub.next_calls == next_calls + 1U);
    TEST_CHECK(stub.close_calls == close_calls + 1U);

    memset(bytes, 0, sizeof(bytes));
    TEST_CHECK(first_fs.file_read(first_fs.context, first_file, bytes, sizeof(bytes)) ==
               (int64_t)sizeof(bytes));
    TEST_CHECK(memcmp(bytes, "CC ?= cc", sizeof(bytes)) == 0);
    TEST_CHECK(first_fs.dir_next(first_fs.context, first_dir, &entry) == LIBRARY_FS_EOF);
    TEST_ASSERT(first_fs.dir_open(first_fs.context, "/first-active", &first_dir, &entry) ==
                LIBRARY_FS_ENTRY);
    TEST_ASSERT(second_fs.dir_open(second_fs.context, "/second-active", &second_dir, &entry) ==
                LIBRARY_FS_ENTRY);

    close_calls = stub.close_calls;
    library_fs_libdragon_deinit(&second_fs);
    TEST_CHECK(stub.close_calls == close_calls + 1U);
    library_fs_libdragon_deinit(&first_fs);
    TEST_CHECK(stub.close_calls == close_calls + 2U);
    TEST_CHECK(all_zero(&first_fs, sizeof(first_fs)));
    TEST_CHECK(all_zero(&second_fs, sizeof(second_fs)));
}

void test_libdragon_adapter_files_stat_cross_tokens_and_pool(void)
{
    library_fs_t fs;
    library_dirent_t entry;
    library_stat_t stat_value;
    void *dir_handle = NULL;
    void *file_handle = NULL;
    void *replacement = NULL;
    void *handles[TEST_POOL_SIZE];
    void *stale;
    unsigned char bytes[8] = { 0U };
    size_t index;
    size_t next_calls;

    reset_stub();
    TEST_ASSERT(library_fs_libdragon_init(&fs, "./"));
    TEST_ASSERT(fs.stat(fs.context, "/Makefile", &stat_value) == LIBRARY_FS_ENTRY);
    TEST_CHECK(stat_value.type == LIBRARY_FS_ENTRY_FILE);
    TEST_CHECK(stat_value.size > sizeof(bytes));

    TEST_ASSERT(fs.file_open_read(fs.context, "/Makefile", &file_handle) == LIBRARY_FS_ENTRY);
    TEST_CHECK(fs.file_read(fs.context, file_handle, bytes, sizeof(bytes)) == (int64_t)sizeof(bytes));
    TEST_CHECK(memcmp(bytes, "CC ?= cc", sizeof(bytes)) == 0);
    stale = file_handle;

    TEST_ASSERT(fs.dir_open(fs.context, "/cross", &dir_handle, &entry) == LIBRARY_FS_ENTRY);
    next_calls = stub.next_calls;
    memset(&entry, 0xa5, sizeof(entry));
    TEST_CHECK(fs.dir_next(fs.context, file_handle, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(all_zero(&entry, sizeof(entry)));
    TEST_CHECK(stub.next_calls == next_calls);
    TEST_CHECK(fs.file_read(fs.context, dir_handle, bytes, 1U) == -1);
    TEST_CHECK(fs.file_close(fs.context, dir_handle) == LIBRARY_FS_ERROR);
    TEST_CHECK(fs.dir_close(fs.context, file_handle) == LIBRARY_FS_ERROR);

    TEST_CHECK(fs.file_close(fs.context, file_handle) == LIBRARY_FS_ENTRY);
    TEST_CHECK(fs.file_close(fs.context, file_handle) == LIBRARY_FS_EOF);
    TEST_ASSERT(fs.file_open_read(fs.context, "/Makefile", &replacement) == LIBRARY_FS_ENTRY);
    TEST_CHECK(fs.file_close(fs.context, stale) == LIBRARY_FS_ERROR);
    TEST_CHECK(fs.file_read(fs.context, replacement, bytes, 1U) == 1);
    TEST_CHECK(fs.file_close(fs.context, replacement) == LIBRARY_FS_ENTRY);
    TEST_CHECK(fs.dir_close(fs.context, dir_handle) == LIBRARY_FS_ENTRY);

    for (index = 0U; index < TEST_POOL_SIZE; ++index) {
        TEST_ASSERT(fs.file_open_read(fs.context, "/Makefile", &handles[index]) == LIBRARY_FS_ENTRY);
    }
    replacement = (void *)(uintptr_t)99U;
    TEST_CHECK(fs.file_open_read(fs.context, "/Makefile", &replacement) == LIBRARY_FS_ERROR);
    TEST_CHECK(replacement == NULL);
    TEST_CHECK(errno == EMFILE);
    for (index = 0U; index + 1U < TEST_POOL_SIZE; ++index) {
        TEST_CHECK(fs.file_close(fs.context, handles[index]) == LIBRARY_FS_ENTRY);
    }
    /* Deinit owns and closes the final still-active file handle. */
    errno = ERANGE;
    library_fs_libdragon_deinit(&fs);
    TEST_CHECK(all_zero(&fs, sizeof(fs)));
    TEST_CHECK(errno == ERANGE);
}

void test_libdragon_adapter_token_exhaustion_preflight(void)
{
    library_fs_libdragon_token_issuer_t saved;
    library_fs_libdragon_token_issuer_t injected;
    library_fs_t fs;
    library_dirent_t entry;
    library_dirent_t zero_entry;
    void *handle = NULL;
    size_t first_calls;
    size_t close_calls;
    size_t file_open_calls;

    reset_stub();
    memset(&fs, 0, sizeof(fs));
    memset(&zero_entry, 0, sizeof(zero_entry));
    library_fs_libdragon_test_get_token_issuer(&saved);
    injected.next_token = UINTPTR_MAX;
    if ((injected.next_token & (uintptr_t)1U) == 0U) --injected.next_token;
    injected.exhausted = false;
    library_fs_libdragon_test_set_token_issuer(&injected);

    TEST_CHECK(library_fs_libdragon_init(&fs, "./"));
    if (fs.context == NULL) {
        library_fs_libdragon_test_set_token_issuer(&saved);
        return;
    }
    TEST_CHECK(fs.dir_open(fs.context, "/last-token", &handle, &entry) == LIBRARY_FS_ENTRY);
    TEST_CHECK((uintptr_t)handle == injected.next_token);
    TEST_CHECK(((uintptr_t)handle & (uintptr_t)1U) != 0U);
    if (handle != NULL) TEST_CHECK(fs.dir_close(fs.context, handle) == LIBRARY_FS_ENTRY);

    first_calls = stub.first_calls;
    close_calls = stub.close_calls;
    file_open_calls = stub_file_open_calls;
    handle = (void *)(uintptr_t)99U;
    memset(&entry, 0xa5, sizeof(entry));
    errno = 0;
    TEST_CHECK(fs.dir_open(fs.context, "/exhausted", &handle, &entry) == LIBRARY_FS_ERROR);
    TEST_CHECK(handle == NULL);
    TEST_CHECK(memcmp(&entry, &zero_entry, sizeof(entry)) == 0);
    TEST_CHECK(errno == EMFILE);
    TEST_CHECK(stub.first_calls == first_calls);
    TEST_CHECK(stub.close_calls == close_calls);

    handle = (void *)(uintptr_t)99U;
    errno = 0;
    TEST_CHECK(fs.file_open_read(fs.context, "/Makefile", &handle) == LIBRARY_FS_ERROR);
    TEST_CHECK(handle == NULL);
    TEST_CHECK(errno == EMFILE);
    TEST_CHECK(stub_file_open_calls == file_open_calls);

    library_fs_libdragon_deinit(&fs);
    library_fs_libdragon_test_set_token_issuer(&saved);
}
