#include "support/fake_library_fs.h"

#include <errno.h>
#include <string.h>

/*
 * Fake handles share one deterministic, process-wide even-token namespace.
 * This keeps simultaneous fake instances distinct and also keeps fake tokens
 * disjoint from the libdragon adapter's odd namespace. Host tests are single
 * threaded, so no atomics are required. Exhaustion is permanent and closed.
 */
static uintptr_t next_fake_token = (uintptr_t)2U;
static bool fake_tokens_exhausted;

static bool copy_text(char *destination, size_t capacity, const char *source)
{
    size_t index;
    if (destination == NULL || capacity == 0U || source == NULL) return false;
    for (index = 0U; index < capacity; ++index) {
        destination[index] = source[index];
        if (source[index] == '\0') return index > 0U;
    }
    destination[0] = '\0';
    return false;
}

static bool bounded_path_equal(const char *left, const char *right)
{
    size_t index;
    if (left == NULL || right == NULL) return false;
    for (index = 0U; index < FAKE_LIBRARY_FS_PATH_CAPACITY; ++index) {
        if (left[index] != right[index]) return false;
        if (left[index] == '\0') return true;
    }
    return false;
}

static size_t find_directory(const fake_library_fs_t *fake, const char *path)
{
    size_t index;
    if (fake == NULL || path == NULL) return SIZE_MAX;
    for (index = 0U; index < fake->directory_count; ++index) {
        if (bounded_path_equal(fake->directories[index].path, path)) return index;
    }
    return SIZE_MAX;
}

static size_t find_file(const fake_library_fs_t *fake, const char *path)
{
    size_t index;
    if (fake == NULL || path == NULL) return SIZE_MAX;
    for (index = 0U; index < fake->file_count; ++index) {
        if (bounded_path_equal(fake->files[index].path, path)) return index;
    }
    return SIZE_MAX;
}

static fake_library_handle_t *find_handle(fake_library_handle_t *handles, void *handle)
{
    size_t index;
    uintptr_t token = (uintptr_t)handle;
    if (token == 0U) return NULL;
    for (index = 0U; index < FAKE_LIBRARY_FS_MAX_HANDLES; ++index) {
        if (handles[index].token == token) return &handles[index];
    }
    return NULL;
}

static fake_library_handle_t *free_handle(fake_library_handle_t *handles)
{
    size_t index;
    for (index = 0U; index < FAKE_LIBRARY_FS_MAX_HANDLES; ++index) {
        if (!handles[index].active) return &handles[index];
    }
    return NULL;
}

static bool token_available(const fake_library_fs_t *fake)
{
    /* UINTPTR_MAX is a narrow test-only exhaustion injection point. */
    if (fake->next_token == UINTPTR_MAX || fake_tokens_exhausted) {
        errno = EMFILE;
        return false;
    }
    return true;
}

static void *issue_handle(fake_library_fs_t *fake)
{
    uintptr_t token;
    if (!token_available(fake)) return NULL;
    token = next_fake_token;
    if (token > UINTPTR_MAX - (uintptr_t)2U) {
        fake_tokens_exhausted = true;
    } else {
        next_fake_token = token + (uintptr_t)2U;
    }
    fake->next_token = token;
    return (void *)token;
}

static int fake_dir_open_callback(void *context, const char *path, void **handle,
                                  library_dirent_t *first)
{
    fake_library_fs_t *fake = context;
    size_t directory_index;
    fake_library_handle_t *slot;

    if (handle != NULL) *handle = NULL;
    if (first != NULL) memset(first, 0, sizeof(*first));
    if (fake == NULL || path == NULL || handle == NULL || first == NULL) return LIBRARY_FS_ERROR;
    if (!token_available(fake)) return LIBRARY_FS_ERROR;
    ++fake->dir_open_calls;
    if (fake->fail_dir_open_path[0] != '\0' &&
        bounded_path_equal(path, fake->fail_dir_open_path)) {
        return LIBRARY_FS_ERROR;
    }
    directory_index = find_directory(fake, path);
    if (directory_index == SIZE_MAX) return LIBRARY_FS_ERROR;
    if (fake->directories[directory_index].entry_count == 0U) return LIBRARY_FS_EOF;
    slot = free_handle(fake->dir_handles);
    if (slot == NULL) return LIBRARY_FS_ERROR;
    memset(slot, 0, sizeof(*slot));
    slot->active = true;
    slot->object_index = directory_index;
    slot->offset = 1U;
    ++fake->active_dir_handles;
    *first = fake->directories[directory_index].entries[0];
    *handle = issue_handle(fake);
    if (*handle == NULL) {
        slot->active = false;
        --fake->active_dir_handles;
        ++fake->backend_dir_close_calls;
        memset(first, 0, sizeof(*first));
        return LIBRARY_FS_ERROR;
    }
    slot->token = (uintptr_t)*handle;
    return LIBRARY_FS_ENTRY;
}

