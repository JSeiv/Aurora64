#ifndef LIBRARY_FS_H__
#define LIBRARY_FS_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIBRARY_FS_BASENAME_CAPACITY 256U

typedef enum {
    LIBRARY_FS_ERROR = -1,
    LIBRARY_FS_EOF = 0,
    LIBRARY_FS_ENTRY = 1
} library_fs_result_t;

typedef enum {
    LIBRARY_FS_ENTRY_UNKNOWN = 0,
    LIBRARY_FS_ENTRY_FILE,
    LIBRARY_FS_ENTRY_DIRECTORY
} library_fs_entry_type_t;

/* Basenames are always NUL terminated. Size is zero when unavailable. */
typedef struct {
    char basename[LIBRARY_FS_BASENAME_CAPACITY];
    library_fs_entry_type_t type;
    uint64_t size;
    int64_t modified_time;
    uint64_t change_token;
} library_dirent_t;

/* Public stat data only: no FatFs implementation details escape this API. */
typedef struct {
    library_fs_entry_type_t type;
    uint64_t size;
    int64_t modified_time;
    uint64_t change_token;
} library_stat_t;

typedef struct {
    void *context;
    int (*dir_open)(void *context, const char *path, void **handle, library_dirent_t *first);
    int (*dir_next)(void *context, void *handle, library_dirent_t *next);
    int (*dir_close)(void *context, void *handle);
    int (*file_open_read)(void *context, const char *path, void **handle);
    int64_t (*file_read)(void *context, void *handle, void *buffer, size_t length);
    int (*file_close)(void *context, void *handle);
    int (*stat)(void *context, const char *path, library_stat_t *out);
    uint32_t (*ticks_now)(void *context);
} library_fs_t;

/*
 * Initializes an adapter that owns a copy of storage_prefix (for example
 * "sd:/"). Logical callback paths must be absolute "/..." paths. Directory
 * and file handles are generation-safe opaque integer tokens backed by reusable
 * bounded pools; callers must never dereference them. A single-threaded,
 * process-wide monotonic issuer makes tokens unique across adapter instances
 * and both handle types. Stale, foreign, and random tokens are rejected without
 * dereference. Exhausting an active pool, or exhausting the token space rather
 * than wrapping it, fails with errno set to EMFILE.
 */
bool library_fs_libdragon_init(library_fs_t *out, const char *storage_prefix);
void library_fs_libdragon_deinit(library_fs_t *fs);

#ifdef LIBRARY_FS_HOST_TEST
typedef struct {
    uintptr_t next_token;
    bool exhausted;
} library_fs_libdragon_token_issuer_t;

void library_fs_libdragon_test_get_token_issuer(
    library_fs_libdragon_token_issuer_t *out);
void library_fs_libdragon_test_set_token_issuer(
    const library_fs_libdragon_token_issuer_t *state);
#endif

#endif
