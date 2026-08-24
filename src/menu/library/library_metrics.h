#ifndef LIBRARY_METRICS_H__
#define LIBRARY_METRICS_H__

#ifndef FEATURE_AURORA_LIBRARY_TIMING_ENABLED
#define FEATURE_AURORA_LIBRARY_TIMING_ENABLED 0
#endif

#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED

#include "menu/library/library_fs.h"
#include "menu/library/library_scanner.h"
#include "menu/library/library_snapshot.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIBRARY_METRICS_ALLOC_LEDGER_CAPACITY 128U
#define LIBRARY_METRICS_OVERLAY_ROWS 4U
#define LIBRARY_METRICS_OVERLAY_ROW_BYTES 56U
#define LIBRARY_METRICS_TERMINAL_RECORD_BYTES 896U
#define LIBRARY_METRICS_EVENT_COUNT 8U

#define LIBRARY_METRICS_TERMINAL_SCAN       UINT32_C(0x01)
#define LIBRARY_METRICS_TERMINAL_PAUSE      UINT32_C(0x02)
#define LIBRARY_METRICS_TERMINAL_CANCEL     UINT32_C(0x04)
#define LIBRARY_METRICS_TERMINAL_TRANSITION UINT32_C(0x08)

typedef enum {
    LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_INPUT_RECEIVED = 0,
    LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_TRANSITION_REQUESTED,
    LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_QUIESCED,
    LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER,
    LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_EXIT,
    LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_BEGIN,
    LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED,
    LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_NONEMPTY_FRAME_SUBMITTED
} library_metrics_event_t;

typedef enum {
    LIBRARY_METRICS_HEAP_POST_LIBRARY_INIT,
    LIBRARY_METRICS_HEAP_HOME,
    LIBRARY_METRICS_HEAP_ALL_GAMES
} library_metrics_heap_checkpoint_t;

typedef enum {
    LIBRARY_METRICS_OVERLAY_HOME,
    LIBRARY_METRICS_OVERLAY_ALL_GAMES
} library_metrics_overlay_kind_t;

typedef struct {
    char rows[LIBRARY_METRICS_OVERLAY_ROWS]
             [LIBRARY_METRICS_OVERLAY_ROW_BYTES];
} library_metrics_overlay_t;

typedef struct {
    void *pointer;
    size_t size;
} library_metrics_allocation_entry_t;

typedef struct {
    library_allocator_t base;
    library_metrics_allocation_entry_t
        ledger[LIBRARY_METRICS_ALLOC_LEDGER_CAPACITY];
} library_metrics_allocator_wrapper_t;

typedef struct {
    uint32_t transition_id;
    uint32_t generation;
    uint32_t ticks[LIBRARY_METRICS_EVENT_COUNT];
    uint8_t valid_mask;
    uint8_t applicable_mask;
    bool protocol_invalid;
} library_metrics_trace_t;

typedef struct {
    uint64_t owned_allocation_current_bytes;
    uint64_t owned_allocation_peak_bytes;
    uint64_t adapter_context_current_bytes;
    uint64_t adapter_context_peak_bytes;
    uint64_t task_current_bytes;
    uint64_t task_peak_bytes;
    uint64_t total_entries;
    uint64_t total_read_bytes;
    uint64_t scan_duration_ticks;
    uint64_t retained_path_bytes;
    uint32_t owned_allocation_current_count;
    uint32_t owned_allocation_peak_count;
    uint32_t free_heap_after_init;
    uint32_t free_heap_current;
    uint32_t free_heap_minimum;
    uint32_t home_free_heap_first;
    uint32_t home_free_heap_minimum;
    uint32_t all_games_free_heap_first;
    uint32_t all_games_free_heap_minimum;
    uint32_t directory_handles_current;
    uint32_t directory_handles_peak;
    uint32_t file_handles_current;
    uint32_t file_handles_peak;
    uint32_t poll_count;
    uint32_t last_poll_entries;
    uint32_t max_poll_entries;
    uint32_t last_poll_read_bytes;
    uint32_t max_poll_read_bytes;
    uint32_t max_poll_ticks;
    uint32_t max_action_gap_ticks;
    uint32_t max_usb_gap_ticks;
    uint32_t pause_ticks;
    uint32_t cancel_ticks;
    uint32_t attempted_generation;
    uint32_t published_generation;
    uint32_t record_count;
    uint32_t retained_path_count;
    uint32_t warning_count;
    uint32_t error_count;
    uint32_t home_samples;
    uint32_t all_games_samples;
    uint32_t terminal_event_mask;
    uint32_t usb_sequence;
    library_scanner_failure_t first_scanner_failure;
    library_snapshot_build_failure_t first_builder_failure;
    library_scanner_state_t terminal_scanner_state;
    library_metrics_trace_t trace;
    bool free_heap_valid;
    bool adapter_activity_valid;
    bool retained_path_detail_valid;
    bool retained_path_detail_pending;
    bool publication_facts_valid;
    bool publication_failed;
    bool scan_duration_valid;
    bool pause_duration_valid;
    bool cancel_duration_valid;
    bool allocation_accounting_available;
    bool allocation_invalid;
    bool counter_overflow;
    bool timing_invalid;
    bool overlay_truncated;
    bool terminal_truncated;
    bool critical_interval_active;
    bool terminal_pending;
    bool terminal_eligible;
} library_metrics_snapshot_t;