static int fake_dir_next_callback(void *context, void *handle, library_dirent_t *next)
{
    fake_library_fs_t *fake = context;
    fake_library_handle_t *slot;
    fake_library_directory_t *directory;

    if (next != NULL) memset(next, 0, sizeof(*next));
    if (fake == NULL || next == NULL) return LIBRARY_FS_ERROR;
    slot = find_handle(fake->dir_handles, handle);
    if (slot == NULL || !slot->active) return LIBRARY_FS_ERROR;
    ++fake->dir_next_calls;
    if (fake->fail_dir_next_call != 0U && fake->dir_next_calls == fake->fail_dir_next_call) {
        return LIBRARY_FS_ERROR;
    }
    directory = &fake->directories[slot->object_index];
    if (slot->offset < directory->entry_count) {
        *next = directory->entries[slot->offset++];
        return LIBRARY_FS_ENTRY;
    }
    slot->active = false;
    --fake->active_dir_handles;
    ++fake->backend_dir_close_calls;
    return LIBRARY_FS_EOF;
}

static int fake_dir_close_callback(void *context, void *handle)
{
    fake_library_fs_t *fake = context;
    fake_library_handle_t *slot;
    if (fake == NULL) return LIBRARY_FS_ERROR;
    slot = find_handle(fake->dir_handles, handle);
    if (slot == NULL) return LIBRARY_FS_ERROR;
    if (!slot->active) return LIBRARY_FS_EOF;
    slot->active = false;
    --fake->active_dir_handles;
    ++fake->dir_close_calls;
    ++fake->backend_dir_close_calls;
    return LIBRARY_FS_ENTRY;
}

static int fake_file_open_callback(void *context, const char *path, void **handle)
{
    fake_library_fs_t *fake = context;
    fake_library_handle_t *slot;
    size_t file_index;
    if (handle != NULL) *handle = NULL;
    if (fake == NULL || path == NULL || handle == NULL) return LIBRARY_FS_ERROR;
    if (!token_available(fake)) return LIBRARY_FS_ERROR;
    ++fake->file_open_calls;
    file_index = find_file(fake, path);
    slot = free_handle(fake->file_handles);
    if (file_index == SIZE_MAX || slot == NULL) {
        return LIBRARY_FS_ERROR;
    }
    memset(slot, 0, sizeof(*slot));
    slot->active = true;
    slot->object_index = file_index;
    ++fake->active_file_handles;
    *handle = issue_handle(fake);
    if (*handle == NULL) {
        slot->active = false;
        --fake->active_file_handles;
        ++fake->backend_file_close_calls;
        return LIBRARY_FS_ERROR;
    }
    slot->token = (uintptr_t)*handle;
    return LIBRARY_FS_ENTRY;
}

static int64_t fake_file_read_callback(void *context, void *handle, void *buffer,
                                       size_t length)
{
    fake_library_fs_t *fake = context;
    fake_library_handle_t *slot;
    fake_library_file_t *file;
    size_t available;
    size_t amount;
    if (fake == NULL || (buffer == NULL && length != 0U)) return -1;
    slot = find_handle(fake->file_handles, handle);
    if (slot == NULL || !slot->active) return -1;
    ++fake->file_read_calls;
    if (fake->fail_file_read) return -1;
    file = &fake->files[slot->object_index];
    available = file->length - slot->offset;
    amount = (length < available) ? length : available;
    if (amount != 0U) memcpy(buffer, file->data + slot->offset, amount);
    slot->offset += amount;
    return (int64_t)amount;
}

static int fake_file_close_callback(void *context, void *handle)
{
    fake_library_fs_t *fake = context;
    fake_library_handle_t *slot;
    if (fake == NULL) return LIBRARY_FS_ERROR;
    slot = find_handle(fake->file_handles, handle);
    if (slot == NULL) return LIBRARY_FS_ERROR;
    if (!slot->active) return LIBRARY_FS_EOF;
    slot->active = false;
    --fake->active_file_handles;
    ++fake->file_close_calls;
    ++fake->backend_file_close_calls;
    return LIBRARY_FS_ENTRY;
}

