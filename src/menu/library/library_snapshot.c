#include "menu/library/library_snapshot.h"

#include <stdlib.h>
#include <string.h>

#define SNAPSHOT_MAGIC 0x41555237U
#define PAYLOAD_MAGIC 0x41555037U

typedef struct {
    library_scanner_record_t record;
    uint32_t path_offset;
} builder_source_t;

typedef struct {
    builder_source_t sources[LIBRARY_SNAPSHOT_MAX_SOURCES];
    char paths[LIBRARY_SNAPSHOT_PATH_POOL_BYTES];
} builder_staging_t;

typedef struct {
    rom_fingerprint_t fingerprint;
    char title[21];
    char game_code[5];
    char cartridge_id[3];
    uint8_t country_code;
    uint8_t revision;
    rom_region_t region;
    uint32_t source_first;
    uint16_t source_count;
} record_seed_t;

typedef struct snapshot_payload {
    library_allocator_t allocator;
    library_allocator_t storage_allocator;
    uint32_t magic;
    uint32_t refcount;
    size_t allocation_size;
    size_t record_count;
    size_t source_count;
    size_t paths_used;
    library_record_t *records;
    library_source_t *sources;
    char *paths;
    void *primary_storage;
    size_t primary_storage_size;
    void *secondary_storage;
    size_t secondary_storage_size;
} snapshot_payload_t;

struct library_snapshot_builder {
    library_allocator_t allocator;
    builder_staging_t *staging;
    library_scanner_t *scanner;
    library_scanner_record_t *detached_records;
    char *detached_paths;
    library_allocator_t detached_allocator;
    size_t detached_records_size;
    size_t detached_paths_size;
    size_t source_count;
    size_t paths_used;
    bool failed;
    bool frozen;
};

struct library_snapshot {
    library_allocator_t allocator;
    snapshot_payload_t *payload;
    uint32_t magic;
    uint32_t refcount;
    uint32_t generation;
    uint32_t warning_count;
    uint32_t error_count;
    library_snapshot_status_t status;
};

/* Private cross-translation-unit ownership handoff; not a public scanner API. */
bool library_scanner_snapshot_detach_internal(
    library_scanner_t *scanner, library_scanner_record_t **records_out,
    size_t *record_count_out, char **arena_out, size_t *arena_used_out,
    size_t *records_allocation_out, size_t *arena_allocation_out,
    library_allocator_t *allocator_out);

static void *default_malloc(void *context, size_t size)
{
    (void)context;
    return malloc(size);
}

static void *default_calloc(void *context, size_t count, size_t size)
{
    (void)context;
    return calloc(count, size);
}

static void default_free(void *context, void *pointer)
{
    (void)context;
    free(pointer);
}

static bool allocator_select(library_allocator_t *out,
                             const library_allocator_t *requested)
{
    if (out == NULL) return false;
    if (requested == NULL) {
        out->context = NULL;
        out->malloc_fn = default_malloc;
        out->calloc_fn = default_calloc;
        out->free_fn = default_free;
        return true;
    }
    if (requested->malloc_fn == NULL || requested->calloc_fn == NULL ||
        requested->free_fn == NULL) return false;
    *out = *requested;
    return true;
}

static size_t align_up(size_t value, size_t alignment)
{
    size_t remainder = value % alignment;
    return remainder == 0U ? value : value + alignment - remainder;
}

static unsigned char ascii_fold(unsigned char value)
{
    return value >= (unsigned char)'A' && value <= (unsigned char)'Z'
               ? (unsigned char)(value + 32U) : value;
}

static int folded_path_compare(const char *left, const char *right)
{
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    while (*a != 0U && *b != 0U) {
        unsigned char ca = ascii_fold(*a);
        unsigned char cb = ascii_fold(*b);
        if (ca != cb) return ca < cb ? -1 : 1;
        ++a;
        ++b;
    }
    if (*a != *b) return *a == 0U ? -1 : 1;
    return strcmp(left, right);
}