typedef int (*library_metrics_writer_t)(void *context, const char *bytes,
                                        size_t length);

void library_metrics_reset(void);
void library_metrics_allocator_wrap(library_metrics_allocator_wrapper_t *state,
                                    const library_allocator_t *base,
                                    library_allocator_t *wrapped);
void library_metrics_allocator_track(library_metrics_allocator_wrapper_t *state,
                                     void *pointer, size_t size);
void library_metrics_observe_adapter(
    const library_fs_libdragon_activity_t *activity, bool activity_valid);
void library_metrics_record_scanner_poll(
    uint32_t start, uint32_t end,
    const library_scanner_poll_stats_t *poll_stats);
void library_metrics_scan_attempt(uint32_t generation, uint32_t now);
void library_metrics_scan_observe(uint32_t now);
void library_metrics_scan_terminal(
    const library_scanner_stats_t *stats,
    library_scanner_state_t scanner_state, bool complete_generation,
    uint32_t now);
void library_metrics_publication_succeeded(
    uint32_t generation, const library_snapshot_t *published_snapshot);
void library_metrics_publication_failed(uint32_t generation);
bool library_metrics_snapshot_detail_pending(uint32_t *generation);
bool library_metrics_capture_snapshot_detail(
    const library_snapshot_t *snapshot);
void library_metrics_invalidate_snapshot_detail(uint32_t generation);
void library_metrics_set_scanner_failure(library_scanner_failure_t failure);
void library_metrics_set_builder_failure(
    library_snapshot_build_failure_t failure);
void library_metrics_pause_request(uint32_t now);
void library_metrics_pause_complete(uint32_t now, bool exactly_quiesced);
void library_metrics_cancel_request(uint32_t now);
void library_metrics_cancel_complete(uint32_t now, bool exactly_quiesced);
void library_metrics_lifecycle_reset(void);
void library_metrics_record_action_opportunity(uint32_t now);
void library_metrics_record_usb_opportunity(uint32_t now);
void library_metrics_heap_sample(library_metrics_heap_checkpoint_t checkpoint,
                                 uint32_t free_bytes);
void library_metrics_heap_sample_current(
    library_metrics_heap_checkpoint_t checkpoint);
uint32_t library_metrics_trace_begin(uint32_t generation);
bool library_metrics_trace_context(uint32_t *transition_id,
                                   uint32_t *generation);
bool library_metrics_trace_accept_transition(uint32_t transition_id,
                                             uint32_t generation,
                                             bool quiescence_required);
bool library_metrics_trace_record(library_metrics_event_t event,
                                  uint32_t transition_id,
                                  uint32_t generation);
bool library_metrics_critical_interval_active(void);
void library_metrics_snapshot(library_metrics_snapshot_t *out);
bool library_metrics_format_overlay(library_metrics_overlay_kind_t kind,
                                    library_metrics_overlay_t *out);
bool library_metrics_emit_pending(library_metrics_writer_t writer,
                                  void *context);
uint32_t library_metrics_ticks_now(void);
uint32_t library_metrics_tick_delta(uint32_t from, uint32_t to, bool *valid);
uint64_t library_metrics_saturating_add(uint64_t left, uint64_t right,
                                        bool *overflow);

#ifdef LIBRARY_METRICS_HOST_TEST
void library_metrics_host_set_ticks(uint32_t ticks);
void library_metrics_host_set_tick_step(uint32_t step);
#endif

#endif
#endif
