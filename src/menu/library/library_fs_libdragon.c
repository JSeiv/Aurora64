#include "menu/library/library_fs.h"

#include <dir.h>
#include <timer.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LIBDRAGON_PREFIX_CAPACITY 64U
#define LIBDRAGON_LOGICAL_PATH_CAPACITY 512U
#define LIBDRAGON_TRANSLATED_PATH_CAPACITY \
    (LIBDRAGON_PREFIX_CAPACITY + LIBDRAGON_LOGICAL_PATH_CAPACITY)
#define LIBDRAGON_DIR_HANDLE_COUNT 16U
#define LIBDRAGON_FILE_HANDLE_COUNT 16U
#define LIBDRAGON_CONTEXT_MAGIC UINT32_C(0x4c465331)

typedef struct {
    bool active;
    bool at_eof;
    uintptr_t token;
    char path[LIBDRAGON_TRANSLATED_PATH_CAPACITY];
    dir_t iterator;
} libdragon_dir_handle_t;

typedef struct {
    bool active;
    uintptr_t token;
    FILE *file;
} libdragon_file_handle_t;

typedef struct {
    uint32_t magic;
    char prefix[LIBDRAGON_PREFIX_CAPACITY];
    libdragon_dir_handle_t directories[LIBDRAGON_DIR_HANDLE_COUNT];
    libdragon_file_handle_t files[LIBDRAGON_FILE_HANDLE_COUNT];
} libdragon_fs_context_t;

/*
 * Integer handles are never dereferenced. One process-wide issuer makes every
 * directory and file generation unique across all live adapter instances.
 * Real-adapter tokens are odd; the host fake reserves even tokens. Aurora64
 * and its host tests are single threaded, so this deliberately needs no atomic
 * support on N64. Exhaustion is permanent and fails closed rather than
 * wrapping or retaining an unbounded stale-token set.
 */
static uintptr_t next_libdragon_token = (uintptr_t)1U;
static bool libdragon_tokens_exhausted;

#ifdef LIBRARY_FS_HOST_TEST
extern FILE *library_fs_host_test_fopen(const char *path, const char *mode);
#endif

static bool bounded_length(const char *text, size_t capacity, size_t *length)
{
    size_t index;
    if (text == NULL || length == NULL) return false;
    for (index = 0U; index < capacity; ++index) {
        if (text[index] == '\0') {
            *length = index;
            return true;
        }
    }
    return false;
}

static libdragon_fs_context_t *valid_context(void *context)
{
    libdragon_fs_context_t *value = context;
    if (value == NULL || value->magic != LIBDRAGON_CONTEXT_MAGIC) return NULL;
    return value;
}

static bool translate_path(libdragon_fs_context_t *context, const char *logical,
                           char *translated, size_t capacity)
{
    size_t prefix_length;
    size_t logical_length;
    size_t logical_start = 0U;
    size_t total;

    if (context == NULL || translated == NULL || capacity == 0U) return false;
    translated[0] = '\0';
    if (!bounded_length(context->prefix, sizeof(context->prefix), &prefix_length) ||
        !bounded_length(logical, LIBDRAGON_LOGICAL_PATH_CAPACITY, &logical_length) ||
        logical_length == 0U || logical[0] != '/') {
        errno = EINVAL;
        return false;
    }
    if (prefix_length > 0U && context->prefix[prefix_length - 1U] == '/') logical_start = 1U;
    total = prefix_length + logical_length - logical_start;
    if (total >= capacity) {
        errno = ENAMETOOLONG;
        return false;
    }
    memcpy(translated, context->prefix, prefix_length);
    memcpy(translated + prefix_length, logical + logical_start,
           logical_length - logical_start);
    translated[total] = '\0';
    return true;
}

static bool convert_dirent(const dir_t *source, library_dirent_t *destination)
{
    size_t length;
    if (source == NULL || destination == NULL) return false;
    memset(destination, 0, sizeof(*destination));
    if (!bounded_length(source->d_name, sizeof(source->d_name), &length)) {
        errno = EOVERFLOW;
        return false;
    }
    memcpy(destination->basename, source->d_name, length + 1U);
    if (source->d_type == DT_REG) destination->type = LIBRARY_FS_ENTRY_FILE;
    else if (source->d_type == DT_DIR) destination->type = LIBRARY_FS_ENTRY_DIRECTORY;
    else destination->type = LIBRARY_FS_ENTRY_UNKNOWN;
    destination->size = (source->d_size < 0) ? 0U : (uint64_t)source->d_size;
    destination->change_token = source->d_cookie;
    return true;
}

static uintptr_t opaque_token(void *opaque)
{
    return (uintptr_t)opaque;
}

