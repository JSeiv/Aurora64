#define TEST_NO_MAIN
#include "acutest.h"
#include "menu/library/library_roots.h"
#include "support/fake_library_fs.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

void test_library_roots_normalization(void)
{
    char output[LIBRARY_ROOT_PATH_CAPACITY];

    TEST_CHECK(library_root_normalize("///Games//./N64///", output, sizeof(output)));
    TEST_CHECK(strcmp(output, "/Games/N64") == 0);
    TEST_CHECK(library_root_normalize("/a/b/../c", output, sizeof(output)));
    TEST_CHECK(strcmp(output, "/a/c") == 0);
    TEST_CHECK(library_root_normalize("/../escape", output, sizeof(output)) == false);
    TEST_CHECK(output[0] == '\0');
    TEST_CHECK(library_root_normalize("/a/../../escape", output, sizeof(output)) == false);
    TEST_CHECK(output[0] == '\0');
    TEST_CHECK(library_root_normalize("/", output, sizeof(output)));
    TEST_CHECK(strcmp(output, "/") == 0);
}

void test_library_roots_bounds_and_atomicity(void)
{
    char accepted[LIBRARY_ROOT_PATH_CAPACITY];
    char rejected[LIBRARY_ROOT_PATH_CAPACITY + 1U];
    char output[LIBRARY_ROOT_PATH_CAPACITY];
    library_roots_t roots;
    library_roots_t zero_roots;
    const char *bad[] = { "/valid", "relative" };
    const char *single[1];
    size_t index;

    memset(&zero_roots, 0, sizeof(zero_roots));
    accepted[0] = '/';
    for (index = 1U; index < LIBRARY_ROOT_PATH_CAPACITY - 1U; ++index) accepted[index] = 'a';
    accepted[LIBRARY_ROOT_PATH_CAPACITY - 1U] = '\0';
    TEST_CHECK(strlen(accepted) == 511U);
    TEST_CHECK(library_root_normalize(accepted, output, sizeof(output)));
    TEST_CHECK(strcmp(output, accepted) == 0);

    rejected[0] = '/';
    for (index = 1U; index < LIBRARY_ROOT_PATH_CAPACITY; ++index) rejected[index] = 'b';
    rejected[LIBRARY_ROOT_PATH_CAPACITY] = '\0';
    memset(output, 0xa5, sizeof(output));
    TEST_CHECK(library_root_normalize(rejected, output, sizeof(output)) == false);
    TEST_CHECK(output[0] == '\0');
    TEST_CHECK(library_root_normalize(NULL, output, sizeof(output)) == false);
    TEST_CHECK(output[0] == '\0');
    TEST_CHECK(library_root_normalize("relative", output, sizeof(output)) == false);
    TEST_CHECK(output[0] == '\0');
    TEST_CHECK(library_root_normalize("/abc", output, 4U) == false);
    TEST_CHECK(output[0] == '\0');
    TEST_CHECK(library_root_normalize("/abc", NULL, sizeof(output)) == false);
    TEST_CHECK(library_root_normalize("/abc", output, 0U) == false);

    memset(&roots, 0xa5, sizeof(roots));
    TEST_CHECK(library_roots_configure(bad, 2U, &roots) == false);
    TEST_CHECK(memcmp(&roots, &zero_roots, sizeof(roots)) == 0);
    single[0] = accepted;
    TEST_CHECK(library_roots_configure(single, 1U, &roots));
    TEST_CHECK(roots.count == 1U);
    TEST_CHECK(strcmp(roots.paths[0], accepted) == 0);
    single[0] = rejected;
    memset(&roots, 0xa5, sizeof(roots));
    TEST_CHECK(library_roots_configure(single, 1U, &roots) == false);
    TEST_CHECK(memcmp(&roots, &zero_roots, sizeof(roots)) == 0);
}