static bool same_header_identity(const rom_header_t *a, const rom_header_t *b)
{
    return memcmp(a->cartridge_id, b->cartridge_id,
                  sizeof(a->cartridge_id)) == 0 &&
           a->country_code == b->country_code && a->region == b->region &&
           a->revision == b->revision &&
           a->clock_rate == b->clock_rate &&
           a->boot_address == b->boot_address &&
           a->check_code == b->check_code &&
           memcmp(a->game_code, b->game_code, sizeof(a->game_code)) == 0 &&
           memcmp(a->title, b->title, sizeof(a->title)) == 0;
}

static bool compatible(const library_scanner_record_t *a,
                       const library_scanner_record_t *b)
{
    return a->source_signature.size == b->source_signature.size &&
           a->source_signature.normalized_header_crc32 ==
               b->source_signature.normalized_header_crc32 &&
           a->source_signature.normalized_sample_crc32 ==
               b->source_signature.normalized_sample_crc32 &&
           same_header_identity(&a->header, &b->header);
}

static int fingerprint_compare(const rom_fingerprint_t *a,
                               const rom_fingerprint_t *b)
{
    return memcmp(a->bytes, b->bytes, sizeof(a->bytes));
}

static int source_detail_compare(const library_scanner_record_t *a,
                                 const library_scanner_record_t *b)
{
    int path = folded_path_compare(a->logical_path, b->logical_path);
    if (path != 0) return path;
    if (a->source_signature.size != b->source_signature.size)
        return a->source_signature.size < b->source_signature.size ? -1 : 1;
    if (a->source_signature.modified_time != b->source_signature.modified_time)
        return a->source_signature.modified_time < b->source_signature.modified_time ? -1 : 1;
    if (a->source_signature.normalized_header_crc32 !=
        b->source_signature.normalized_header_crc32)
        return a->source_signature.normalized_header_crc32 <
               b->source_signature.normalized_header_crc32 ? -1 : 1;
    if (a->source_signature.normalized_sample_crc32 !=
        b->source_signature.normalized_sample_crc32)
        return a->source_signature.normalized_sample_crc32 <
               b->source_signature.normalized_sample_crc32 ? -1 : 1;
    if (a->header.byte_order != b->header.byte_order)
        return a->header.byte_order < b->header.byte_order ? -1 : 1;
    return 0;
}

static int standalone_source_compare(const void *left, const void *right)
{
    const builder_source_t *a = left;
    const builder_source_t *b = right;
    int fingerprint = fingerprint_compare(&a->record.fingerprint,
                                          &b->record.fingerprint);
    return fingerprint != 0 ? fingerprint
                            : source_detail_compare(&a->record, &b->record);
}

static int scanner_source_compare(const void *left, const void *right)
{
    const library_scanner_record_t *a = left;
    const library_scanner_record_t *b = right;
    int fingerprint = fingerprint_compare(&a->fingerprint, &b->fingerprint);
    return fingerprint != 0 ? fingerprint : source_detail_compare(a, b);
}

static library_scanner_record_t *builder_source_at(
    library_snapshot_builder_t *builder, size_t index)
{
    if (index >= builder->source_count) return NULL;
    if (builder->detached_records != NULL) return &builder->detached_records[index];
    return builder->staging == NULL ? NULL : &builder->staging->sources[index].record;
}

bool library_snapshot_builder_create(library_snapshot_builder_t **out,
                                     const library_allocator_t *allocator)
{
    library_allocator_t selected;
    library_snapshot_builder_t *builder;
    if (out != NULL) *out = NULL;
    if (out == NULL || !allocator_select(&selected, allocator)) return false;
    builder = selected.calloc_fn(selected.context, 1U, sizeof(*builder));
    if (builder == NULL) return false;
    builder->allocator = selected;
    *out = builder;
    return true;
}