static bool token_available(void)
{
    if (libdragon_tokens_exhausted) {
        errno = EMFILE;
        return false;
    }
    return true;
}

static void *issue_token(void)
{
    uintptr_t token;
    if (!token_available()) return NULL;
    token = next_libdragon_token;
    if (token > UINTPTR_MAX - (uintptr_t)2U) {
        libdragon_tokens_exhausted = true;
    } else {
        next_libdragon_token = token + (uintptr_t)2U;
    }
    return (void *)token;
}

static FILE *backend_file_open(const char *path)
{
#ifdef LIBRARY_FS_HOST_TEST
    return library_fs_host_test_fopen(path, "rb");
#else
    return fopen(path, "rb");
#endif
}

static libdragon_dir_handle_t *find_directory_handle(libdragon_fs_context_t *context,
                                                       void *opaque)
{
    size_t index;
    uintptr_t token = opaque_token(opaque);
    if (context == NULL || token == 0U) return NULL;
    for (index = 0U; index < LIBDRAGON_DIR_HANDLE_COUNT; ++index) {
        if (context->directories[index].token == token) return &context->directories[index];
    }
    return NULL;
}

static libdragon_file_handle_t *find_file_handle(libdragon_fs_context_t *context,
                                                   void *opaque)
{
    size_t index;
    uintptr_t token = opaque_token(opaque);
    if (context == NULL || token == 0U) return NULL;
    for (index = 0U; index < LIBDRAGON_FILE_HANDLE_COUNT; ++index) {
        if (context->files[index].token == token) return &context->files[index];
    }
    return NULL;
}

static libdragon_dir_handle_t *free_directory_slot(libdragon_fs_context_t *context)
{
    size_t index;
    for (index = 0U; index < LIBDRAGON_DIR_HANDLE_COUNT; ++index) {
        if (!context->directories[index].active) return &context->directories[index];
    }
    return NULL;
}

static libdragon_file_handle_t *free_file_slot(libdragon_fs_context_t *context)
{
    size_t index;
    for (index = 0U; index < LIBDRAGON_FILE_HANDLE_COUNT; ++index) {
        if (!context->files[index].active) return &context->files[index];
    }
    return NULL;
}

static void rollback_directory_open(libdragon_dir_handle_t *slot)
{
    int result;
    if (slot == NULL || !slot->active) return;
    slot->token = 0U;
    result = dir_findclose(slot->path, &slot->iterator);
    if (result == 0) {
        memset(slot, 0, sizeof(*slot));
    }
}

static int adapter_dir_open(void *opaque_context, const char *path, void **handle,
                            library_dirent_t *first)
{
    libdragon_fs_context_t *context = valid_context(opaque_context);
    libdragon_dir_handle_t *slot;
    int result;
    int saved_errno;

    if (handle != NULL) *handle = NULL;
    if (first != NULL) memset(first, 0, sizeof(*first));
    if (context == NULL || path == NULL || handle == NULL || first == NULL) {
        errno = EINVAL;
        return LIBRARY_FS_ERROR;
    }
    slot = free_directory_slot(context);
    if (slot == NULL) {
        errno = EMFILE;
        return LIBRARY_FS_ERROR;
    }
    if (!token_available()) return LIBRARY_FS_ERROR;
    memset(slot, 0, sizeof(*slot));
    if (!translate_path(context, path, slot->path, sizeof(slot->path))) return LIBRARY_FS_ERROR;
    result = dir_findfirst(slot->path, &slot->iterator);
    saved_errno = errno;
    if (result == -1) {
        errno = saved_errno;
        return LIBRARY_FS_EOF;
    }
    if (result != 0) {
        errno = saved_errno;
        return LIBRARY_FS_ERROR;
    }
    slot->active = true;
    if (!convert_dirent(&slot->iterator, first)) {
        saved_errno = errno;
        rollback_directory_open(slot);
        memset(first, 0, sizeof(*first));
        errno = saved_errno;
        return LIBRARY_FS_ERROR;
    }
    *handle = issue_token();
    if (*handle == NULL) {
        saved_errno = errno;
        rollback_directory_open(slot);
        memset(first, 0, sizeof(*first));
        errno = saved_errno;
        return LIBRARY_FS_ERROR;
    }
    slot->token = opaque_token(*handle);
    return LIBRARY_FS_ENTRY;
}