void test_library_roots_reduction_and_limits(void)
{
    library_roots_t roots;
    const char *duplicates[] = { "/ROMS", "/roms/", "//Roms" };
    const char *exact_duplicates[] = { "/roms", "/roms" };
    const char *case_duplicates[] = { "/ROMS", "/roms" };
    const char *ancestor[] = { "/games/n64", "/GAMES", "/games/ps1" };
    const char *boundary[] = { "/rom", "/roms" };
    const char *too_many[] = { "/a", "/a", "/a", "/a", "/a" };
    const char *exact_limit[] = { "/top", "/top/a", "/TOP", "/top/b" };
    library_roots_t zero_roots;

    memset(&zero_roots, 0, sizeof(zero_roots));

    TEST_CHECK(library_path_ascii_equal("/Roms/É", "/roms/É"));
    TEST_CHECK(library_path_ascii_equal("/Roms/É", "/roms/é") == false);
    TEST_CHECK(library_roots_configure(duplicates, 3U, &roots));
    TEST_CHECK(roots.count == 1U);
    TEST_CHECK(strcmp(roots.paths[0], "/ROMS") == 0);
    TEST_CHECK(library_roots_configure(exact_duplicates, 2U, &roots));
    TEST_CHECK(strcmp(roots.paths[0], "/roms") == 0);
    TEST_CHECK(library_roots_configure(case_duplicates, 2U, &roots));
    TEST_CHECK(strcmp(roots.paths[0], "/ROMS") == 0);
    TEST_CHECK(library_roots_configure(ancestor, 3U, &roots));
    TEST_CHECK(roots.count == 1U);
    TEST_CHECK(strcmp(roots.paths[0], "/GAMES") == 0);
    TEST_CHECK(library_roots_configure(boundary, 2U, &roots) == false);
    TEST_CHECK(memcmp(&roots, &zero_roots, sizeof(roots)) == 0);
    TEST_CHECK(library_roots_configure(too_many, 5U, &roots) == false);
    TEST_CHECK(memcmp(&roots, &zero_roots, sizeof(roots)) == 0);
    TEST_CHECK(library_roots_configure(exact_limit, 4U, &roots));
    TEST_CHECK(roots.count == 1U);
    TEST_CHECK(strcmp(roots.paths[0], "/top") == 0);
    TEST_CHECK(library_roots_configure(NULL, 1U, &roots) == false);
    TEST_CHECK(library_roots_configure(duplicates, 0U, &roots) == false);
    TEST_CHECK(library_roots_configure(duplicates, 1U, NULL) == false);
}

static void perturb_library_roots_stack(unsigned char value)
{
    volatile unsigned char noise[4096U];
    size_t index;
    for (index = 0U; index < sizeof(noise); ++index) noise[index] = value;
    TEST_CHECK(noise[sizeof(noise) - 1U] == value);
}

void test_library_roots_deterministic_zeroed_output(void)
{
    const char *inputs[] = { "/games/n64/../roms" };
    library_roots_t first;
    library_roots_t second;
    const unsigned char *bytes;
    size_t used;
    size_t index;

    perturb_library_roots_stack(0x3cU);
    memset(&first, 0xa5, sizeof(first));
    TEST_ASSERT(library_roots_configure(inputs, 1U, &first));
    TEST_CHECK(first.count == 1U);
    TEST_CHECK(strcmp(first.paths[0], "/games/roms") == 0);
    used = (size_t)((const unsigned char *)&first.paths[0][0] -
                    (const unsigned char *)&first) +
           strlen(first.paths[0]) + 1U;
    bytes = (const unsigned char *)&first;
    for (index = used; index < sizeof(first); ++index) TEST_CHECK(bytes[index] == 0U);

    perturb_library_roots_stack(0xc3U);
    memset(&second, 0x5a, sizeof(second));
    TEST_ASSERT(library_roots_configure(inputs, 1U, &second));
    TEST_CHECK(memcmp(&first, &second, sizeof(first)) == 0);
}

void test_library_roots_default(void)
{
    const library_roots_t *roots = library_roots_default();
    TEST_ASSERT(roots != NULL);
    TEST_CHECK(roots->count == 1U);
    TEST_CHECK(strcmp(roots->paths[0], "/") == 0);
}