bool library_snapshot_builder_add(library_snapshot_builder_t *builder,
                                  const library_scanner_record_t *source)
{
    size_t i;
    size_t length;
    builder_source_t *destination;
    if (builder == NULL || source == NULL || source->logical_path == NULL ||
        builder->failed || builder->frozen || builder->scanner != NULL ||
        builder->detached_records != NULL) return false;
    if (builder->staging == NULL) {
        builder->staging = builder->allocator.calloc_fn(
            builder->allocator.context, 1U, sizeof(*builder->staging));
        if (builder->staging == NULL) {
            builder->failed = true;
            return false;
        }
    }
    length = strlen(source->logical_path) + 1U;
    if (length > LIBRARY_SNAPSHOT_PATH_BYTES ||
        builder->source_count >= LIBRARY_SNAPSHOT_MAX_SOURCES ||
        length > LIBRARY_SNAPSHOT_PATH_POOL_BYTES - builder->paths_used) {
        builder->failed = true;
        return false;
    }
    for (i = 0U; i < builder->source_count; ++i) {
        const library_scanner_record_t *old =
            &builder->staging->sources[i].record;
        if (rom_fingerprint_equal(&old->fingerprint, &source->fingerprint) &&
            !compatible(old, source)) {
            builder->failed = true;
            return false;
        }
    }
    destination = &builder->staging->sources[builder->source_count++];
    destination->record = *source;
    destination->path_offset = (uint32_t)builder->paths_used;
    memcpy(builder->staging->paths + builder->paths_used,
           source->logical_path, length);
    destination->record.logical_path =
        builder->staging->paths + builder->paths_used;
    builder->paths_used += length;
    return true;
}

bool library_snapshot_builder_add_scanner(library_snapshot_builder_t *builder,
                                          library_scanner_t *scanner)
{
    const library_scanner_stats_t *stats;
    if (builder == NULL || builder->failed || builder->frozen ||
        builder->source_count != 0U || builder->staging != NULL ||
        builder->scanner != NULL || scanner == NULL ||
        library_scanner_state(scanner) != LIBRARY_SCANNER_COMPLETE) {
        if (builder != NULL) builder->failed = true;
        return false;
    }
    stats = library_scanner_stats(scanner);
    if (stats == NULL || !stats->clean ||
        library_scanner_result_count(scanner) > LIBRARY_SNAPSHOT_MAX_SOURCES) {
        builder->failed = true;
        return false;
    }
    builder->scanner = scanner;
    builder->source_count = library_scanner_result_count(scanner);
    return true;
}

static bool detach_scanner(library_snapshot_builder_t *builder)
{
    size_t count;
    if (builder->scanner == NULL) return true;
    if (!library_scanner_snapshot_detach_internal(
            builder->scanner, &builder->detached_records, &count,
            &builder->detached_paths, &builder->paths_used,
            &builder->detached_records_size, &builder->detached_paths_size,
            &builder->detached_allocator) || count != builder->source_count) {
        builder->failed = true;
        return false;
    }
    builder->scanner = NULL;
    return true;
}

static bool inspect_sorted(library_snapshot_builder_t *builder,
                           size_t *record_count_out, size_t *paths_used_out)
{
    size_t i;
    size_t unique = 0U;
    size_t paths = 0U;
    library_scanner_record_t *previous = NULL;
    for (i = 0U; i < builder->source_count; ++i) {
        library_scanner_record_t *source = builder_source_at(builder, i);
        size_t length;
        if (source == NULL || source->logical_path == NULL) return false;
        length = strlen(source->logical_path) + 1U;
        if (length > LIBRARY_SNAPSHOT_PATH_BYTES ||
            length > LIBRARY_SNAPSHOT_PATH_POOL_BYTES - paths) return false;
        paths += length;
        if (previous == NULL ||
            !rom_fingerprint_equal(&previous->fingerprint,
                                   &source->fingerprint)) {
            if (++unique > LIBRARY_SNAPSHOT_MAX_RECORDS) return false;
            previous = source;
        } else if (!compatible(previous, source)) {
            return false;
        }
    }
    *record_count_out = unique;
    *paths_used_out = paths;
    return true;
}