static int fake_stat_callback(void *context, const char *path, library_stat_t *out)
{
    fake_library_fs_t *fake = context;
    size_t index;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (fake == NULL || path == NULL || out == NULL) return LIBRARY_FS_ERROR;
    ++fake->stat_calls;
    if (fake->fail_stat) return LIBRARY_FS_ERROR;
    index = find_file(fake, path);
    if (index != SIZE_MAX) {
        out->type = LIBRARY_FS_ENTRY_FILE;
        out->size = fake->files[index].length;
        out->modified_time = fake->files[index].modified_time;
        out->change_token = (uint64_t)fake->files[index].modified_time;
        return LIBRARY_FS_ENTRY;
    }
    index = find_directory(fake, path);
    if (index != SIZE_MAX) {
        out->type = LIBRARY_FS_ENTRY_DIRECTORY;
        return LIBRARY_FS_ENTRY;
    }
    return LIBRARY_FS_ERROR;
}

static uint32_t fake_ticks_callback(void *context)
{
    fake_library_fs_t *fake = context;
    return (fake == NULL) ? 0U : fake->ticks;
}

void fake_library_fs_init(fake_library_fs_t *fake)
{
    if (fake == NULL) return;
    memset(fake, 0, sizeof(*fake));
    fake->interface.context = fake;
    fake->interface.dir_open = fake_dir_open_callback;
    fake->interface.dir_next = fake_dir_next_callback;
    fake->interface.dir_close = fake_dir_close_callback;
    fake->interface.file_open_read = fake_file_open_callback;
    fake->interface.file_read = fake_file_read_callback;
    fake->interface.file_close = fake_file_close_callback;
    fake->interface.stat = fake_stat_callback;
    fake->interface.ticks_now = fake_ticks_callback;
}

library_fs_t *fake_library_fs_interface(fake_library_fs_t *fake)
{
    return (fake == NULL) ? NULL : &fake->interface;
}

bool fake_library_fs_add_directory(fake_library_fs_t *fake, const char *path,
                                   const library_dirent_t *entries, size_t count)
{
    fake_library_directory_t *directory;
    if (fake == NULL || count > FAKE_LIBRARY_FS_MAX_ENTRIES ||
        (entries == NULL && count != 0U) ||
        fake->directory_count >= FAKE_LIBRARY_FS_MAX_DIRECTORIES) return false;
    directory = &fake->directories[fake->directory_count];
    memset(directory, 0, sizeof(*directory));
    if (!copy_text(directory->path, sizeof(directory->path), path)) return false;
    if (count != 0U) memcpy(directory->entries, entries, count * sizeof(*entries));
    directory->entry_count = count;
    ++fake->directory_count;
    return true;
}

bool fake_library_fs_add_file(fake_library_fs_t *fake, const char *path,
                              const uint8_t *data, size_t length,
                              int64_t modified_time)
{
    fake_library_file_t *file;
    if (fake == NULL || length > FAKE_LIBRARY_FS_MAX_FILE_BYTES ||
        (data == NULL && length != 0U) || fake->file_count >= FAKE_LIBRARY_FS_MAX_FILES) return false;
    file = &fake->files[fake->file_count];
    memset(file, 0, sizeof(*file));
    if (!copy_text(file->path, sizeof(file->path), path)) return false;
    if (length != 0U) memcpy(file->data, data, length);
    file->length = length;
    file->modified_time = modified_time;
    ++fake->file_count;
    return true;
}

void fake_library_fs_fail_dir_open(fake_library_fs_t *fake, const char *path)
{
    if (fake == NULL) return;
    fake->fail_dir_open_path[0] = '\0';
    (void)copy_text(fake->fail_dir_open_path, sizeof(fake->fail_dir_open_path), path);
}

void fake_library_fs_fail_dir_next(fake_library_fs_t *fake, size_t call_number)
{
    if (fake != NULL) fake->fail_dir_next_call = call_number;
}

void fake_library_fs_fail_file_read(fake_library_fs_t *fake, bool fail)
{
    if (fake != NULL) fake->fail_file_read = fail;
}

void fake_library_fs_fail_stat(fake_library_fs_t *fake, bool fail)
{
    if (fake != NULL) fake->fail_stat = fail;
}

void fake_library_fs_set_ticks(fake_library_fs_t *fake, uint32_t ticks)
{
    if (fake != NULL) fake->ticks = ticks;
}