static int adapter_dir_next(void *opaque_context, void *handle, library_dirent_t *next)
{
    libdragon_fs_context_t *context = valid_context(opaque_context);
    libdragon_dir_handle_t *slot;
    int result;
    int saved_errno;
    if (next != NULL) memset(next, 0, sizeof(*next));
    if (context == NULL || next == NULL) {
        errno = EINVAL;
        return LIBRARY_FS_ERROR;
    }
    slot = find_directory_handle(context, handle);
    if (slot == NULL || !slot->active) {
        errno = EBADF;
        return LIBRARY_FS_ERROR;
    }
    if (slot->at_eof) {
        errno = EIO;
        return LIBRARY_FS_ERROR;
    }
    result = dir_findnext(slot->path, &slot->iterator);
    saved_errno = errno;
    if (result == 0) {
        if (!convert_dirent(&slot->iterator, next)) return LIBRARY_FS_ERROR;
        return LIBRARY_FS_ENTRY;
    }
    if (result == -1) {
        int eof_errno = saved_errno;
        slot->at_eof = true;
        result = dir_findclose(slot->path, &slot->iterator);
        saved_errno = errno;
        if (result == 0) {
            slot->active = false;
            errno = eof_errno;
            return LIBRARY_FS_EOF;
        }
        errno = saved_errno;
        return LIBRARY_FS_ERROR;
    }
    errno = saved_errno;
    return LIBRARY_FS_ERROR;
}

static int adapter_dir_close(void *opaque_context, void *handle)
{
    libdragon_fs_context_t *context = valid_context(opaque_context);
    libdragon_dir_handle_t *slot;
    int result;
    int saved_errno;
    if (context == NULL) {
        errno = EINVAL;
        return LIBRARY_FS_ERROR;
    }
    slot = find_directory_handle(context, handle);
    if (slot == NULL) {
        errno = EBADF;
        return LIBRARY_FS_ERROR;
    }
    if (!slot->active) return LIBRARY_FS_EOF;
    result = dir_findclose(slot->path, &slot->iterator);
    saved_errno = errno;
    if (result == 0) slot->active = false;
    errno = saved_errno;
    return (result == 0) ? LIBRARY_FS_ENTRY : LIBRARY_FS_ERROR;
}

static int adapter_file_open(void *opaque_context, const char *path, void **handle)
{
    libdragon_fs_context_t *context = valid_context(opaque_context);
    libdragon_file_handle_t *slot;
    char translated[LIBDRAGON_TRANSLATED_PATH_CAPACITY];
    FILE *file;
    int saved_errno;
    if (handle != NULL) *handle = NULL;
    if (context == NULL || path == NULL || handle == NULL) {
        errno = EINVAL;
        return LIBRARY_FS_ERROR;
    }
    slot = free_file_slot(context);
    if (slot == NULL) {
        errno = EMFILE;
        return LIBRARY_FS_ERROR;
    }
    if (!token_available()) return LIBRARY_FS_ERROR;
    if (!translate_path(context, path, translated, sizeof(translated))) return LIBRARY_FS_ERROR;
    file = backend_file_open(translated);
    saved_errno = errno;
    if (file == NULL) {
        errno = saved_errno;
        return LIBRARY_FS_ERROR;
    }
    memset(slot, 0, sizeof(*slot));
    slot->active = true;
    slot->file = file;
    *handle = issue_token();
    if (*handle == NULL) {
        saved_errno = errno;
        slot->active = false;
        slot->file = NULL;
        (void)fclose(file);
        errno = saved_errno;
        return LIBRARY_FS_ERROR;
    }
    slot->token = opaque_token(*handle);
    return LIBRARY_FS_ENTRY;
}

static int64_t adapter_file_read(void *opaque_context, void *handle, void *buffer,
                                 size_t length)
{
    libdragon_fs_context_t *context = valid_context(opaque_context);
    libdragon_file_handle_t *slot;
    size_t amount;
    int saved_errno;
    if (context == NULL || (buffer == NULL && length != 0U)) {
        errno = EINVAL;
        return -1;
    }
    slot = find_file_handle(context, handle);
    if (slot == NULL || !slot->active) {
        errno = EBADF;
        return -1;
    }
    amount = fread(buffer, 1U, length, slot->file);
    saved_errno = errno;
    if (amount == 0U && length != 0U && ferror(slot->file)) {
        errno = (saved_errno == 0) ? EIO : saved_errno;
        return -1;
    }
    errno = saved_errno;
    return (int64_t)amount;
}

static int adapter_file_close(void *opaque_context, void *handle)
{
    libdragon_fs_context_t *context = valid_context(opaque_context);
    libdragon_file_handle_t *slot;
    int result;
    int saved_errno;
    if (context == NULL) {
        errno = EINVAL;
        return LIBRARY_FS_ERROR;
    }
    slot = find_file_handle(context, handle);
    if (slot == NULL) {
        errno = EBADF;
        return LIBRARY_FS_ERROR;
    }
    if (!slot->active) return LIBRARY_FS_EOF;
    slot->active = false;
    result = fclose(slot->file);
    saved_errno = errno;
    slot->file = NULL;
    errno = saved_errno;
    return (result == 0) ? LIBRARY_FS_ENTRY : LIBRARY_FS_ERROR;
}