static bool order_paths(library_snapshot_builder_t *builder, char *paths,
                        size_t path_capacity, size_t paths_used)
{
    char temporary[LIBRARY_SNAPSHOT_PATH_BYTES];
    size_t desired = 0U;
    size_t i;
    if (paths_used > path_capacity) return false;
    for (i = 0U; i < builder->source_count; ++i) {
        library_scanner_record_t *current = builder_source_at(builder, i);
        uintptr_t base = (uintptr_t)(void *)paths;
        uintptr_t address = (uintptr_t)(const void *)current->logical_path;
        size_t current_offset;
        size_t length;
        size_t j;
        if (address < base || address >= base + path_capacity) return false;
        current_offset = (size_t)(address - base);
        length = strlen(current->logical_path) + 1U;
        if (length > sizeof(temporary) || current_offset < desired ||
            current_offset + length > path_capacity) return false;
        memcpy(temporary, current->logical_path, length);
        if (current_offset > desired) {
            memmove(paths + desired + length, paths + desired,
                    current_offset - desired);
            for (j = i + 1U; j < builder->source_count; ++j) {
                library_scanner_record_t *later = builder_source_at(builder, j);
                uintptr_t later_address =
                    (uintptr_t)(const void *)later->logical_path;
                if (later_address >= base + desired &&
                    later_address < base + current_offset) {
                    later->logical_path += length;
                }
            }
        }
        memcpy(paths + desired, temporary, length);
        current->logical_path = paths + desired;
        desired += length;
    }
    return desired == paths_used;
}

static void seed_from_record(record_seed_t *seed,
                             const library_scanner_record_t *input,
                             size_t source_first)
{
    memset(seed, 0, sizeof(*seed));
    seed->fingerprint = input->fingerprint;
    memcpy(seed->title, input->header.title, sizeof(seed->title));
    memcpy(seed->game_code, input->header.game_code, sizeof(seed->game_code));
    memcpy(seed->cartridge_id, input->header.cartridge_id,
           sizeof(seed->cartridge_id));
    seed->country_code = input->header.country_code;
    seed->revision = input->header.revision;
    seed->region = input->header.region;
    seed->source_first = (uint32_t)source_first;
}

static void record_from_seed(library_record_t *record,
                             const record_seed_t *seed)
{
    record->fingerprint = seed->fingerprint;
    record->lookup_key.fingerprint = seed->fingerprint;
    record->lookup_key.kind =
        ((seed->cartridge_id[0] >= 'A' && seed->cartridge_id[0] <= 'Z') ||
         (seed->cartridge_id[0] >= '0' && seed->cartridge_id[0] <= '9')) &&
        ((seed->cartridge_id[1] >= 'A' && seed->cartridge_id[1] <= 'Z') ||
         (seed->cartridge_id[1] >= '0' && seed->cartridge_id[1] <= '9')) &&
        seed->cartridge_id[2] == '\0' && seed->country_code != 0U
            ? LIBRARY_LOOKUP_HEADER : LIBRARY_LOOKUP_FINGERPRINT;
    memcpy(record->lookup_key.cartridge_id, seed->cartridge_id, 3U);
    record->lookup_key.country_code = seed->country_code;
    record->lookup_key.region = seed->region;
    record->lookup_key.revision = seed->revision;
    record->source_first = seed->source_first;
    record->source_count = seed->source_count;
    record->primary_source_index = (uint16_t)seed->source_first;
    memcpy(record->title, seed->title, sizeof(record->title));
    memcpy(record->game_code, seed->game_code, sizeof(record->game_code));
    record->country_code = seed->country_code;
    record->region = seed->region;
    record->revision = seed->revision;
}