void test_library_exclusions(void)
{
    static const char *root_names[] = {
        "menu.bin", "menu", "N64FlashcartMenu.n64", "ED64", "ED64P",
        "sc64menu.n64", "System Volume Information", ".fseventsd",
        ".Spotlight-V100", ".Trashes", ".VolumeIcon.icns",
        ".metadata_never_index"
    };
    static const char *everywhere[] = { "desktop.ini", "Thumbs.db", ".DS_Store" };
    char unterminated[LIBRARY_FS_BASENAME_CAPACITY];
    char case_variant[LIBRARY_FS_BASENAME_CAPACITY];
    size_t index;

    for (index = 0U; index < sizeof(root_names) / sizeof(root_names[0]); ++index) {
        size_t character;
        TEST_CHECK_(library_basename_is_excluded(true, root_names[index]), "%s", root_names[index]);
        TEST_CHECK_(library_basename_is_excluded(false, root_names[index]) == false, "%s", root_names[index]);
        for (character = 0U; root_names[index][character] != '\0'; ++character) {
            unsigned char value = (unsigned char)root_names[index][character];
            if (value >= (unsigned char)'a' && value <= (unsigned char)'z') value -= (unsigned char)('a' - 'A');
            else if (value >= (unsigned char)'A' && value <= (unsigned char)'Z') value += (unsigned char)('a' - 'A');
            case_variant[character] = (char)value;
        }
        case_variant[character] = '\0';
        TEST_CHECK_(library_basename_is_excluded(true, case_variant), "%s", case_variant);
    }
    for (index = 0U; index < sizeof(everywhere) / sizeof(everywhere[0]); ++index) {
        size_t character;
        TEST_CHECK(library_basename_is_excluded(true, everywhere[index]));
        TEST_CHECK(library_basename_is_excluded(false, everywhere[index]));
        for (character = 0U; everywhere[index][character] != '\0'; ++character) {
            unsigned char value = (unsigned char)everywhere[index][character];
            if (value >= (unsigned char)'a' && value <= (unsigned char)'z') value -= (unsigned char)('a' - 'A');
            else if (value >= (unsigned char)'A' && value <= (unsigned char)'Z') value += (unsigned char)('a' - 'A');
            case_variant[character] = (char)value;
        }
        case_variant[character] = '\0';
        TEST_CHECK(library_basename_is_excluded(false, case_variant));
    }
    TEST_CHECK(library_basename_is_excluded(true, "MeNu.BiN"));
    TEST_CHECK(library_basename_is_excluded(false, "tHuMbS.Db"));
    TEST_CHECK(library_basename_is_excluded(false, "._x"));
    TEST_CHECK(library_basename_is_excluded(false, "._long-name"));
    TEST_CHECK(library_basename_is_excluded(false, "._") == false);
    TEST_CHECK(library_basename_is_excluded(false, "x._file") == false);
    TEST_CHECK(library_basename_is_excluded(true, "menu.bin.bak") == false);
    TEST_CHECK(library_basename_is_excluded(true, "/menu.bin"));
    TEST_CHECK(library_basename_is_excluded(false, "desktop.ini.bak") == false);
    TEST_CHECK(library_basename_is_excluded(false, NULL));
    memset(unterminated, 'x', sizeof(unterminated));
    TEST_CHECK(library_basename_is_excluded(false, unterminated));
}

static library_dirent_t fake_entry(const char *name, library_fs_entry_type_t type, uint64_t size)
{
    library_dirent_t entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.basename, name, strlen(name) + 1U);
    entry.type = type;
    entry.size = size;
    return entry;
}

