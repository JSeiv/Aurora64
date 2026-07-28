#include "menu/library/library_scanner.h"

#include <stdlib.h>
#include <string.h>

#define LIBRARY_SCANNER_MAX_SOURCES 512U

typedef struct {
    size_t path_offset;
    uint8_t depth;
} scan_path_t;

typedef enum {
    CANDIDATE_CLOSE_NONE,
    CANDIDATE_CLOSE_SKIP,
    CANDIDATE_CLOSE_RETRY
} candidate_close_action_t;

struct library_scanner {
    library_fs_t *fs;
    library_roots_t roots;
    library_allocator_t allocator;
    library_scanner_state_t state;
    library_scan_phase_t phase;
    uint32_t generation;
    library_scanner_stats_t stats;

    scan_path_t *queue;
    size_t queue_head;
    size_t queue_count;
    char *arena;
    size_t arena_used;
    uint8_t *read_buffer;
    uint8_t *normalize_buffer;

    library_scanner_record_t *records;
    size_t record_count;
    size_t logical_record_count;

    bool current_directory_valid;
    scan_path_t current_directory;
    void *directory_handle;
    size_t directory_entries_consumed;
    bool pending_entry_valid;
    library_dirent_t pending_entry;
    size_t replay_remaining;
    bool replaying;
    bool resume_candidate;

    bool candidate_valid;
    size_t candidate_path_offset;
    void *file_handle;
    library_stat_t pass_stat;
    rom_header_t header;
    uint8_t header_bytes[ROM_HEADER_METADATA_BYTES];
    size_t header_length;
    uint64_t pass_bytes;
    rom_identity_ctx_t identity;
    uint8_t normalize_carry[4];
    size_t normalize_carry_length;
    uint64_t normalized_offset;
    uint32_t header_crc;
    uint32_t sample_crc;
    rom_fingerprint_t first_fingerprint;
    library_source_signature_t first_signature;
    library_source_signature_t second_signature;
    unsigned int pass_number;
    unsigned int retry_count;
    candidate_close_action_t candidate_close_action;
    size_t top_level_directory_count;
};

static void *libc_malloc(void *context, size_t size)
{
    (void)context;
    return malloc(size);
}

static void *libc_calloc(void *context, size_t count, size_t size)
{
    (void)context;
    return calloc(count, size);
}

static void libc_free(void *context, void *pointer)
{
    (void)context;
    free(pointer);
}

static bool allocator_valid(const library_allocator_t *allocator)
{
    return allocator != NULL && allocator->malloc_fn != NULL &&
           allocator->calloc_fn != NULL && allocator->free_fn != NULL;
}

static const char *arena_path(const library_scanner_t *scanner, size_t offset)
{
    return scanner->arena + offset;
}

static bool append_path(library_scanner_t *scanner, const char *path,
                        size_t *offset_out)
{
    size_t length;
    if (path == NULL || offset_out == NULL) return false;
    length = strlen(path);
    if (length + 1U > LIBRARY_SCANNER_PATH_BYTES ||
        length + 1U > LIBRARY_SCANNER_PATH_ARENA_BYTES - scanner->arena_used) {
        return false;
    }
    *offset_out = scanner->arena_used;
    memcpy(scanner->arena + scanner->arena_used, path, length + 1U);
    scanner->arena_used += length + 1U;
    return true;
}

static bool join_path(library_scanner_t *scanner, const char *directory,
                      const char *basename, size_t *offset_out)
{
    char path[LIBRARY_SCANNER_PATH_BYTES];
    size_t directory_length;
    size_t basename_length;
    size_t needed;
    size_t at;

    if (directory == NULL || basename == NULL || offset_out == NULL) return false;
    directory_length = strlen(directory);
    basename_length = strlen(basename);
    if (basename_length == 0U || basename_length >= LIBRARY_FS_BASENAME_CAPACITY ||
        strchr(basename, '/') != NULL) return false;
    needed = directory_length + basename_length + 1U;
    if (!(directory_length == 1U && directory[0] == '/')) ++needed;
    if (needed > sizeof(path)) return false;
    memcpy(path, directory, directory_length);
    at = directory_length;
    if (!(directory_length == 1U && directory[0] == '/')) path[at++] = '/';
    memcpy(path + at, basename, basename_length + 1U);
    return append_path(scanner, path, offset_out);
}

static bool queue_path(library_scanner_t *scanner, size_t offset, uint8_t depth)
{
    if (scanner->queue_count >= LIBRARY_SCANNER_MAX_PATHS) return false;
    scanner->queue[scanner->queue_count].path_offset = offset;
    scanner->queue[scanner->queue_count].depth = depth;
    ++scanner->queue_count;
    return true;
}

static bool close_file(library_scanner_t *scanner)
{
    if (scanner->file_handle != NULL) {
        if (scanner->fs->file_close(scanner->fs->context,
                                    scanner->file_handle) == LIBRARY_FS_ERROR) {
            return false;
        }
        scanner->file_handle = NULL;
    }
    return true;
}