static bool repack_storage(library_snapshot_builder_t *builder,
                           void *storage, size_t storage_size,
                           record_seed_t *seeds, size_t record_count,
                           library_record_t **records_out,
                           library_source_t **sources_out)
{
    size_t records_bytes = record_count * sizeof(library_record_t);
    size_t sources_offset = align_up(records_bytes, _Alignof(library_source_t));
    size_t sources_bytes = builder->source_count * sizeof(library_source_t);
    size_t i;
    size_t seed_at = 0U;
    rom_fingerprint_t previous;
    bool have_previous = false;
    if (sources_offset > storage_size ||
        sources_bytes > storage_size - sources_offset) return false;

    for (i = 0U; i < builder->source_count; ++i) {
        library_scanner_record_t input = *builder_source_at(builder, i);
        library_source_t output;
        memset(&output, 0, sizeof(output));
        if (!have_previous ||
            !rom_fingerprint_equal(&previous, &input.fingerprint)) {
            if (seed_at >= record_count) return false;
            previous = input.fingerprint;
            have_previous = true;
            seed_from_record(&seeds[seed_at++], &input, i);
        }
        ++seeds[seed_at - 1U].source_count;
        output.path_offset = (uint32_t)(input.logical_path -
            (builder->detached_paths != NULL ? builder->detached_paths
                                             : builder->staging->paths));
        output.size = input.source_signature.size;
        output.mtime_seconds = input.source_signature.modified_time;
        output.normalized_header_crc32 =
            input.source_signature.normalized_header_crc32;
        output.normalized_sample_crc32 =
            input.source_signature.normalized_sample_crc32;
        output.byte_order = input.header.byte_order;
        memcpy((unsigned char *)storage + i * sizeof(output),
               &output, sizeof(output));
    }
    if (seed_at != record_count) return false;
    memmove((unsigned char *)storage + sources_offset, storage, sources_bytes);
    memset(storage, 0, records_bytes);
    for (i = 0U; i < record_count; ++i) {
        record_from_seed(&((library_record_t *)storage)[i], &seeds[i]);
    }
    *records_out = storage;
    *sources_out = (library_source_t *)((unsigned char *)storage +
                                        sources_offset);
    return true;
}

static snapshot_payload_t *payload_create(library_snapshot_builder_t *builder)
{
    snapshot_payload_t *payload = builder->allocator.calloc_fn(
        builder->allocator.context, 1U, sizeof(*payload));
    if (payload == NULL) return NULL;
    payload->allocator = builder->allocator;
    payload->magic = PAYLOAD_MAGIC;
    payload->refcount = 1U;
    return payload;
}

static library_snapshot_t *snapshot_handle_create(snapshot_payload_t *payload,
                                                   uint32_t generation,
                                                   library_snapshot_status_t status)
{
    library_snapshot_t *snapshot = payload->allocator.calloc_fn(
        payload->allocator.context, 1U, sizeof(*snapshot));
    if (snapshot == NULL) return NULL;
    snapshot->allocator = payload->allocator;
    snapshot->payload = payload;
    snapshot->magic = SNAPSHOT_MAGIC;
    snapshot->refcount = 1U;
    snapshot->generation = generation;
    snapshot->status = status;
    return snapshot;
}

