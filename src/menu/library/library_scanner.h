#ifndef LIBRARY_SCANNER_H__
#define LIBRARY_SCANNER_H__

#include "menu/library/library_fs.h"
#include "menu/library/library_roots.h"
#include "menu/library/rom_header.h"
#include "menu/library/rom_identity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIBRARY_SCANNER_MAX_DEPTH 8U
#define LIBRARY_SCANNER_MAX_RECORDS 256U
#define LIBRARY_SCANNER_MAX_PATHS 512U
#define LIBRARY_SCANNER_PATH_ARENA_BYTES 32768U
#define LIBRARY_SCANNER_PATH_BYTES 512U
#define LIBRARY_SCANNER_READ_BYTES 4096U
#define LIBRARY_SCANNER_MAX_LIVE_HEAP (224U * 1024U)

typedef enum {
    LIBRARY_SCANNER_IDLE,
    LIBRARY_SCANNER_SCANNING,
    LIBRARY_SCANNER_PAUSE_REQUESTED,
    LIBRARY_SCANNER_QUIESCED,
    LIBRARY_SCANNER_CANCEL_REQUESTED,
    LIBRARY_SCANNER_COMPLETE,
    LIBRARY_SCANNER_FAILED
} library_scanner_state_t;

typedef enum {
    LIBRARY_SCAN_PHASE_NORMALIZE_ROOTS,
    LIBRARY_SCAN_PHASE_OPEN_DIRECTORY,
    LIBRARY_SCAN_PHASE_CONSUME_ENTRY,
    LIBRARY_SCAN_PHASE_QUEUE_CHILD,
    LIBRARY_SCAN_PHASE_OPEN_CANDIDATE,
    LIBRARY_SCAN_PHASE_READ_HEADER,
    LIBRARY_SCAN_PHASE_HASH_FIRST,
    LIBRARY_SCAN_PHASE_REOPEN_CANDIDATE,
    LIBRARY_SCAN_PHASE_VALIDATE_MID_SIGNATURE,
    LIBRARY_SCAN_PHASE_HASH_SECOND,
    LIBRARY_SCAN_PHASE_VALIDATE_POST_SIGNATURE,
    LIBRARY_SCAN_PHASE_ADD_CANDIDATE,
    LIBRARY_SCAN_PHASE_CLOSE_CANDIDATE,
    LIBRARY_SCAN_PHASE_CLOSE_DIRECTORY,
    LIBRARY_SCAN_PHASE_COMPLETE,
    LIBRARY_SCAN_PHASE_FAIL
} library_scan_phase_t;

typedef struct {
    uint32_t max_directory_entries;
    uint32_t max_read_bytes;
    uint32_t max_ticks;
} library_scan_budget_t;

typedef enum {
    LIBRARY_SCAN_PROGRESS,
    LIBRARY_SCAN_YIELDED,
    LIBRARY_SCAN_COMPLETED,
    LIBRARY_SCAN_FAILED_RESULT,
    LIBRARY_SCAN_QUIESCED_RESULT,
    LIBRARY_SCAN_CANCELLED
} library_scan_result_t;

typedef struct {
    uint64_t size;
    int64_t modified_time;
    uint32_t normalized_header_crc32;
    uint32_t normalized_sample_crc32;
} library_source_signature_t;

typedef struct {
    const char *logical_path;
    rom_header_t header;
    rom_fingerprint_t fingerprint;
    library_source_signature_t source_signature;
} library_scanner_record_t;

typedef struct {
    uint32_t files_seen;
    uint32_t candidates_seen;
    uint32_t candidate_failures;
    uint32_t mutation_failures;
    uint32_t fatal_errors;
    uint32_t capacity_failures;
    bool clean;
} library_scanner_stats_t;

typedef struct {
    void *context;
    void *(*malloc_fn)(void *context, size_t size);
    void *(*calloc_fn)(void *context, size_t count, size_t size);
    void (*free_fn)(void *context, void *pointer);
} library_allocator_t;

typedef struct library_scanner library_scanner_t;

/* NULL allocator selects malloc/calloc/free. Creation is atomic on failure. */
bool library_scanner_create(library_scanner_t **out, library_fs_t *fs,
                            const library_roots_t *roots,
                            const library_allocator_t *allocator);
/* Returns false without freeing when a retryable owned handle cannot close. */
bool library_scanner_destroy(library_scanner_t *scanner);
bool library_scanner_start(library_scanner_t *scanner);
bool library_scanner_restart(library_scanner_t *scanner);
void library_scanner_request_pause(library_scanner_t *scanner);
bool library_scanner_resume(library_scanner_t *scanner);
void library_scanner_request_cancel(library_scanner_t *scanner);
library_scan_result_t library_scanner_poll(library_scanner_t *scanner,
                                           const library_scan_budget_t *budget);

library_scanner_state_t library_scanner_state(const library_scanner_t *scanner);
library_scan_phase_t library_scanner_phase(const library_scanner_t *scanner);
uint32_t library_scanner_generation(const library_scanner_t *scanner);
size_t library_scanner_result_count(const library_scanner_t *scanner);
const library_scanner_record_t *library_scanner_result_at(
    const library_scanner_t *scanner, size_t index);
const library_scanner_stats_t *library_scanner_stats(
    const library_scanner_t *scanner);

#endif