static int adapter_stat(void *opaque_context, const char *path, library_stat_t *out)
{
    libdragon_fs_context_t *context = valid_context(opaque_context);
    char translated[LIBDRAGON_TRANSLATED_PATH_CAPACITY];
    struct stat value;
    int result;
    int saved_errno;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (context == NULL || path == NULL || out == NULL) {
        errno = EINVAL;
        return LIBRARY_FS_ERROR;
    }
    if (!translate_path(context, path, translated, sizeof(translated))) return LIBRARY_FS_ERROR;
    result = stat(translated, &value);
    saved_errno = errno;
    if (result != 0) {
        errno = saved_errno;
        return LIBRARY_FS_ERROR;
    }
    if (S_ISREG(value.st_mode)) out->type = LIBRARY_FS_ENTRY_FILE;
    else if (S_ISDIR(value.st_mode)) out->type = LIBRARY_FS_ENTRY_DIRECTORY;
    else out->type = LIBRARY_FS_ENTRY_UNKNOWN;
    out->size = (value.st_size < 0) ? 0U : (uint64_t)value.st_size;
    out->modified_time = (int64_t)value.st_mtime;
    out->change_token = (value.st_ctime < 0) ? 0U : (uint64_t)value.st_ctime;
    return LIBRARY_FS_ENTRY;
}

static uint32_t adapter_ticks(void *opaque_context)
{
    if (valid_context(opaque_context) == NULL) return 0U;
    return (uint32_t)get_ticks();
}

bool library_fs_libdragon_init(library_fs_t *out, const char *storage_prefix)
{
    libdragon_fs_context_t *context;
    size_t prefix_length;
    if (out == NULL) {
        errno = EINVAL;
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!bounded_length(storage_prefix, LIBDRAGON_PREFIX_CAPACITY, &prefix_length) ||
        prefix_length == 0U) {
        errno = EINVAL;
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) return false;
    memcpy(context->prefix, storage_prefix, prefix_length + 1U);
    context->magic = LIBDRAGON_CONTEXT_MAGIC;
    out->context = context;
    out->dir_open = adapter_dir_open;
    out->dir_next = adapter_dir_next;
    out->dir_close = adapter_dir_close;
    out->file_open_read = adapter_file_open;
    out->file_read = adapter_file_read;
    out->file_close = adapter_file_close;
    out->stat = adapter_stat;
    out->ticks_now = adapter_ticks;
    return true;
}

#ifdef LIBRARY_FS_HOST_TEST
void library_fs_libdragon_test_get_token_issuer(
    library_fs_libdragon_token_issuer_t *out)
{
    if (out == NULL) return;
    out->next_token = next_libdragon_token;
    out->exhausted = libdragon_tokens_exhausted;
}

void library_fs_libdragon_test_set_token_issuer(
    const library_fs_libdragon_token_issuer_t *state)
{
    if (state == NULL) return;
    next_libdragon_token = state->next_token;
    libdragon_tokens_exhausted = state->exhausted;
}
#endif

void library_fs_libdragon_deinit(library_fs_t *fs)
{
    libdragon_fs_context_t *context;
    size_t index;
    int saved_errno;
    int cleanup_errno = 0;
    if (fs == NULL) return;
    saved_errno = errno;
    context = valid_context(fs->context);
    if (context != NULL) {
        for (index = 0U; index < LIBDRAGON_DIR_HANDLE_COUNT; ++index) {
            if (context->directories[index].active) {
                int result = dir_findclose(context->directories[index].path,
                                           &context->directories[index].iterator);
                if (result != 0 && cleanup_errno == 0) {
                    cleanup_errno = (errno != 0) ? errno : EIO;
                }
                context->directories[index].active = false;
            }
        }
        for (index = 0U; index < LIBDRAGON_FILE_HANDLE_COUNT; ++index) {
            if (context->files[index].active) {
                int result = fclose(context->files[index].file);
                if (result != 0 && cleanup_errno == 0) {
                    cleanup_errno = (errno != 0) ? errno : EIO;
                }
                context->files[index].active = false;
            }
        }
        context->magic = 0U;
        free(context);
    }
    memset(fs, 0, sizeof(*fs));
    errno = (cleanup_errno != 0) ? cleanup_errno : saved_errno;
}