bool library_snapshot_builder_freeze(library_snapshot_builder_t *builder,
                                     uint32_t generation,
                                     library_snapshot_t **out)
{
    size_t record_count;
    size_t paths_used;
    record_seed_t *seeds = NULL;
    snapshot_payload_t *payload = NULL;
    library_snapshot_t *snapshot = NULL;
    void *storage;
    size_t storage_size;
    char *paths;
    size_t path_capacity;
    library_allocator_t storage_allocator;
    if (out != NULL) *out = NULL;
    if (builder == NULL || out == NULL || builder->failed || builder->frozen ||
        generation == 0U || !detach_scanner(builder)) {
        if (builder != NULL) builder->failed = true;
        return false;
    }

    if (builder->detached_records != NULL) {
        qsort(builder->detached_records, builder->source_count,
              sizeof(*builder->detached_records), scanner_source_compare);
        storage = builder->detached_records;
        storage_size = builder->detached_records_size;
        paths = builder->detached_paths;
        path_capacity = builder->detached_paths_size;
        storage_allocator = builder->detached_allocator;
    } else if (builder->staging != NULL) {
        qsort(builder->staging->sources, builder->source_count,
              sizeof(builder->staging->sources[0]), standalone_source_compare);
        storage = builder->staging;
        storage_size = sizeof(*builder->staging);
        paths = builder->staging->paths;
        path_capacity = sizeof(builder->staging->paths);
        storage_allocator = builder->allocator;
    } else {
        storage = NULL;
        storage_size = 0U;
        paths = NULL;
        path_capacity = 0U;
        storage_allocator = builder->allocator;
    }

    if (!inspect_sorted(builder, &record_count, &paths_used) ||
        (builder->source_count != 0U &&
         !order_paths(builder, paths, path_capacity, paths_used))) {
        builder->failed = true;
        return false;
    }
    if (record_count != 0U) {
        seeds = builder->allocator.calloc_fn(builder->allocator.context,
                                             record_count, sizeof(*seeds));
        if (seeds == NULL) {
            builder->failed = true;
            return false;
        }
    }
    payload = payload_create(builder);
    if (payload != NULL) {
        snapshot = snapshot_handle_create(payload, generation,
                                          LIBRARY_SNAPSHOT_FRESH);
    }
    if (payload == NULL || snapshot == NULL ||
        (builder->source_count != 0U &&
         !repack_storage(builder, storage, storage_size, seeds, record_count,
                         &payload->records, &payload->sources))) {
        builder->allocator.free_fn(builder->allocator.context, snapshot);
        builder->allocator.free_fn(builder->allocator.context, payload);
        builder->allocator.free_fn(builder->allocator.context, seeds);
        builder->failed = true;
        return false;
    }
    builder->allocator.free_fn(builder->allocator.context, seeds);

    payload->storage_allocator = storage_allocator;
    payload->record_count = record_count;
    payload->source_count = builder->source_count;
    payload->paths_used = paths_used;
    payload->paths = paths;
    payload->primary_storage = storage;
    payload->primary_storage_size = storage_size;
    if (builder->detached_paths != NULL) {
        payload->secondary_storage = builder->detached_paths;
        payload->secondary_storage_size = builder->detached_paths_size;
    }
    payload->allocation_size = sizeof(*payload) +
        payload->primary_storage_size + payload->secondary_storage_size;

    builder->staging = NULL;
    builder->detached_records = NULL;
    builder->detached_paths = NULL;
    builder->frozen = true;
    *out = snapshot;
    return true;
}

void library_snapshot_builder_destroy(library_snapshot_builder_t *builder)
{
    library_allocator_t allocator;
    if (builder == NULL) return;
    allocator = builder->allocator;
    allocator.free_fn(allocator.context, builder->staging);
    if (builder->detached_records != NULL) {
        builder->detached_allocator.free_fn(
            builder->detached_allocator.context, builder->detached_records);
        builder->detached_allocator.free_fn(
            builder->detached_allocator.context, builder->detached_paths);
    }
    allocator.free_fn(allocator.context, builder);
}

bool library_snapshot_acquire(library_snapshot_t *snapshot)
{
    if (snapshot == NULL || snapshot->magic != SNAPSHOT_MAGIC ||
        snapshot->refcount == UINT32_MAX) return false;
    ++snapshot->refcount;
    return true;
}

static void payload_release(snapshot_payload_t *payload)
{
    library_allocator_t allocator;
    if (payload == NULL || payload->magic != PAYLOAD_MAGIC ||
        payload->refcount == 0U) return;
    if (--payload->refcount != 0U) return;
    allocator = payload->allocator;
    payload->magic = 0U;
    payload->storage_allocator.free_fn(payload->storage_allocator.context,
                                       payload->secondary_storage);
    payload->storage_allocator.free_fn(payload->storage_allocator.context,
                                       payload->primary_storage);
    allocator.free_fn(allocator.context, payload);
}

void library_snapshot_release(library_snapshot_t *snapshot)
{
    library_allocator_t allocator;
    snapshot_payload_t *payload;
    if (snapshot == NULL || snapshot->magic != SNAPSHOT_MAGIC ||
        snapshot->refcount == 0U) return;
    if (--snapshot->refcount != 0U) return;
    allocator = snapshot->allocator;
    payload = snapshot->payload;
    snapshot->magic = 0U;
    allocator.free_fn(allocator.context, snapshot);
    payload_release(payload);
}

uint32_t library_snapshot_generation(const library_snapshot_t *snapshot)
{
    return snapshot == NULL ? 0U : snapshot->generation;
}

library_snapshot_status_t library_snapshot_status(
    const library_snapshot_t *snapshot)
{
    return snapshot == NULL ? LIBRARY_SNAPSHOT_EMPTY : snapshot->status;
}