static bool close_directory(library_scanner_t *scanner)
{
    if (scanner->directory_handle != NULL) {
        if (scanner->fs->dir_close(scanner->fs->context,
                                   scanner->directory_handle) == LIBRARY_FS_ERROR) {
            return false;
        }
        scanner->directory_handle = NULL;
    }
    return true;
}

static bool close_all(library_scanner_t *scanner)
{
    bool file_closed = close_file(scanner);
    bool directory_closed = close_directory(scanner);
    return file_closed && directory_closed;
}

static void reset_generation_work(library_scanner_t *scanner)
{
    (void)close_all(scanner);
    scanner->queue_head = 0U;
    scanner->queue_count = 0U;
    scanner->arena_used = 0U;
    scanner->record_count = 0U;
    scanner->logical_record_count = 0U;
    if (scanner->records != NULL) {
        memset(scanner->records, 0,
               LIBRARY_SCANNER_MAX_SOURCES * sizeof(*scanner->records));
    }
    memset(&scanner->stats, 0, sizeof(scanner->stats));
    scanner->stats.clean = true;
    scanner->current_directory_valid = false;
    scanner->directory_entries_consumed = 0U;
    scanner->pending_entry_valid = false;
    scanner->replay_remaining = 0U;
    scanner->replaying = false;
    scanner->resume_candidate = false;
    scanner->candidate_valid = false;
    scanner->retry_count = 0U;
    scanner->candidate_close_action = CANDIDATE_CLOSE_NONE;
    scanner->top_level_directory_count = 0U;
    scanner->phase = LIBRARY_SCAN_PHASE_NORMALIZE_ROOTS;
}

static void fail_generation(library_scanner_t *scanner, bool capacity)
{
    scanner->pending_entry_valid = false;
    scanner->record_count = 0U;
    scanner->stats.clean = false;
    ++scanner->stats.fatal_errors;
    if (capacity) ++scanner->stats.capacity_failures;
    scanner->phase = LIBRARY_SCAN_PHASE_FAIL;
    if (close_all(scanner)) scanner->state = LIBRARY_SCANNER_FAILED;
}

static void fail_candidate(library_scanner_t *scanner, bool mutation)
{
    scanner->stats.clean = false;
    ++scanner->stats.candidate_failures;
    if (mutation) ++scanner->stats.mutation_failures;
    scanner->candidate_close_action = CANDIDATE_CLOSE_SKIP;
    scanner->phase = LIBRARY_SCAN_PHASE_CLOSE_CANDIDATE;
}

static unsigned char ascii_lower(unsigned char value)
{
    if (value >= (unsigned char)'A' && value <= (unsigned char)'Z') {
        return (unsigned char)(value + ((unsigned char)'a' - (unsigned char)'A'));
    }
    return value;
}

static bool candidate_extension(const char *basename)
{
    const char *dot;
    char extension[5];
    size_t index;
    if (basename == NULL) return false;
    dot = strrchr(basename, '.');
    if (dot == NULL || strlen(dot + 1) < 3U || strlen(dot + 1) > 4U) return false;
    memset(extension, 0, sizeof(extension));
    for (index = 0U; dot[1U + index] != '\0'; ++index) {
        extension[index] = (char)ascii_lower((unsigned char)dot[1U + index]);
    }
    return strcmp(extension, "z64") == 0 || strcmp(extension, "v64") == 0 ||
           strcmp(extension, "n64") == 0 || strcmp(extension, "rom") == 0;
}

