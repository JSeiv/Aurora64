#ifndef LIBRARY_SNAPSHOT_H__
#define LIBRARY_SNAPSHOT_H__

#include "menu/library/library_scanner.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIBRARY_SNAPSHOT_MAX_RECORDS 256U
#define LIBRARY_SNAPSHOT_MAX_SOURCES 512U
#define LIBRARY_SNAPSHOT_PATH_POOL_BYTES 32768U
#define LIBRARY_SNAPSHOT_PATH_BYTES 512U
#define LIBRARY_SNAPSHOT_MAX_LIVE_HEAP (224U * 1024U)

typedef enum { LIBRARY_LOOKUP_HEADER, LIBRARY_LOOKUP_FINGERPRINT } library_lookup_kind_t;
typedef enum {
    LIBRARY_SNAPSHOT_EMPTY,
    LIBRARY_SNAPSHOT_STALE,
    LIBRARY_SNAPSHOT_REVALIDATING,
    LIBRARY_SNAPSHOT_FRESH,
    LIBRARY_SNAPSHOT_FAILED_STALE
} library_snapshot_status_t;

typedef struct {
    library_lookup_kind_t kind;
    char cartridge_id[3];
    uint8_t country_code;
    rom_region_t region;
    uint8_t revision;
    rom_fingerprint_t fingerprint;
} library_lookup_key_t;

typedef struct {
    uint32_t path_offset;
    uint64_t size;
    int64_t mtime_seconds;
    uint32_t normalized_header_crc32;
    uint32_t normalized_sample_crc32;
    rom_byte_order_t byte_order;
} library_source_t;

typedef struct {
    rom_fingerprint_t fingerprint;
    library_lookup_key_t lookup_key;
    uint32_t source_first;
    uint16_t source_count;
    uint16_t primary_source_index;
    char title[21];
    char game_code[5];
    uint8_t country_code;
    rom_region_t region;
    uint8_t revision;
} library_record_t;

typedef struct library_snapshot library_snapshot_t;
typedef struct library_snapshot_builder library_snapshot_builder_t;

typedef struct {
    library_snapshot_t *published;
    library_snapshot_t *retired;
    library_snapshot_status_t status;
    uint32_t next_generation;
} library_snapshot_store_t;

bool library_snapshot_builder_create(library_snapshot_builder_t **out,
                                     const library_allocator_t *allocator);
bool library_snapshot_builder_add(library_snapshot_builder_t *builder,
                                  const library_scanner_record_t *source);
/*
 * Borrows a complete, clean scanner until freeze or builder destruction.
 * A freeze attempt transfers the completed result storage to the builder; the
 * scanner remains caller-owned but may only be destroyed afterward.
 */
bool library_snapshot_builder_add_scanner(library_snapshot_builder_t *builder,
                                          library_scanner_t *scanner);
/* Freeze is transactional; the builder remains caller-owned and reusable only for destroy. */
bool library_snapshot_builder_freeze(library_snapshot_builder_t *builder,
                                     uint32_t generation,
                                     library_snapshot_t **out);
void library_snapshot_builder_destroy(library_snapshot_builder_t *builder);

/* Acquire fails closed at refcount saturation; every success requires release. */
bool library_snapshot_acquire(library_snapshot_t *snapshot);
void library_snapshot_release(library_snapshot_t *snapshot);
uint32_t library_snapshot_generation(const library_snapshot_t *snapshot);
library_snapshot_status_t library_snapshot_status(const library_snapshot_t *snapshot);
size_t library_snapshot_record_count(const library_snapshot_t *snapshot);
size_t library_snapshot_source_count(const library_snapshot_t *snapshot);
const library_record_t *library_snapshot_record_at(const library_snapshot_t *snapshot,
                                                   size_t index);
const library_source_t *library_snapshot_source_at(const library_snapshot_t *snapshot,
                                                   size_t index);
const char *library_snapshot_source_path(const library_snapshot_t *snapshot,
                                         size_t source_index);
const library_record_t *library_snapshot_find_fingerprint(
    const library_snapshot_t *snapshot, const rom_fingerprint_t *fingerprint);
/* Exact allocator-requested bytes (allocator metadata is intentionally excluded). */
size_t library_snapshot_allocation_size(const library_snapshot_t *snapshot);
size_t library_snapshot_builder_allocation_size(const library_snapshot_builder_t *builder);
size_t library_snapshot_store_allocation_size(const library_snapshot_store_t *store);

void library_snapshot_store_init(library_snapshot_store_t *store);
bool library_snapshot_store_mark_stale(library_snapshot_store_t *store);
bool library_snapshot_store_begin_revalidation(library_snapshot_store_t *store);
bool library_snapshot_store_fail(library_snapshot_store_t *store);
/* Preserve the failed publication while making a later refresh retryable. */
bool library_snapshot_store_retry(library_snapshot_store_t *store);
/* True only while an acquired current and retired handle block replacement. */
bool library_snapshot_store_refresh_blocked(
    library_snapshot_store_t *store);
/* Legal only while REVALIDATING. Transactional; consumes builder only on success. */
bool library_snapshot_store_publish(library_snapshot_store_t *store,
                                    library_snapshot_builder_t *builder,
                                    uint32_t warning_count,
                                    uint32_t error_count);
library_snapshot_t *library_snapshot_store_acquire(const library_snapshot_store_t *store);
void library_snapshot_store_deinit(library_snapshot_store_t *store);
library_snapshot_status_t library_snapshot_store_status(const library_snapshot_store_t *store);
uint32_t library_snapshot_warning_count(const library_snapshot_t *snapshot);
uint32_t library_snapshot_error_count(const library_snapshot_t *snapshot);

#endif