size_t library_snapshot_record_count(const library_snapshot_t *snapshot)
{
    return snapshot == NULL ? 0U : snapshot->payload->record_count;
}

size_t library_snapshot_source_count(const library_snapshot_t *snapshot)
{
    return snapshot == NULL ? 0U : snapshot->payload->source_count;
}

const library_record_t *library_snapshot_record_at(
    const library_snapshot_t *snapshot, size_t index)
{
    return snapshot == NULL || index >= snapshot->payload->record_count
               ? NULL : &snapshot->payload->records[index];
}

const library_source_t *library_snapshot_source_at(
    const library_snapshot_t *snapshot, size_t index)
{
    return snapshot == NULL || index >= snapshot->payload->source_count
               ? NULL : &snapshot->payload->sources[index];
}

const char *library_snapshot_source_path(const library_snapshot_t *snapshot,
                                         size_t index)
{
    const library_source_t *source = library_snapshot_source_at(snapshot, index);
    return source == NULL || source->path_offset >= snapshot->payload->paths_used
               ? NULL : snapshot->payload->paths + source->path_offset;
}

const library_record_t *library_snapshot_find_fingerprint(
    const library_snapshot_t *snapshot, const rom_fingerprint_t *fingerprint)
{
    size_t low = 0U;
    size_t high;
    if (snapshot == NULL || fingerprint == NULL) return NULL;
    high = snapshot->payload->record_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        const library_record_t *record = &snapshot->payload->records[middle];
        int comparison = fingerprint_compare(&record->fingerprint, fingerprint);
        if (comparison < 0) low = middle + 1U;
        else if (comparison > 0) high = middle;
        else return record;
    }
    return NULL;
}

size_t library_snapshot_allocation_size(const library_snapshot_t *snapshot)
{
    return snapshot == NULL ? 0U
                            : sizeof(*snapshot) +
                                  snapshot->payload->allocation_size;
}

size_t library_snapshot_builder_allocation_size(
    const library_snapshot_builder_t *builder)
{
    size_t total;
    if (builder == NULL) return 0U;
    total = sizeof(*builder);
    if (builder->staging != NULL) total += sizeof(*builder->staging);
    if (builder->detached_records != NULL) {
        total += builder->detached_records_size + builder->detached_paths_size;
    }
    return total;
}

size_t library_snapshot_store_allocation_size(
    const library_snapshot_store_t *store)
{
    size_t total;
    if (store == NULL) return 0U;
    total = store->published == NULL
                ? 0U : sizeof(*store->published) +
                           store->published->payload->allocation_size;
    if (store->retired != NULL) {
        total += sizeof(*store->retired);
        if (store->published == NULL ||
            store->retired->payload != store->published->payload) {
            total += store->retired->payload->allocation_size;
        }
    }
    return total;
}

void library_snapshot_store_init(library_snapshot_store_t *store)
{
    if (store == NULL) return;
    memset(store, 0, sizeof(*store));
    store->status = LIBRARY_SNAPSHOT_EMPTY;
    store->next_generation = 1U;
}

static void cleanup_retired(library_snapshot_store_t *store)
{
    if (store->retired != NULL && store->retired->refcount == 1U) {
        library_snapshot_release(store->retired);
        store->retired = NULL;
    }
}

static bool can_retire(library_snapshot_store_t *store)
{
    cleanup_retired(store);
    return store->retired == NULL;
}

static bool replace_handle(library_snapshot_store_t *store,
                           library_snapshot_t *replacement)
{
    library_snapshot_t *old = store->published;
    if (old != NULL && old->refcount > 1U) {
        if (!can_retire(store)) return false;
        store->retired = old;
    } else {
        library_snapshot_release(old);
    }
    store->published = replacement;
    return true;
}

