#ifndef FAKE_LIBRARY_FS_H__
#define FAKE_LIBRARY_FS_H__

#include "menu/library/library_fs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FAKE_LIBRARY_FS_MAX_DIRECTORIES 8U
#define FAKE_LIBRARY_FS_MAX_ENTRIES 16U
#define FAKE_LIBRARY_FS_MAX_FILES 8U
#define FAKE_LIBRARY_FS_MAX_FILE_BYTES 1024U
#define FAKE_LIBRARY_FS_MAX_HANDLES 16U
#define FAKE_LIBRARY_FS_PATH_CAPACITY 512U

typedef struct {
    char path[FAKE_LIBRARY_FS_PATH_CAPACITY];
    library_dirent_t entries[FAKE_LIBRARY_FS_MAX_ENTRIES];
    size_t entry_count;
} fake_library_directory_t;

typedef struct {
    char path[FAKE_LIBRARY_FS_PATH_CAPACITY];
    uint8_t data[FAKE_LIBRARY_FS_MAX_FILE_BYTES];
    size_t length;
    int64_t modified_time;
} fake_library_file_t;

typedef struct {
    bool active;
    uintptr_t token;
    size_t object_index;
    size_t offset;
} fake_library_handle_t;

typedef struct {
    library_fs_t interface;
    fake_library_directory_t directories[FAKE_LIBRARY_FS_MAX_DIRECTORIES];
    fake_library_file_t files[FAKE_LIBRARY_FS_MAX_FILES];
    fake_library_handle_t dir_handles[FAKE_LIBRARY_FS_MAX_HANDLES];
    fake_library_handle_t file_handles[FAKE_LIBRARY_FS_MAX_HANDLES];
    size_t directory_count;
    size_t file_count;
    /* Last issued token; UINTPTR_MAX forces deterministic exhaustion in tests. */
    uintptr_t next_token;
    char fail_dir_open_path[FAKE_LIBRARY_FS_PATH_CAPACITY];
    size_t fail_dir_next_call;
    bool fail_file_read;
    bool fail_stat;
    uint32_t ticks;
    size_t dir_open_calls;
    size_t dir_next_calls;
    size_t dir_close_calls;
    size_t backend_dir_close_calls;
    size_t active_dir_handles;
    size_t file_open_calls;
    size_t file_read_calls;
    size_t file_close_calls;
    size_t backend_file_close_calls;
    size_t active_file_handles;
    size_t stat_calls;
} fake_library_fs_t;

void fake_library_fs_init(fake_library_fs_t *fake);
library_fs_t *fake_library_fs_interface(fake_library_fs_t *fake);
bool fake_library_fs_add_directory(fake_library_fs_t *fake, const char *path,
                                   const library_dirent_t *entries, size_t count);
bool fake_library_fs_add_file(fake_library_fs_t *fake, const char *path,
                              const uint8_t *data, size_t length,
                              int64_t modified_time);
void fake_library_fs_fail_dir_open(fake_library_fs_t *fake, const char *path);
void fake_library_fs_fail_dir_next(fake_library_fs_t *fake, size_t call_number);
void fake_library_fs_fail_file_read(fake_library_fs_t *fake, bool fail);
void fake_library_fs_fail_stat(fake_library_fs_t *fake, bool fail);
void fake_library_fs_set_ticks(fake_library_fs_t *fake, uint32_t ticks);

#endif