void test_fake_library_fs_directory_lifecycle(void)
{
    fake_library_fs_t fake;
    library_fs_t *fs;
    library_dirent_t entries[2];
    library_dirent_t got;
    void *handle = (void *)(uintptr_t)1U;

    fake_library_fs_init(&fake);
    fs = fake_library_fs_interface(&fake);
    TEST_ASSERT(fs != NULL);
    TEST_ASSERT(fake_library_fs_add_directory(&fake, "/empty", NULL, 0U));
    TEST_CHECK(fs->dir_open(fs->context, "/empty", &handle, &got) == LIBRARY_FS_EOF);
    TEST_CHECK(handle == NULL);
    TEST_CHECK(fake.dir_open_calls == 1U);
    TEST_CHECK(fake.backend_dir_close_calls == 0U);

    entries[0] = fake_entry("one.z64", LIBRARY_FS_ENTRY_FILE, 10U);
    entries[1] = fake_entry("sub", LIBRARY_FS_ENTRY_DIRECTORY, 0U);
    TEST_ASSERT(fake_library_fs_add_directory(&fake, "/games", entries, 2U));
    TEST_CHECK(fs->dir_open(fs->context, "/games", &handle, &got) == LIBRARY_FS_ENTRY);
    TEST_ASSERT(handle != NULL);
    TEST_CHECK(strcmp(got.basename, "one.z64") == 0);
    TEST_CHECK(fs->dir_next(fs->context, handle, &got) == LIBRARY_FS_ENTRY);
    TEST_CHECK(strcmp(got.basename, "sub") == 0);
    TEST_CHECK(fs->dir_next(fs->context, handle, &got) == LIBRARY_FS_EOF);
    TEST_CHECK(fake.backend_dir_close_calls == 1U);
    TEST_CHECK(fs->dir_next(fs->context, handle, &got) == LIBRARY_FS_ERROR);
    TEST_CHECK(fs->dir_close(fs->context, handle) == LIBRARY_FS_EOF);
    TEST_CHECK(fake.backend_dir_close_calls == 1U);

    TEST_CHECK(fs->dir_open(fs->context, "/games", &handle, &got) == LIBRARY_FS_ENTRY);
    TEST_CHECK(fs->dir_close(fs->context, handle) == LIBRARY_FS_ENTRY);
    TEST_CHECK(fake.backend_dir_close_calls == 2U);
    TEST_CHECK(fs->dir_close(fs->context, handle) == LIBRARY_FS_EOF);
    TEST_CHECK(fs->dir_next(fs->context, handle, &got) == LIBRARY_FS_ERROR);
    TEST_CHECK(fake.backend_dir_close_calls == 2U);
}

void test_fake_library_fs_errors_and_balance(void)
{
    fake_library_fs_t fake;
    library_fs_t *fs;
    library_dirent_t entry = fake_entry("one", LIBRARY_FS_ENTRY_FILE, 1U);
    library_dirent_t got;
    void *handle = (void *)(uintptr_t)1U;

    fake_library_fs_init(&fake);
    fs = fake_library_fs_interface(&fake);
    TEST_ASSERT(fake_library_fs_add_directory(&fake, "/games", &entry, 1U));
    fake_library_fs_fail_dir_open(&fake, "/bad");
    TEST_CHECK(fs->dir_open(fs->context, "/bad", &handle, &got) == LIBRARY_FS_ERROR);
    TEST_CHECK(handle == NULL);

    fake_library_fs_fail_dir_next(&fake, 1U);
    TEST_CHECK(fs->dir_open(fs->context, "/games", &handle, &got) == LIBRARY_FS_ENTRY);
    TEST_CHECK(fs->dir_next(fs->context, handle, &got) == LIBRARY_FS_ERROR);
    TEST_CHECK(fake.backend_dir_close_calls == 0U);
    TEST_CHECK(fs->dir_close(fs->context, handle) == LIBRARY_FS_ENTRY);
    TEST_CHECK(fake.backend_dir_close_calls == 1U);
    TEST_CHECK(fake.active_dir_handles == 0U);
    TEST_CHECK(fake.dir_open_calls == 2U);
    TEST_CHECK(fake.dir_close_calls == 1U);
}