static bool transition_snapshot(library_snapshot_store_t *store,
                                library_snapshot_status_t expected,
                                library_snapshot_status_t next)
{
    library_snapshot_t *clone;
    if (store == NULL || store->status != expected) return false;
    if (store->published == NULL) {
        store->status = next;
        return true;
    }
    clone = snapshot_handle_create(store->published->payload,
                                   store->published->generation, next);
    if (clone == NULL) return false;
    ++store->published->payload->refcount;
    clone->warning_count = store->published->warning_count;
    clone->error_count = store->published->error_count;
    if (!replace_handle(store, clone)) {
        library_snapshot_release(clone);
        return false;
    }
    store->status = next;
    return true;
}

bool library_snapshot_store_mark_stale(library_snapshot_store_t *store)
{
    return transition_snapshot(
        store,
        store != NULL && store->status == LIBRARY_SNAPSHOT_EMPTY
            ? LIBRARY_SNAPSHOT_EMPTY : LIBRARY_SNAPSHOT_FRESH,
        LIBRARY_SNAPSHOT_STALE);
}

bool library_snapshot_store_begin_revalidation(library_snapshot_store_t *store)
{
    return transition_snapshot(store, LIBRARY_SNAPSHOT_STALE,
                               LIBRARY_SNAPSHOT_REVALIDATING);
}

bool library_snapshot_store_fail(library_snapshot_store_t *store)
{
    if (store == NULL ||
        store->status != LIBRARY_SNAPSHOT_REVALIDATING) return false;
    if (store->published == NULL) {
        store->status = LIBRARY_SNAPSHOT_EMPTY;
        return false;
    }
    return transition_snapshot(store, LIBRARY_SNAPSHOT_REVALIDATING,
                               LIBRARY_SNAPSHOT_FAILED_STALE);
}

bool library_snapshot_store_retry(library_snapshot_store_t *store)
{
    return transition_snapshot(store, LIBRARY_SNAPSHOT_FAILED_STALE,
                               LIBRARY_SNAPSHOT_STALE);
}

bool library_snapshot_store_refresh_blocked(library_snapshot_store_t *store)
{
    if (store == NULL) return false;
    cleanup_retired(store);
    return store->published != NULL && store->published->refcount > 1U &&
           store->retired != NULL;
}

bool library_snapshot_store_publish(library_snapshot_store_t *store,
                                    library_snapshot_builder_t *builder,
                                    uint32_t warning_count,
                                    uint32_t error_count)
{
    library_snapshot_t *candidate = NULL;
    if (store == NULL || builder == NULL ||
        store->status != LIBRARY_SNAPSHOT_REVALIDATING ||
        store->next_generation == 0U) return false;
    cleanup_retired(store);
    if (store->published != NULL && store->published->refcount > 1U &&
        store->retired != NULL) return false;
    if (error_count != 0U ||
        !library_snapshot_builder_freeze(builder, store->next_generation,
                                         &candidate)) {
        (void)library_snapshot_store_fail(store);
        return false;
    }
    candidate->warning_count = warning_count;
    candidate->error_count = 0U;
    if (!replace_handle(store, candidate)) {
        library_snapshot_release(candidate);
        (void)library_snapshot_store_fail(store);
        return false;
    }
    ++store->next_generation;
    store->status = LIBRARY_SNAPSHOT_FRESH;
    library_snapshot_builder_destroy(builder);
    return true;
}

library_snapshot_t *library_snapshot_store_acquire(
    const library_snapshot_store_t *store)
{
    library_snapshot_t *snapshot;
    if (store == NULL) return NULL;
    snapshot = store->published;
    return library_snapshot_acquire(snapshot) ? snapshot : NULL;
}

void library_snapshot_store_deinit(library_snapshot_store_t *store)
{
    if (store == NULL) return;
    library_snapshot_release(store->published);
    library_snapshot_release(store->retired);
    library_snapshot_store_init(store);
}

library_snapshot_status_t library_snapshot_store_status(
    const library_snapshot_store_t *store)
{
    return store == NULL ? LIBRARY_SNAPSHOT_EMPTY : store->status;
}

uint32_t library_snapshot_warning_count(const library_snapshot_t *snapshot)
{
    return snapshot == NULL ? 0U : snapshot->warning_count;
}

uint32_t library_snapshot_error_count(const library_snapshot_t *snapshot)
{
    return snapshot == NULL ? 0U : snapshot->error_count;
}