static uint32_t crc32_byte(uint32_t crc, uint8_t value)
{
    unsigned int bit;
    crc ^= value;
    for (bit = 0U; bit < 8U; ++bit) {
        uint32_t mask = (uint32_t)(0U - (crc & 1U));
        crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
    return crc;
}

static uint32_t crc32_data(uint32_t crc, const uint8_t *bytes, size_t length)
{
    size_t index;
    for (index = 0U; index < length; ++index) crc = crc32_byte(crc, bytes[index]);
    return crc;
}

static bool sampled_index(uint64_t index, uint64_t size)
{
    uint64_t middle = size / 2U;
    return index < 64U || (index >= middle && index - middle < 64U) ||
           (size <= 64U || index >= size - 64U);
}

static void sample_normalized(library_scanner_t *scanner, const uint8_t *bytes,
                              size_t length)
{
    size_t index;
    for (index = 0U; index < length; ++index) {
        if (sampled_index(scanner->normalized_offset, scanner->pass_stat.size)) {
            scanner->sample_crc = crc32_byte(scanner->sample_crc, bytes[index]);
        }
        ++scanner->normalized_offset;
    }
}

static bool signatures_equal(const library_source_signature_t *left,
                             const library_source_signature_t *right)
{
    return left->size == right->size &&
           left->modified_time == right->modified_time &&
           left->normalized_header_crc32 == right->normalized_header_crc32 &&
           left->normalized_sample_crc32 == right->normalized_sample_crc32;
}

static bool stat_candidate(library_scanner_t *scanner, library_stat_t *out)
{
    const char *path = arena_path(scanner, scanner->candidate_path_offset);
    memset(out, 0, sizeof(*out));
    return scanner->fs->stat(scanner->fs->context, path, out) == LIBRARY_FS_ENTRY &&
           out->type == LIBRARY_FS_ENTRY_FILE && out->size >= ROM_HEADER_METADATA_BYTES;
}

static bool begin_pass(library_scanner_t *scanner, unsigned int pass_number)
{
    const char *path = arena_path(scanner, scanner->candidate_path_offset);
    library_stat_t current;
    if (!stat_candidate(scanner, &current)) return false;
    if (pass_number == 2U &&
        (current.size != scanner->first_signature.size ||
         current.modified_time != scanner->first_signature.modified_time)) return false;
    scanner->pass_stat = current;
    scanner->file_handle = NULL;
    if (scanner->fs->file_open_read(scanner->fs->context, path,
                                    &scanner->file_handle) != LIBRARY_FS_ENTRY ||
        scanner->file_handle == NULL) return false;
    scanner->pass_number = pass_number;
    scanner->header_length = 0U;
    scanner->pass_bytes = 0U;
    scanner->normalize_carry_length = 0U;
    scanner->normalized_offset = 0U;
    scanner->header_crc = 0xffffffffU;
    scanner->sample_crc = 0xffffffffU;
    memset(&scanner->identity, 0, sizeof(scanner->identity));
    scanner->phase = LIBRARY_SCAN_PHASE_READ_HEADER;
    return true;
}

static bool update_hash_chunk(library_scanner_t *scanner, const uint8_t *bytes,
                              size_t length)
{
    size_t produced;
    if (!rom_identity_update(&scanner->identity, bytes, length)) return false;
    produced = rom_normalize_bytes(scanner->header.byte_order, bytes, length,
                                   scanner->normalize_carry,
                                   &scanner->normalize_carry_length,
                                   scanner->normalize_buffer,
                                   LIBRARY_SCANNER_READ_BYTES, false);
    if (produced == SIZE_MAX) return false;
    sample_normalized(scanner, scanner->normalize_buffer, produced);
    return true;
}

typedef enum {
    HASH_FINISH_PENDING,
    HASH_FINISH_OK,
    HASH_FINISH_MUTATED,
    HASH_FINISH_ERROR
} hash_finish_result_t;

static hash_finish_result_t finish_hash_pass(
    library_scanner_t *scanner, rom_fingerprint_t *fingerprint,
    library_source_signature_t *signature)
{
    library_stat_t after;
    size_t produced;
    if (!close_file(scanner)) return HASH_FINISH_PENDING;
    produced = rom_normalize_bytes(scanner->header.byte_order, NULL, 0U,
                                   scanner->normalize_carry,
                                   &scanner->normalize_carry_length,
                                   scanner->normalize_buffer,
                                   LIBRARY_SCANNER_READ_BYTES, true);
    if (produced == SIZE_MAX || produced != 0U ||
        !rom_identity_finish(&scanner->identity, fingerprint)) return HASH_FINISH_ERROR;
    if (!stat_candidate(scanner, &after)) return HASH_FINISH_MUTATED;
    signature->size = after.size;
    signature->modified_time = after.modified_time;
    signature->normalized_header_crc32 = scanner->header_crc ^ 0xffffffffU;
    signature->normalized_sample_crc32 = scanner->sample_crc ^ 0xffffffffU;
    if (scanner->pass_bytes != scanner->pass_stat.size ||
        after.size != scanner->pass_stat.size ||
        after.modified_time != scanner->pass_stat.modified_time) {
        return HASH_FINISH_MUTATED;
    }
    return HASH_FINISH_OK;
}

static void retry_or_skip_mutation(library_scanner_t *scanner)
{
    if (scanner->retry_count == 0U) {
        scanner->candidate_close_action = CANDIDATE_CLOSE_RETRY;
        scanner->phase = LIBRARY_SCAN_PHASE_CLOSE_CANDIDATE;
    } else {
        fail_candidate(scanner, true);
    }
}

static bool finish_candidate_close(library_scanner_t *scanner)
{
    candidate_close_action_t action;

    if (!close_file(scanner)) return false;
    action = scanner->candidate_close_action;
    scanner->candidate_close_action = CANDIDATE_CLOSE_NONE;
    if (action == CANDIDATE_CLOSE_RETRY) {
        ++scanner->retry_count;
        scanner->phase = LIBRARY_SCAN_PHASE_OPEN_CANDIDATE;
        return true;
    }
    if (action == CANDIDATE_CLOSE_SKIP) {
        scanner->candidate_valid = false;
        scanner->phase = LIBRARY_SCAN_PHASE_CONSUME_ENTRY;
        return true;
    }
    fail_generation(scanner, false);
    return false;
}

static bool add_root_paths(library_scanner_t *scanner)
{
    size_t index;
    if (scanner->roots.count == 0U ||
        scanner->roots.count > LIBRARY_ROOT_MAX_EFFECTIVE) return false;
    for (index = 0U; index < scanner->roots.count; ++index) {
        char normalized[LIBRARY_SCANNER_PATH_BYTES];
        size_t offset;
        if (!library_root_normalize(scanner->roots.paths[index], normalized,
                                    sizeof(normalized)) ||
            !append_path(scanner, normalized, &offset) ||
            !queue_path(scanner, offset, 0U)) return false;
    }
    return true;
}

bool library_scanner_create(library_scanner_t **out, library_fs_t *fs,
                            const library_roots_t *roots,
                            const library_allocator_t *allocator)
{
    library_allocator_t selected;
    library_scanner_t *scanner;
    if (out != NULL) *out = NULL;
    if (out == NULL || fs == NULL || roots == NULL || roots->count == 0U ||
        roots->count > LIBRARY_ROOT_MAX_EFFECTIVE || fs->dir_open == NULL ||
        fs->dir_next == NULL || fs->dir_close == NULL ||
        fs->file_open_read == NULL || fs->file_read == NULL ||
        fs->file_close == NULL || fs->stat == NULL) return false;
    if (allocator == NULL) {
        selected.context = NULL;
        selected.malloc_fn = libc_malloc;
        selected.calloc_fn = libc_calloc;
        selected.free_fn = libc_free;
    } else if (allocator_valid(allocator)) {
        selected = *allocator;
    } else {
        return false;
    }
    scanner = selected.calloc_fn(selected.context, 1U, sizeof(*scanner));
    if (scanner == NULL) return false;
    scanner->allocator = selected;
    scanner->records = selected.calloc_fn(selected.context,
                                           LIBRARY_SCANNER_MAX_SOURCES,
                                           sizeof(*scanner->records));
    scanner->queue = selected.calloc_fn(selected.context,
                                         LIBRARY_SCANNER_MAX_PATHS,
                                         sizeof(*scanner->queue));
    scanner->arena = selected.malloc_fn(selected.context,
                                        LIBRARY_SCANNER_PATH_ARENA_BYTES);
    scanner->read_buffer = selected.malloc_fn(selected.context,
                                               LIBRARY_SCANNER_READ_BYTES);
    scanner->normalize_buffer = selected.malloc_fn(selected.context,
                                                    LIBRARY_SCANNER_READ_BYTES);
    if (scanner->records == NULL || scanner->queue == NULL ||
        scanner->arena == NULL || scanner->read_buffer == NULL ||
        scanner->normalize_buffer == NULL) {
        library_scanner_destroy(scanner);
        return false;
    }
    scanner->fs = fs;
    scanner->roots = *roots;
    scanner->state = LIBRARY_SCANNER_IDLE;
    scanner->phase = LIBRARY_SCAN_PHASE_NORMALIZE_ROOTS;
    scanner->stats.clean = true;
    *out = scanner;
    return true;
}

bool library_scanner_destroy(library_scanner_t *scanner)
{
    library_allocator_t allocator;
    if (scanner == NULL) return true;
    if (!close_all(scanner)) return false;
    allocator = scanner->allocator;
    allocator.free_fn(allocator.context, scanner->normalize_buffer);
    allocator.free_fn(allocator.context, scanner->read_buffer);
    allocator.free_fn(allocator.context, scanner->arena);
    allocator.free_fn(allocator.context, scanner->queue);
    allocator.free_fn(allocator.context, scanner->records);
    allocator.free_fn(allocator.context, scanner);
    return true;
}

bool library_scanner_snapshot_detach_internal(
    library_scanner_t *scanner, library_scanner_record_t **records_out,
    size_t *record_count_out, char **arena_out, size_t *arena_used_out,
    size_t *records_allocation_out, size_t *arena_allocation_out,
    library_allocator_t *allocator_out)
{
    library_allocator_t allocator;
    if (scanner == NULL || records_out == NULL || record_count_out == NULL ||
        arena_out == NULL || arena_used_out == NULL || records_allocation_out == NULL ||
        arena_allocation_out == NULL || allocator_out == NULL ||
        scanner->state != LIBRARY_SCANNER_COMPLETE || !scanner->stats.clean ||
        scanner->records == NULL || scanner->arena == NULL ||
        scanner->file_handle != NULL || scanner->directory_handle != NULL) {
        return false;
    }
    allocator = scanner->allocator;
    allocator.free_fn(allocator.context, scanner->normalize_buffer);
    allocator.free_fn(allocator.context, scanner->read_buffer);
    allocator.free_fn(allocator.context, scanner->queue);
    scanner->normalize_buffer = NULL;
    scanner->read_buffer = NULL;
    scanner->queue = NULL;

    *records_out = scanner->records;
    *record_count_out = scanner->record_count;
    *arena_out = scanner->arena;
    *arena_used_out = scanner->arena_used;
    *records_allocation_out =
        LIBRARY_SCANNER_MAX_SOURCES * sizeof(*scanner->records);
    *arena_allocation_out = LIBRARY_SCANNER_PATH_ARENA_BYTES;
    *allocator_out = allocator;

    scanner->records = NULL;
    scanner->record_count = 0U;
    scanner->logical_record_count = 0U;
    scanner->arena = NULL;
    scanner->arena_used = 0U;
    return true;
}

bool library_scanner_start(library_scanner_t *scanner)
{
    if (scanner == NULL || scanner->state != LIBRARY_SCANNER_IDLE ||
        scanner->records == NULL || scanner->arena == NULL || scanner->queue == NULL ||
        scanner->read_buffer == NULL || scanner->normalize_buffer == NULL) return false;
    reset_generation_work(scanner);
    ++scanner->generation;
    scanner->state = LIBRARY_SCANNER_SCANNING;
    return true;
}

bool library_scanner_restart(library_scanner_t *scanner)
{
    if (scanner == NULL || scanner->records == NULL || scanner->arena == NULL ||
        scanner->queue == NULL || scanner->read_buffer == NULL ||
        scanner->normalize_buffer == NULL || !close_all(scanner)) return false;
    reset_generation_work(scanner);
    ++scanner->generation;
    scanner->state = LIBRARY_SCANNER_SCANNING;
    return true;
}

void library_scanner_request_pause(library_scanner_t *scanner)
{
    if (scanner != NULL && scanner->state == LIBRARY_SCANNER_SCANNING) {
        scanner->state = LIBRARY_SCANNER_PAUSE_REQUESTED;
    }
}

bool library_scanner_resume(library_scanner_t *scanner)
{
    if (scanner == NULL || scanner->state != LIBRARY_SCANNER_QUIESCED) return false;
    scanner->state = LIBRARY_SCANNER_SCANNING;
    if (scanner->current_directory_valid) {
        scanner->replaying = true;
        scanner->replay_remaining = scanner->directory_entries_consumed;
        scanner->phase = LIBRARY_SCAN_PHASE_OPEN_DIRECTORY;
    }
    return true;
}

void library_scanner_request_cancel(library_scanner_t *scanner)
{
    if (scanner != NULL && scanner->state != LIBRARY_SCANNER_IDLE) {
        scanner->state = LIBRARY_SCANNER_CANCEL_REQUESTED;
    }
}

static bool tick_expired(library_scanner_t *scanner, uint32_t start,
                         uint32_t maximum)
{
    uint32_t now;
    if (maximum == 0U || scanner->fs->ticks_now == NULL) return false;
    now = scanner->fs->ticks_now(scanner->fs->context);
    return (uint32_t)(now - start) >= maximum;
}

static bool retrieve_next_entry(library_scanner_t *scanner,
                                uint32_t *entries_used,
                                const library_scan_budget_t *budget)
{
    int result;
    library_dirent_t value;
    if (*entries_used >= budget->max_directory_entries) return false;
    memset(&value, 0, sizeof(value));
    result = scanner->fs->dir_next(scanner->fs->context,
                                   scanner->directory_handle, &value);
    if (result == LIBRARY_FS_ERROR) {
        fail_generation(scanner, false);
        return false;
    }
    if (result == LIBRARY_FS_EOF) {
        scanner->directory_handle = NULL;
        scanner->current_directory_valid = false;
        scanner->phase = LIBRARY_SCAN_PHASE_OPEN_DIRECTORY;
        return true;
    }
    ++*entries_used;
    if (scanner->replaying) {
        if (scanner->replay_remaining == 0U) {
            fail_generation(scanner, false);
            return false;
        }
        --scanner->replay_remaining;
        if (scanner->replay_remaining == 0U) {
            scanner->replaying = false;
            scanner->phase = scanner->resume_candidate
                                 ? LIBRARY_SCAN_PHASE_OPEN_CANDIDATE
                                 : LIBRARY_SCAN_PHASE_CONSUME_ENTRY;
            scanner->resume_candidate = false;
        }
    } else {
        ++scanner->directory_entries_consumed;
        scanner->pending_entry = value;
        scanner->pending_entry_valid = true;
    }
    return true;
}

static bool open_next_directory(library_scanner_t *scanner,
                                uint32_t *entries_used,
                                const library_scan_budget_t *budget)
{
    int result;
    library_dirent_t first;
    const char *path;

    if (!scanner->current_directory_valid) {
        if (scanner->queue_head >= scanner->queue_count) {
            scanner->state = LIBRARY_SCANNER_COMPLETE;
            scanner->phase = LIBRARY_SCAN_PHASE_COMPLETE;
            return true;
        }
        scanner->current_directory = scanner->queue[scanner->queue_head++];
        scanner->current_directory_valid = true;
        scanner->directory_entries_consumed = 0U;
    }
    if (*entries_used >= budget->max_directory_entries) return false;
    path = arena_path(scanner, scanner->current_directory.path_offset);
    memset(&first, 0, sizeof(first));
    scanner->directory_handle = NULL;
    result = scanner->fs->dir_open(scanner->fs->context, path,
                                   &scanner->directory_handle, &first);
    if (result == LIBRARY_FS_ERROR) {
        fail_generation(scanner, false);
        return false;
    }
    if (result == LIBRARY_FS_EOF) {
        scanner->current_directory_valid = false;
        scanner->directory_handle = NULL;
        return true;
    }
    ++*entries_used;
    if (scanner->replaying) {
        if (scanner->replay_remaining == 0U) {
            fail_generation(scanner, false);
            return false;
        }
        --scanner->replay_remaining;
        if (scanner->replay_remaining == 0U) {
            scanner->replaying = false;
            scanner->phase = scanner->resume_candidate
                                 ? LIBRARY_SCAN_PHASE_OPEN_CANDIDATE
                                 : LIBRARY_SCAN_PHASE_CONSUME_ENTRY;
            scanner->resume_candidate = false;
        } else {
            scanner->phase = LIBRARY_SCAN_PHASE_CONSUME_ENTRY;
        }
    } else {
        scanner->directory_entries_consumed = 1U;
        scanner->pending_entry = first;
        scanner->pending_entry_valid = true;
        scanner->phase = LIBRARY_SCAN_PHASE_CONSUME_ENTRY;
    }
    return true;
}

static void consume_pending(library_scanner_t *scanner)
{
    library_dirent_t entry_value;
    const char *directory;
    size_t offset;
    bool root;

    if (!scanner->pending_entry_valid) return;
    entry_value = scanner->pending_entry;
    scanner->pending_entry_valid = false;
    directory = arena_path(scanner, scanner->current_directory.path_offset);
    root = scanner->current_directory.depth == 0U;
    if (library_basename_is_excluded(root, entry_value.basename)) return;
    if (entry_value.type == LIBRARY_FS_ENTRY_DIRECTORY) {
        scanner->phase = LIBRARY_SCAN_PHASE_QUEUE_CHILD;
        if (root && ++scanner->top_level_directory_count > 32U) {
            fail_generation(scanner, true);
            return;
        }
        if (scanner->current_directory.depth >= LIBRARY_SCANNER_MAX_DEPTH ||
            !join_path(scanner, directory, entry_value.basename, &offset) ||
            !queue_path(scanner, offset,
                        (uint8_t)(scanner->current_directory.depth + 1U))) {
            fail_generation(scanner, true);
            return;
        }
        scanner->phase = LIBRARY_SCAN_PHASE_CONSUME_ENTRY;
        return;
    }
    if (entry_value.type != LIBRARY_FS_ENTRY_FILE) return;
    ++scanner->stats.files_seen;
    if (!candidate_extension(entry_value.basename)) return;
    ++scanner->stats.candidates_seen;
    if (!join_path(scanner, directory, entry_value.basename, &offset)) {
        fail_generation(scanner, true);
        return;
    }
    scanner->candidate_valid = true;
    scanner->candidate_path_offset = offset;
    scanner->retry_count = 0U;
    scanner->phase = LIBRARY_SCAN_PHASE_OPEN_CANDIDATE;
}

static bool read_hash_data(library_scanner_t *scanner, uint32_t *bytes_used,
                           const library_scan_budget_t *budget)
{
    size_t wanted;
    int64_t received;

    if (*bytes_used >= budget->max_read_bytes) return false;
    if (scanner->pass_bytes >= scanner->pass_stat.size) {
        rom_fingerprint_t fingerprint;
        library_source_signature_t signature;
        hash_finish_result_t finished = finish_hash_pass(scanner, &fingerprint,
                                                         &signature);
        if (finished == HASH_FINISH_PENDING) return false;
        if (finished == HASH_FINISH_MUTATED) {
            retry_or_skip_mutation(scanner);
            return true;
        }
        if (finished == HASH_FINISH_ERROR) {
            fail_candidate(scanner, false);
            return false;
        }
        if (scanner->pass_number == 1U) {
            scanner->first_fingerprint = fingerprint;
            scanner->first_signature = signature;
            scanner->phase = LIBRARY_SCAN_PHASE_REOPEN_CANDIDATE;
        } else {
            scanner->second_signature = signature;
            scanner->phase = LIBRARY_SCAN_PHASE_VALIDATE_POST_SIGNATURE;
            if (!rom_fingerprint_equal(&scanner->first_fingerprint, &fingerprint) ||
                !signatures_equal(&scanner->first_signature,
                                  &scanner->second_signature)) {
                retry_or_skip_mutation(scanner);
                return false;
            } else {
                scanner->phase = LIBRARY_SCAN_PHASE_ADD_CANDIDATE;
            }
        }
        return true;
    }
    wanted = LIBRARY_SCANNER_READ_BYTES;
    if (scanner->pass_stat.size - scanner->pass_bytes < (uint64_t)wanted) {
        wanted = (size_t)(scanner->pass_stat.size - scanner->pass_bytes);
    }
    if (scanner->phase == LIBRARY_SCAN_PHASE_READ_HEADER) {
        size_t header_remaining = ROM_HEADER_METADATA_BYTES - scanner->header_length;
        if (wanted > header_remaining) wanted = header_remaining;
    }
    if (wanted > budget->max_read_bytes - *bytes_used) {
        wanted = budget->max_read_bytes - *bytes_used;
    }
    if (wanted == 0U) return false;
    received = scanner->fs->file_read(scanner->fs->context, scanner->file_handle,
                                      scanner->read_buffer, wanted);
    if (received <= 0 || (uint64_t)received > scanner->pass_stat.size - scanner->pass_bytes ||
        (size_t)received > wanted) {
        library_stat_t current;
        bool coherent_mutation = received == 0 ||
            !stat_candidate(scanner, &current) ||
            current.size != scanner->pass_stat.size ||
            current.modified_time != scanner->pass_stat.modified_time;
        if (coherent_mutation) retry_or_skip_mutation(scanner);
        else fail_candidate(scanner, false);
        return false;
    }
    *bytes_used += (uint32_t)received;
    scanner->pass_bytes += (uint64_t)received;
    if (scanner->phase == LIBRARY_SCAN_PHASE_READ_HEADER) {
        memcpy(scanner->header_bytes + scanner->header_length,
               scanner->read_buffer, (size_t)received);
        scanner->header_length += (size_t)received;
        if (scanner->header_length == ROM_HEADER_METADATA_BYTES) {
            if (!rom_header_parse(scanner->header_bytes,
                                  scanner->header_length, &scanner->header) ||
                !rom_identity_begin(&scanner->identity,
                                    scanner->header.byte_order) ||
                !rom_normalize_prefix(scanner->header.byte_order,
                                      scanner->header_bytes,
                                      scanner->header_length,
                                      scanner->normalize_buffer,
                                      scanner->header_length)) {
                fail_candidate(scanner, false);
                return false;
            }
            scanner->header_crc = crc32_data(scanner->header_crc,
                                             scanner->normalize_buffer,
                                             scanner->header_length);
            if (!update_hash_chunk(scanner, scanner->header_bytes,
                                   scanner->header_length)) {
                fail_candidate(scanner, false);
                return false;
            }
            scanner->phase = scanner->pass_number == 1U
                                 ? LIBRARY_SCAN_PHASE_HASH_FIRST
                                 : LIBRARY_SCAN_PHASE_HASH_SECOND;
        }
    } else if (!update_hash_chunk(scanner, scanner->read_buffer,
                                  (size_t)received)) {
        fail_candidate(scanner, false);
        return false;
    }
    return true;
}

library_scan_result_t library_scanner_poll(library_scanner_t *scanner,
                                           const library_scan_budget_t *budget)
{
    uint32_t entries_used = 0U;
    uint32_t bytes_used = 0U;
    uint32_t tick_start;
    size_t steps;

    if (scanner == NULL || budget == NULL) return LIBRARY_SCAN_FAILED_RESULT;
    if (scanner->state == LIBRARY_SCANNER_COMPLETE) return LIBRARY_SCAN_COMPLETED;
    if (scanner->state == LIBRARY_SCANNER_FAILED) return LIBRARY_SCAN_FAILED_RESULT;
    if (scanner->state == LIBRARY_SCANNER_QUIESCED) return LIBRARY_SCAN_QUIESCED_RESULT;
    if (scanner->state == LIBRARY_SCANNER_IDLE) return LIBRARY_SCAN_CANCELLED;
    if (scanner->state == LIBRARY_SCANNER_CANCEL_REQUESTED) {
        if (!close_all(scanner)) return LIBRARY_SCAN_YIELDED;
        reset_generation_work(scanner);
        scanner->state = LIBRARY_SCANNER_IDLE;
        return LIBRARY_SCAN_CANCELLED;
    }
    if (scanner->state == LIBRARY_SCANNER_PAUSE_REQUESTED) {
        if (!close_all(scanner)) return LIBRARY_SCAN_YIELDED;
        if (scanner->phase == LIBRARY_SCAN_PHASE_CLOSE_CANDIDATE &&
            !finish_candidate_close(scanner)) return LIBRARY_SCAN_YIELDED;
        scanner->resume_candidate = scanner->candidate_valid;
        scanner->pending_entry_valid = false;
        scanner->state = LIBRARY_SCANNER_QUIESCED;
        return LIBRARY_SCAN_QUIESCED_RESULT;
    }
    tick_start = scanner->fs->ticks_now != NULL
                     ? scanner->fs->ticks_now(scanner->fs->context)
                     : 0U;
    for (steps = 0U; steps < 128U; ++steps) {
        if (tick_expired(scanner, tick_start, budget->max_ticks)) break;
        if (scanner->phase == LIBRARY_SCAN_PHASE_NORMALIZE_ROOTS) {
            if (!add_root_paths(scanner)) {
                fail_generation(scanner, true);
                break;
            }
            scanner->phase = LIBRARY_SCAN_PHASE_OPEN_DIRECTORY;
        } else if (scanner->phase == LIBRARY_SCAN_PHASE_OPEN_DIRECTORY) {
            if (!open_next_directory(scanner, &entries_used, budget)) break;
        } else if (scanner->phase == LIBRARY_SCAN_PHASE_CONSUME_ENTRY) {
            if (scanner->replaying) {
                if (!retrieve_next_entry(scanner, &entries_used, budget)) break;
            } else if (scanner->pending_entry_valid) {
                consume_pending(scanner);
            } else if (scanner->directory_handle != NULL) {
                if (!retrieve_next_entry(scanner, &entries_used, budget)) break;
            } else {
                scanner->phase = LIBRARY_SCAN_PHASE_OPEN_DIRECTORY;
            }
        } else if (scanner->phase == LIBRARY_SCAN_PHASE_OPEN_CANDIDATE) {
            if (!begin_pass(scanner, 1U)) {
                fail_candidate(scanner, scanner->retry_count != 0U);
            }
        } else if (scanner->phase == LIBRARY_SCAN_PHASE_READ_HEADER ||
                   scanner->phase == LIBRARY_SCAN_PHASE_HASH_FIRST ||
                   scanner->phase == LIBRARY_SCAN_PHASE_HASH_SECOND) {
            if (!read_hash_data(scanner, &bytes_used, budget)) break;
        } else if (scanner->phase == LIBRARY_SCAN_PHASE_REOPEN_CANDIDATE) {
            scanner->phase = LIBRARY_SCAN_PHASE_VALIDATE_MID_SIGNATURE;
            if (!begin_pass(scanner, 2U)) retry_or_skip_mutation(scanner);
        } else if (scanner->phase == LIBRARY_SCAN_PHASE_CLOSE_CANDIDATE) {
            if (!finish_candidate_close(scanner)) break;
        } else if (scanner->phase == LIBRARY_SCAN_PHASE_ADD_CANDIDATE) {
            library_scanner_record_t *record;
            size_t index;
            bool new_fingerprint = true;
            for (index = 0U; index < scanner->record_count; ++index) {
                if (rom_fingerprint_equal(&scanner->records[index].fingerprint,
                                          &scanner->first_fingerprint)) {
                    new_fingerprint = false;
                    break;
                }
            }
            if (scanner->record_count >= LIBRARY_SCANNER_MAX_SOURCES ||
                (new_fingerprint &&
                 scanner->logical_record_count >= LIBRARY_SCANNER_MAX_RECORDS)) {
                fail_generation(scanner, true);
                break;
            }
            if (new_fingerprint) ++scanner->logical_record_count;
            record = &scanner->records[scanner->record_count++];
            memset(record, 0, sizeof(*record));
            record->logical_path = arena_path(scanner,
                                              scanner->candidate_path_offset);
            record->header = scanner->header;
            record->fingerprint = scanner->first_fingerprint;
            record->source_signature = scanner->second_signature;
            scanner->candidate_valid = false;
            scanner->phase = LIBRARY_SCAN_PHASE_CONSUME_ENTRY;
        } else if (scanner->phase == LIBRARY_SCAN_PHASE_FAIL) {
            if (!close_all(scanner)) break;
            scanner->state = LIBRARY_SCANNER_FAILED;
        } else {
            break;
        }
        if (scanner->state != LIBRARY_SCANNER_SCANNING) break;
        if (entries_used >= budget->max_directory_entries &&
            bytes_used >= budget->max_read_bytes) break;
    }
    if (scanner->state == LIBRARY_SCANNER_COMPLETE) return LIBRARY_SCAN_COMPLETED;
    if (scanner->state == LIBRARY_SCANNER_FAILED) return LIBRARY_SCAN_FAILED_RESULT;
    return (entries_used != 0U || bytes_used != 0U) ? LIBRARY_SCAN_PROGRESS
                                                    : LIBRARY_SCAN_YIELDED;
}

library_scanner_state_t library_scanner_state(const library_scanner_t *scanner)
{
    return scanner == NULL ? LIBRARY_SCANNER_FAILED : scanner->state;
}

library_scan_phase_t library_scanner_phase(const library_scanner_t *scanner)
{
    return scanner == NULL ? LIBRARY_SCAN_PHASE_FAIL : scanner->phase;
}

uint32_t library_scanner_generation(const library_scanner_t *scanner)
{
    return scanner == NULL ? 0U : scanner->generation;
}

size_t library_scanner_result_count(const library_scanner_t *scanner)
{
    return scanner == NULL ? 0U : scanner->record_count;
}

const library_scanner_record_t *library_scanner_result_at(
    const library_scanner_t *scanner, size_t index)
{
    if (scanner == NULL || index >= scanner->record_count) return NULL;
    return &scanner->records[index];
}

const library_scanner_stats_t *library_scanner_stats(
    const library_scanner_t *scanner)
{
    return scanner == NULL ? NULL : &scanner->stats;
}