void test_fake_library_fs_file_stat_ticks_and_ownership(void)
{
    fake_library_fs_t fake;
    library_fs_t *fs;
    library_stat_t stat_value;
    void *handle = NULL;
    char path[] = "/file.bin";
    uint8_t data[] = { 1U, 2U, 3U, 4U };
    uint8_t readback[5] = { 0U };

    fake_library_fs_init(&fake);
    fs = fake_library_fs_interface(&fake);
    TEST_ASSERT(fake_library_fs_add_file(&fake, path, data, sizeof(data), 123));
    path[1] = 'X';
    data[0] = 9U;
    TEST_CHECK(fs->stat(fs->context, "/file.bin", &stat_value) == LIBRARY_FS_ENTRY);
    TEST_CHECK(stat_value.type == LIBRARY_FS_ENTRY_FILE);
    TEST_CHECK(stat_value.size == 4U);
    TEST_CHECK(stat_value.modified_time == 123);
    TEST_CHECK(fs->file_open_read(fs->context, "/file.bin", &handle) == LIBRARY_FS_ENTRY);
    TEST_CHECK(fs->file_read(fs->context, handle, readback, 2U) == 2);
    TEST_CHECK(fs->file_read(fs->context, handle, readback + 2U, 3U) == 2);
    TEST_CHECK(memcmp(readback, "\1\2\3\4", 4U) == 0);
    TEST_CHECK(fs->file_close(fs->context, handle) == LIBRARY_FS_ENTRY);
    TEST_CHECK(fs->file_close(fs->context, handle) == LIBRARY_FS_EOF);
    fake_library_fs_set_ticks(&fake, UINT32_C(0xfedcba98));
    TEST_CHECK(fs->ticks_now(fs->context) == UINT32_C(0xfedcba98));

    fake_library_fs_fail_file_read(&fake, true);
    TEST_CHECK(fs->file_open_read(fs->context, "/file.bin", &handle) == LIBRARY_FS_ENTRY);
    TEST_CHECK(fs->file_read(fs->context, handle, readback, 1U) == -1);
    TEST_CHECK(fs->file_close(fs->context, handle) == LIBRARY_FS_ENTRY);
    fake_library_fs_fail_stat(&fake, true);
    memset(&stat_value, 0xa5, sizeof(stat_value));
    TEST_CHECK(fs->stat(fs->context, "/file.bin", &stat_value) == LIBRARY_FS_ERROR);
    TEST_CHECK(stat_value.type == LIBRARY_FS_ENTRY_UNKNOWN);
    TEST_CHECK(fs->file_open_read(fs->context, "/missing", &handle) == LIBRARY_FS_ERROR);
    TEST_CHECK(handle == NULL);
}

void test_fake_library_fs_two_instance_handle_provenance(void)
{
    fake_library_fs_t first;
    fake_library_fs_t second;
    library_fs_t *first_fs;
    library_fs_t *second_fs;
    library_dirent_t entries[2];
    library_dirent_t got;
    library_dirent_t zero_entry;
    void *first_dir = NULL;
    void *first_file = NULL;
    void *second_dir = NULL;
    void *second_file = NULL;
    uint8_t data[] = { 1U, 2U, 3U };
    uint8_t bytes[3];
    size_t next_calls;
    size_t read_calls;
    size_t dir_closes;
    size_t file_closes;

    memset(&zero_entry, 0, sizeof(zero_entry));
    entries[0] = fake_entry("one", LIBRARY_FS_ENTRY_FILE, 1U);
    entries[1] = fake_entry("two", LIBRARY_FS_ENTRY_FILE, 1U);
    fake_library_fs_init(&first);
    fake_library_fs_init(&second);
    first_fs = fake_library_fs_interface(&first);
    second_fs = fake_library_fs_interface(&second);
    TEST_ASSERT(fake_library_fs_add_directory(&first, "/games", entries, 2U));
    TEST_ASSERT(fake_library_fs_add_directory(&second, "/games", entries, 2U));
    TEST_ASSERT(fake_library_fs_add_file(&first, "/data", data, sizeof(data), 0));
    TEST_ASSERT(fake_library_fs_add_file(&second, "/data", data, sizeof(data), 0));
    TEST_ASSERT(first_fs->dir_open(first_fs->context, "/games", &first_dir, &got) ==
                LIBRARY_FS_ENTRY);
    TEST_ASSERT(second_fs->dir_open(second_fs->context, "/games", &second_dir, &got) ==
                LIBRARY_FS_ENTRY);
    TEST_ASSERT(first_fs->file_open_read(first_fs->context, "/data", &first_file) ==
                LIBRARY_FS_ENTRY);
    TEST_ASSERT(second_fs->file_open_read(second_fs->context, "/data", &second_file) ==
                LIBRARY_FS_ENTRY);
    TEST_CHECK(first_dir != second_dir);
    TEST_CHECK(first_dir != first_file);
    TEST_CHECK(first_dir != second_file);
    TEST_CHECK(first_file != second_dir);
    TEST_CHECK(first_file != second_file);
    TEST_CHECK(second_dir != second_file);
    TEST_CHECK(((uintptr_t)first_dir & (uintptr_t)1U) == 0U);
    TEST_CHECK(((uintptr_t)first_file & (uintptr_t)1U) == 0U);

    next_calls = second.dir_next_calls;
    read_calls = second.file_read_calls;
    dir_closes = second.backend_dir_close_calls;
    file_closes = second.backend_file_close_calls;
    memset(&got, 0xa5, sizeof(got));
    TEST_CHECK(second_fs->dir_next(second_fs->context, first_dir, &got) == LIBRARY_FS_ERROR);
    TEST_CHECK(memcmp(&got, &zero_entry, sizeof(got)) == 0);
    TEST_CHECK(second_fs->dir_close(second_fs->context, first_dir) == LIBRARY_FS_ERROR);
    memset(bytes, 0xa5, sizeof(bytes));
    TEST_CHECK(second_fs->file_read(second_fs->context, first_dir, bytes, sizeof(bytes)) == -1);
    TEST_CHECK(bytes[0] == 0xa5U && bytes[sizeof(bytes) - 1U] == 0xa5U);
    TEST_CHECK(second_fs->file_close(second_fs->context, first_dir) == LIBRARY_FS_ERROR);

    memset(&got, 0xa5, sizeof(got));
    TEST_CHECK(second_fs->dir_next(second_fs->context, first_file, &got) == LIBRARY_FS_ERROR);
    TEST_CHECK(memcmp(&got, &zero_entry, sizeof(got)) == 0);
    TEST_CHECK(second_fs->dir_close(second_fs->context, first_file) == LIBRARY_FS_ERROR);
    memset(bytes, 0xa5, sizeof(bytes));
    TEST_CHECK(second_fs->file_read(second_fs->context, first_file, bytes, sizeof(bytes)) == -1);
    TEST_CHECK(bytes[0] == 0xa5U && bytes[sizeof(bytes) - 1U] == 0xa5U);
    TEST_CHECK(second_fs->file_close(second_fs->context, first_file) == LIBRARY_FS_ERROR);
    TEST_CHECK(second.dir_next_calls == next_calls);
    TEST_CHECK(second.file_read_calls == read_calls);
    TEST_CHECK(second.dir_close_calls == 0U);
    TEST_CHECK(second.file_close_calls == 0U);
    TEST_CHECK(second.backend_dir_close_calls == dir_closes);
    TEST_CHECK(second.backend_file_close_calls == file_closes);
    TEST_CHECK(second.active_dir_handles == 1U);
    TEST_CHECK(second.active_file_handles == 1U);

    TEST_CHECK(second_fs->dir_next(second_fs->context, second_dir, &got) == LIBRARY_FS_ENTRY);
    TEST_CHECK(strcmp(got.basename, "two") == 0);
    memset(bytes, 0, sizeof(bytes));
    TEST_CHECK(second_fs->file_read(second_fs->context, second_file, bytes, sizeof(bytes)) == 3);
    TEST_CHECK(memcmp(bytes, data, sizeof(data)) == 0);
    TEST_CHECK(first_fs->dir_next(first_fs->context, first_dir, &got) == LIBRARY_FS_ENTRY);
    TEST_CHECK(first_fs->file_read(first_fs->context, first_file, bytes, 1U) == 1);

    TEST_CHECK(second_fs->dir_close(second_fs->context, second_dir) == LIBRARY_FS_ENTRY);
    TEST_CHECK(second_fs->file_close(second_fs->context, second_file) == LIBRARY_FS_ENTRY);
    TEST_CHECK(first_fs->dir_close(first_fs->context, first_dir) == LIBRARY_FS_ENTRY);
    TEST_CHECK(first_fs->file_close(first_fs->context, first_file) == LIBRARY_FS_ENTRY);
    TEST_CHECK(first.active_dir_handles == 0U);
    TEST_CHECK(first.active_file_handles == 0U);
    TEST_CHECK(second.active_dir_handles == 0U);
    TEST_CHECK(second.active_file_handles == 0U);
    TEST_CHECK(first.backend_dir_close_calls == 1U);
    TEST_CHECK(first.backend_file_close_calls == 1U);
    TEST_CHECK(second.backend_dir_close_calls == 1U);
    TEST_CHECK(second.backend_file_close_calls == 1U);
}

void test_fake_library_fs_pool_stale_ownership_and_bounds(void)
{
    fake_library_fs_t fake;
    library_fs_t *fs;
    library_dirent_t source[2];
    library_dirent_t got;
    library_dirent_t zero_entry;
    library_stat_t stat_value;
    library_stat_t zero_stat;
    void *handles[FAKE_LIBRARY_FS_MAX_HANDLES];
    void *stale = NULL;
    char path[] = "/owned";
    char unterminated[FAKE_LIBRARY_FS_PATH_CAPACITY];
    static const uint8_t file_data[] = { 1U };
    size_t index;
    size_t closes;

    memset(&zero_entry, 0, sizeof(zero_entry));
    memset(&zero_stat, 0, sizeof(zero_stat));
    source[0] = fake_entry("original.z64", LIBRARY_FS_ENTRY_FILE, 7U);
    source[1] = fake_entry("original-dir", LIBRARY_FS_ENTRY_DIRECTORY, 0U);
    fake_library_fs_init(&fake);
    fs = fake_library_fs_interface(&fake);
    TEST_ASSERT(fake_library_fs_add_directory(&fake, path, source, 2U));
    TEST_ASSERT(fake_library_fs_add_file(&fake, "/owned.bin", file_data,
                                         sizeof(file_data), 0));
    path[1] = 'X';
    memcpy(source[0].basename, "mutated", 8U);
    memset(source, 0, sizeof(source));
    TEST_ASSERT(fs->dir_open(fs->context, "/owned", &stale, &got) == LIBRARY_FS_ENTRY);
    TEST_CHECK(strcmp(got.basename, "original.z64") == 0);
    TEST_CHECK(fs->dir_next(fs->context, stale, &got) == LIBRARY_FS_ENTRY);
    TEST_CHECK(strcmp(got.basename, "original-dir") == 0);
    TEST_CHECK(fs->dir_close(fs->context, stale) == LIBRARY_FS_ENTRY);

    for (index = 0U; index < 80U; ++index) {
        void *handle = NULL;
        TEST_ASSERT(fs->dir_open(fs->context, "/owned", &handle, &got) == LIBRARY_FS_ENTRY);
        TEST_CHECK(fs->dir_close(fs->context, handle) == LIBRARY_FS_ENTRY);
    }
    closes = fake.backend_dir_close_calls;
    memset(&got, 0xa5, sizeof(got));
    TEST_CHECK(fs->dir_next(fs->context, stale, &got) == LIBRARY_FS_ERROR);
    TEST_CHECK(memcmp(&got, &zero_entry, sizeof(got)) == 0);
    TEST_CHECK(fs->dir_close(fs->context, stale) == LIBRARY_FS_ERROR);
    TEST_CHECK(fake.backend_dir_close_calls == closes);

    for (index = 0U; index < FAKE_LIBRARY_FS_MAX_HANDLES; ++index) {
        TEST_ASSERT(fs->dir_open(fs->context, "/owned", &handles[index], &got) == LIBRARY_FS_ENTRY);
    }
    memset(&got, 0xa5, sizeof(got));
    TEST_CHECK(fs->dir_open(fs->context, "/owned", &stale, &got) == LIBRARY_FS_ERROR);
    TEST_CHECK(stale == NULL);
    TEST_CHECK(memcmp(&got, &zero_entry, sizeof(got)) == 0);
    for (index = 0U; index < FAKE_LIBRARY_FS_MAX_HANDLES; ++index) {
        TEST_CHECK(fs->dir_close(fs->context, handles[index]) == LIBRARY_FS_ENTRY);
    }

    memset(unterminated, 'x', sizeof(unterminated));
    memset(&got, 0xa5, sizeof(got));
    TEST_CHECK(fs->dir_open(fs->context, unterminated, &stale, &got) == LIBRARY_FS_ERROR);
    TEST_CHECK(stale == NULL);
    TEST_CHECK(memcmp(&got, &zero_entry, sizeof(got)) == 0);
    memset(&stat_value, 0xa5, sizeof(stat_value));
    TEST_CHECK(fs->stat(fs->context, unterminated, &stat_value) == LIBRARY_FS_ERROR);
    TEST_CHECK(memcmp(&stat_value, &zero_stat, sizeof(stat_value)) == 0);
    memset(&got, 0xa5, sizeof(got));
    TEST_CHECK(fs->dir_next(NULL, (void *)(uintptr_t)77U, &got) == LIBRARY_FS_ERROR);
    TEST_CHECK(memcmp(&got, &zero_entry, sizeof(got)) == 0);

    fake.next_token = UINTPTR_MAX;
    {
        size_t dir_opens = fake.dir_open_calls;
        size_t file_opens = fake.file_open_calls;
        fake_library_handle_t dir_handles[FAKE_LIBRARY_FS_MAX_HANDLES];
        fake_library_handle_t file_handles[FAKE_LIBRARY_FS_MAX_HANDLES];
        uintptr_t next_token = fake.next_token;
        memcpy(dir_handles, fake.dir_handles, sizeof(dir_handles));
        memcpy(file_handles, fake.file_handles, sizeof(file_handles));
        closes = fake.backend_dir_close_calls;
        memset(&got, 0xa5, sizeof(got));
        TEST_CHECK(fs->dir_open(fs->context, "/owned", &stale, &got) == LIBRARY_FS_ERROR);
        TEST_CHECK(stale == NULL);
        TEST_CHECK(memcmp(&got, &zero_entry, sizeof(got)) == 0);
        TEST_CHECK(fake.dir_open_calls == dir_opens);
        TEST_CHECK(fake.backend_dir_close_calls == closes);
        TEST_CHECK(fake.active_dir_handles == 0U);
        closes = fake.backend_file_close_calls;
        TEST_CHECK(fs->file_open_read(fs->context, "/owned.bin", &stale) == LIBRARY_FS_ERROR);
        TEST_CHECK(stale == NULL);
        TEST_CHECK(fake.file_open_calls == file_opens);
        TEST_CHECK(fake.backend_file_close_calls == closes);
        TEST_CHECK(fake.active_file_handles == 0U);
        TEST_CHECK(fake.next_token == next_token);
        TEST_CHECK(memcmp(fake.dir_handles, dir_handles, sizeof(dir_handles)) == 0);
        TEST_CHECK(memcmp(fake.file_handles, file_handles, sizeof(file_handles)) == 0);
        TEST_CHECK(errno == EMFILE);
    }
}
