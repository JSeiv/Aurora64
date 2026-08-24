#include "menu/library/library_metrics.h"

#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LIBRARY_METRICS_HOST_TEST
#include <libdragon.h>
#endif

typedef struct {
    library_metrics_snapshot_t value;
    uint64_t scan_elapsed_ticks;
    uint32_t scan_last_tick;
    uint32_t pause_start_tick;
    uint32_t cancel_start_tick;
    uint32_t last_action_tick;
    uint32_t last_usb_tick;
    uint32_t pending_after_usb_sequence;
    uint32_t pending_snapshot_generation;
    bool scan_active;
    bool scan_suspended;
    bool terminal_detail_incomplete;
    bool scan_timing_valid;
    bool pause_pending;
    bool cancel_pending;
    bool action_seen;
    bool usb_seen;
    bool heap_seen;
    bool home_heap_seen;
    bool all_games_heap_seen;
    bool terminal_formatted;
    size_t terminal_length;
    char terminal_record[LIBRARY_METRICS_TERMINAL_RECORD_BYTES];
} library_metrics_state_t;

static library_metrics_state_t metrics;

_Static_assert(LIBRARY_METRICS_EVENT_COUNT == 8U,
               "Layer 1 transition trace must have exactly eight events");
_Static_assert(sizeof(((library_metrics_overlay_t *)0)->rows[0]) ==
                   LIBRARY_METRICS_OVERLAY_ROW_BYTES,
               "Layer 1 overlay rows must retain their exact bound");
_Static_assert(sizeof(((library_metrics_state_t *)0)->terminal_record) ==
                   LIBRARY_METRICS_TERMINAL_RECORD_BYTES,
               "Layer 1 terminal record must retain its exact bound");

#ifdef LIBRARY_METRICS_HOST_TEST
static uint32_t host_ticks;
static uint32_t host_tick_step;
#endif

uint64_t library_metrics_saturating_add(uint64_t left, uint64_t right,
                                        bool *overflow)
{
    if (UINT64_MAX - left < right) {
        if (overflow != NULL) *overflow = true;
        return UINT64_MAX;
    }
    return left + right;
}

uint32_t library_metrics_tick_delta(uint32_t from, uint32_t to, bool *valid)
{
    uint32_t distance = to - from;
    bool is_valid = distance <= (uint32_t)INT32_MAX;
    if (valid != NULL) *valid = is_valid;
    return is_valid ? distance : 0U;
}

uint32_t library_metrics_ticks_now(void)
{
#ifdef LIBRARY_METRICS_HOST_TEST
    uint32_t now = host_ticks;
    host_ticks += host_tick_step;
    return now;
#else
    return (uint32_t)TICKS_READ();
#endif
}

#ifdef LIBRARY_METRICS_HOST_TEST
void library_metrics_host_set_ticks(uint32_t ticks)
{
    host_ticks = ticks;
}

void library_metrics_host_set_tick_step(uint32_t step)
{
    host_tick_step = step;
}
#endif

static void counter_add(uint64_t *value, uint64_t amount)
{
    bool overflow = false;
    *value = library_metrics_saturating_add(*value, amount, &overflow);
    if (overflow) metrics.value.counter_overflow = true;
}

static uint32_t measured_delta(uint32_t from, uint32_t to)
{
    bool valid;
    uint32_t result = library_metrics_tick_delta(from, to, &valid);
    if (!valid) metrics.value.timing_invalid = true;
    return result;
}

static void update_task_allocation(void)
{
    bool overflow = false;
    uint64_t current = library_metrics_saturating_add(
        metrics.value.owned_allocation_current_bytes,
        metrics.value.adapter_context_current_bytes, &overflow);
    if (overflow) metrics.value.counter_overflow = true;
    metrics.value.task_current_bytes = current;
    if (current > metrics.value.task_peak_bytes)
        metrics.value.task_peak_bytes = current;
}

static void queue_terminal(uint32_t event_mask)
{
    metrics.value.terminal_event_mask |= event_mask;
    metrics.value.terminal_pending = true;
    metrics.value.terminal_eligible = false;
    metrics.pending_after_usb_sequence = metrics.value.usb_sequence;
    metrics.terminal_formatted = false;
    metrics.terminal_length = 0U;
}

static void ledger_add(library_metrics_allocator_wrapper_t *state,
                       void *pointer, size_t size)
{
    size_t index;
    if (state == NULL || pointer == NULL) return;
    for (index = 0U; index < LIBRARY_METRICS_ALLOC_LEDGER_CAPACITY; ++index) {
        if (state->ledger[index].pointer == NULL) {
            bool overflow = false;
            state->ledger[index].pointer = pointer;
            state->ledger[index].size = size;
            metrics.value.owned_allocation_current_bytes =
                library_metrics_saturating_add(
                    metrics.value.owned_allocation_current_bytes,
                    (uint64_t)size, &overflow);
            if (overflow) metrics.value.counter_overflow = true;
            if (metrics.value.owned_allocation_current_count != UINT32_MAX) {
                ++metrics.value.owned_allocation_current_count;
            } else {
                metrics.value.counter_overflow = true;
            }
            if (metrics.value.owned_allocation_current_bytes >
                metrics.value.owned_allocation_peak_bytes) {
                metrics.value.owned_allocation_peak_bytes =
                    metrics.value.owned_allocation_current_bytes;
            }
            if (metrics.value.owned_allocation_current_count >
                metrics.value.owned_allocation_peak_count) {
                metrics.value.owned_allocation_peak_count =
                    metrics.value.owned_allocation_current_count;
            }
            update_task_allocation();
            return;
        }
    }
    metrics.value.allocation_invalid = true;
}

static void ledger_remove(library_metrics_allocator_wrapper_t *state,
                          void *pointer)
{
    size_t index;
    if (state == NULL || pointer == NULL) return;
    for (index = 0U; index < LIBRARY_METRICS_ALLOC_LEDGER_CAPACITY; ++index) {
        if (state->ledger[index].pointer == pointer) {
            size_t size = state->ledger[index].size;
            state->ledger[index].pointer = NULL;
            state->ledger[index].size = 0U;
            if ((uint64_t)size >
                metrics.value.owned_allocation_current_bytes ||
                metrics.value.owned_allocation_current_count == 0U) {
                metrics.value.owned_allocation_current_bytes = 0U;
                metrics.value.owned_allocation_current_count = 0U;
                metrics.value.allocation_invalid = true;
            } else {
                metrics.value.owned_allocation_current_bytes -= (uint64_t)size;
                --metrics.value.owned_allocation_current_count;
            }
            update_task_allocation();
            return;
        }
    }
    metrics.value.allocation_invalid = true;
}

static void *wrapped_malloc(void *context, size_t size)
{
    library_metrics_allocator_wrapper_t *state = context;
    void *result = state->base.malloc_fn(state->base.context, size);
    ledger_add(state, result, size);
    return result;
}

static void *wrapped_calloc(void *context, size_t count, size_t size)
{
    library_metrics_allocator_wrapper_t *state = context;
    void *result = state->base.calloc_fn(state->base.context, count, size);
    if (result != NULL) {
        if (size != 0U && count > SIZE_MAX / size) {
            metrics.value.allocation_invalid = true;
        } else {
            ledger_add(state, result, count * size);
        }
    }
    return result;
}

static void wrapped_free(void *context, void *pointer)
{
    library_metrics_allocator_wrapper_t *state = context;
    ledger_remove(state, pointer);
    state->base.free_fn(state->base.context, pointer);
}

void library_metrics_reset(void)
{
    memset(&metrics, 0, sizeof(metrics));
}

void library_metrics_allocator_wrap(library_metrics_allocator_wrapper_t *state,
                                    const library_allocator_t *base,
                                    library_allocator_t *wrapped)
{
    if (state == NULL || base == NULL || wrapped == NULL) return;
    memset(state, 0, sizeof(*state));
    state->base = *base;
    wrapped->context = state;
    wrapped->malloc_fn = wrapped_malloc;
    wrapped->calloc_fn = wrapped_calloc;
    wrapped->free_fn = wrapped_free;
    metrics.value.allocation_accounting_available = true;
}

void library_metrics_allocator_track(library_metrics_allocator_wrapper_t *state,
                                     void *pointer, size_t size)
{
    ledger_add(state, pointer, size);
}

void library_metrics_observe_adapter(
    const library_fs_libdragon_activity_t *activity, bool activity_valid)
{
    if (!activity_valid || activity == NULL) {
        metrics.value.adapter_context_current_bytes = 0U;
        metrics.value.directory_handles_current = 0U;
        metrics.value.file_handles_current = 0U;
        metrics.value.adapter_activity_valid = false;
        update_task_allocation();
        return;
    }
    metrics.value.adapter_activity_valid = true;
    metrics.value.adapter_context_current_bytes =
        (uint64_t)activity->context_requested_bytes;
    if (metrics.value.adapter_context_current_bytes >
        metrics.value.adapter_context_peak_bytes) {
        metrics.value.adapter_context_peak_bytes =
            metrics.value.adapter_context_current_bytes;
    }
    metrics.value.directory_handles_current =
        activity->directory_slots_current;
    metrics.value.file_handles_current = activity->file_slots_current;
    if (activity->directory_slots_peak >
        metrics.value.directory_handles_peak) {
        metrics.value.directory_handles_peak = activity->directory_slots_peak;
    }
    if (activity->file_slots_peak > metrics.value.file_handles_peak)
        metrics.value.file_handles_peak = activity->file_slots_peak;
    update_task_allocation();
}

void library_metrics_record_scanner_poll(
    uint32_t start, uint32_t end,
    const library_scanner_poll_stats_t *poll_stats)
{
    if (metrics.scan_active && metrics.scan_timing_valid &&
        metrics.scan_suspended && !metrics.pause_pending &&
        !metrics.cancel_pending) {
        metrics.scan_last_tick = start;
        metrics.scan_suspended = false;
    }
    uint32_t duration = measured_delta(start, end);
    if (metrics.value.poll_count != UINT32_MAX) {
        ++metrics.value.poll_count;
    } else {
        metrics.value.counter_overflow = true;
    }
    if (duration > metrics.value.max_poll_ticks)
        metrics.value.max_poll_ticks = duration;
    if (poll_stats == NULL) return;
    metrics.value.last_poll_entries = poll_stats->directory_entries;
    metrics.value.last_poll_read_bytes = poll_stats->read_bytes;
    if (poll_stats->directory_entries > metrics.value.max_poll_entries)
        metrics.value.max_poll_entries = poll_stats->directory_entries;
    if (poll_stats->read_bytes > metrics.value.max_poll_read_bytes)
        metrics.value.max_poll_read_bytes = poll_stats->read_bytes;
    counter_add(&metrics.value.total_entries,
                (uint64_t)poll_stats->directory_entries);
    counter_add(&metrics.value.total_read_bytes,
                (uint64_t)poll_stats->read_bytes);
}

void library_metrics_scan_attempt(uint32_t generation, uint32_t now)
{
    metrics.value.attempted_generation = generation;
    metrics.value.poll_count = 0U;
    metrics.value.last_poll_entries = 0U;
    metrics.value.max_poll_entries = 0U;
    metrics.value.last_poll_read_bytes = 0U;
    metrics.value.max_poll_read_bytes = 0U;
    metrics.value.max_poll_ticks = 0U;
    metrics.value.total_entries = 0U;
    metrics.value.total_read_bytes = 0U;
    metrics.value.scan_duration_ticks = 0U;
    metrics.value.scan_duration_valid = false;
    metrics.value.first_scanner_failure = LIBRARY_SCANNER_FAILURE_NONE;
    metrics.value.first_builder_failure =
        LIBRARY_SNAPSHOT_BUILD_FAILURE_NONE;
    metrics.value.record_count = 0U;
    metrics.value.retained_path_count = 0U;
    metrics.value.retained_path_bytes = 0U;
    metrics.value.retained_path_detail_valid = false;
    metrics.value.retained_path_detail_pending = false;
    metrics.value.publication_facts_valid = false;
    metrics.value.publication_failed = false;
    metrics.value.warning_count = 0U;
    metrics.value.error_count = 0U;
    metrics.value.pause_ticks = 0U;
    metrics.value.cancel_ticks = 0U;
    metrics.value.pause_duration_valid = false;
    metrics.value.cancel_duration_valid = false;
    metrics.scan_elapsed_ticks = 0U;
    metrics.scan_last_tick = now;
    metrics.pending_snapshot_generation = 0U;
    metrics.scan_active = true;
    metrics.scan_suspended = false;
    metrics.scan_timing_valid = true;
    metrics.terminal_detail_incomplete = false;
    metrics.pause_pending = false;
    metrics.cancel_pending = false;
}

void library_metrics_scan_observe(uint32_t now)
{
    bool valid;
    uint32_t delta;
    if (!metrics.scan_active || !metrics.scan_timing_valid) return;
    if (metrics.scan_suspended) {
        if (metrics.pause_pending || metrics.cancel_pending) return;
        metrics.scan_suspended = false;
    }
    delta = library_metrics_tick_delta(metrics.scan_last_tick, now, &valid);
    metrics.scan_last_tick = now;
    if (!valid) {
        metrics.scan_timing_valid = false;
        metrics.value.timing_invalid = true;
        return;
    }
    counter_add(&metrics.scan_elapsed_ticks, (uint64_t)delta);
}

void library_metrics_set_scanner_failure(library_scanner_failure_t failure)
{
    if (failure != LIBRARY_SCANNER_FAILURE_NONE &&
        metrics.value.first_scanner_failure ==
            LIBRARY_SCANNER_FAILURE_NONE) {
        metrics.value.first_scanner_failure = failure;
    }
}

void library_metrics_set_builder_failure(
    library_snapshot_build_failure_t failure)
{
    if (failure != LIBRARY_SNAPSHOT_BUILD_FAILURE_NONE &&
        metrics.value.first_builder_failure ==
            LIBRARY_SNAPSHOT_BUILD_FAILURE_NONE) {
        metrics.value.first_builder_failure = failure;
    }
}

bool library_metrics_snapshot_detail_pending(uint32_t *generation)
{
    if (generation != NULL)
        *generation = metrics.pending_snapshot_generation;
    return metrics.value.retained_path_detail_pending;
}

bool library_metrics_capture_snapshot_detail(
    const library_snapshot_t *snapshot)
{
    size_t source_count;
    size_t index;
    uint64_t retained_path_bytes = 0U;
    bool overflow = false;
    uint32_t generation;
    if (snapshot == NULL || metrics.value.critical_interval_active ||
        !metrics.value.retained_path_detail_pending) {
        return false;
    }
    generation = library_snapshot_generation(snapshot);
    if (generation != metrics.pending_snapshot_generation) return false;
    source_count = library_snapshot_source_count(snapshot);
    for (index = 0U; index < source_count; ++index) {
        const char *path = library_snapshot_source_path(snapshot, index);
        if (path != NULL) {
            retained_path_bytes = library_metrics_saturating_add(
                retained_path_bytes, (uint64_t)strlen(path) + 1U, &overflow);
        }
    }
    if (overflow) metrics.value.counter_overflow = true;
    metrics.value.published_generation = generation;
    metrics.value.record_count =
        (uint32_t)library_snapshot_record_count(snapshot);
    metrics.value.retained_path_count = (uint32_t)source_count;
    metrics.value.retained_path_bytes = retained_path_bytes;
    metrics.value.warning_count = library_snapshot_warning_count(snapshot);
    metrics.value.error_count = library_snapshot_error_count(snapshot);
    metrics.value.retained_path_detail_valid = true;
    metrics.value.retained_path_detail_pending = false;
    metrics.value.publication_facts_valid = true;
    metrics.pending_snapshot_generation = 0U;
    metrics.terminal_detail_incomplete = false;
    return true;
}

void library_metrics_invalidate_snapshot_detail(uint32_t generation)
{
    if (!metrics.value.retained_path_detail_pending ||
        metrics.pending_snapshot_generation != generation) {
        return;
    }
    metrics.value.retained_path_detail_pending = false;
    metrics.value.retained_path_detail_valid = false;
    metrics.pending_snapshot_generation = 0U;
    metrics.terminal_detail_incomplete = false;
}

void library_metrics_publication_succeeded(
    uint32_t generation, const library_snapshot_t *published_snapshot)
{
    bool matching =
        published_snapshot != NULL &&
        library_snapshot_generation(published_snapshot) == generation;
    metrics.value.published_generation = generation;
    metrics.value.record_count =
        matching ? (uint32_t)library_snapshot_record_count(published_snapshot)
                 : 0U;
    metrics.value.retained_path_count =
        matching ? (uint32_t)library_snapshot_source_count(published_snapshot)
                 : 0U;
    metrics.value.retained_path_bytes = 0U;
    if (matching) {
        metrics.value.warning_count =
            library_snapshot_warning_count(published_snapshot);
        metrics.value.error_count =
            library_snapshot_error_count(published_snapshot);
    }
    metrics.value.retained_path_detail_valid = false;
    metrics.value.retained_path_detail_pending = true;
    metrics.value.publication_facts_valid = matching;
    metrics.value.publication_failed = false;
    metrics.pending_snapshot_generation = generation;
    metrics.terminal_detail_incomplete = true;
}

void library_metrics_publication_failed(uint32_t generation)
{
    metrics.value.publication_failed = true;
    if (metrics.value.retained_path_detail_pending &&
        metrics.pending_snapshot_generation == generation) {
        metrics.value.retained_path_detail_pending = false;
        metrics.value.retained_path_detail_valid = false;
        metrics.pending_snapshot_generation = 0U;
    }
    metrics.terminal_detail_incomplete = false;
}

void library_metrics_scan_terminal(
    const library_scanner_stats_t *stats,
    library_scanner_state_t scanner_state, bool complete_generation,
    uint32_t now)
{
    if (metrics.scan_active) library_metrics_scan_observe(now);
    if (complete_generation && metrics.scan_active &&
        metrics.scan_timing_valid) {
        metrics.value.scan_duration_ticks = metrics.scan_elapsed_ticks;
        metrics.value.scan_duration_valid = true;
    }
    metrics.scan_active = false;
    metrics.scan_suspended = false;
    metrics.value.terminal_scanner_state = scanner_state;
    if (stats != NULL) {
        uint64_t warnings = (uint64_t)stats->candidate_failures +
                            (uint64_t)stats->mutation_failures;
        if (warnings > UINT32_MAX) {
            metrics.value.warning_count = UINT32_MAX;
            metrics.value.counter_overflow = true;
        } else {
            metrics.value.warning_count = (uint32_t)warnings;
        }
        metrics.value.error_count = stats->fatal_errors;
    }
    if (complete_generation &&
        scanner_state == LIBRARY_SCANNER_COMPLETE) {
        metrics.terminal_detail_incomplete = true;
    } else {
        metrics.value.retained_path_detail_pending = false;
        metrics.value.retained_path_detail_valid = false;
        metrics.pending_snapshot_generation = 0U;
        metrics.terminal_detail_incomplete = false;
    }
    queue_terminal(LIBRARY_METRICS_TERMINAL_SCAN);
}

void library_metrics_pause_request(uint32_t now)
{
    if (metrics.pause_pending) return;
    if (metrics.scan_active && metrics.scan_timing_valid &&
        !metrics.scan_suspended) {
        library_metrics_scan_observe(now);
        if (metrics.scan_timing_valid) metrics.scan_suspended = true;
    }
    metrics.pause_pending = true;
    metrics.pause_start_tick = now;
    metrics.value.pause_ticks = 0U;
    metrics.value.pause_duration_valid = false;
}

void library_metrics_pause_complete(uint32_t now, bool exactly_quiesced)
{
    bool valid;
    uint32_t delta;
    if (!metrics.pause_pending || !exactly_quiesced) return;
    delta = library_metrics_tick_delta(metrics.pause_start_tick, now, &valid);
    metrics.pause_pending = false;
    if (metrics.scan_active && metrics.scan_timing_valid &&
        metrics.scan_suspended) {
        metrics.scan_last_tick = now;
    }
    metrics.value.pause_ticks = delta;
    metrics.value.pause_duration_valid = valid;
    if (!valid) metrics.value.timing_invalid = true;
    queue_terminal(LIBRARY_METRICS_TERMINAL_PAUSE);
}

void library_metrics_cancel_request(uint32_t now)
{
    if (metrics.cancel_pending) return;
    if (metrics.pause_pending) {
        metrics.pause_pending = false;
        metrics.value.pause_duration_valid = false;
    }
    metrics.cancel_pending = true;
    metrics.cancel_start_tick = now;
    metrics.value.cancel_ticks = 0U;
    metrics.value.cancel_duration_valid = false;
}

void library_metrics_cancel_complete(uint32_t now, bool exactly_quiesced)
{
    bool valid;
    uint32_t delta;
    if (!metrics.cancel_pending || !exactly_quiesced) return;
    delta = library_metrics_tick_delta(metrics.cancel_start_tick, now, &valid);
    metrics.cancel_pending = false;
    metrics.value.cancel_ticks = delta;
    metrics.value.cancel_duration_valid = valid;
    if (!valid) metrics.value.timing_invalid = true;
    queue_terminal(LIBRARY_METRICS_TERMINAL_CANCEL);
}

void library_metrics_lifecycle_reset(void)
{
    metrics.pause_pending = false;
    metrics.cancel_pending = false;
    metrics.value.pause_ticks = 0U;
    metrics.value.cancel_ticks = 0U;
    metrics.value.pause_duration_valid = false;
    metrics.value.cancel_duration_valid = false;
}

void library_metrics_record_action_opportunity(uint32_t now)
{
    if (metrics.action_seen) {
        uint32_t gap = measured_delta(metrics.last_action_tick, now);
        if (gap > metrics.value.max_action_gap_ticks)
            metrics.value.max_action_gap_ticks = gap;
    }
    metrics.last_action_tick = now;
    metrics.action_seen = true;
}

void library_metrics_record_usb_opportunity(uint32_t now)
{
    if (metrics.usb_seen) {
        uint32_t gap = measured_delta(metrics.last_usb_tick, now);
        if (gap > metrics.value.max_usb_gap_ticks)
            metrics.value.max_usb_gap_ticks = gap;
    }
    metrics.last_usb_tick = now;
    metrics.usb_seen = true;
    if (metrics.value.usb_sequence != UINT32_MAX) {
        ++metrics.value.usb_sequence;
    } else {
        metrics.value.counter_overflow = true;
    }
    if (metrics.value.terminal_pending &&
        !metrics.value.critical_interval_active &&
        metrics.value.usb_sequence > metrics.pending_after_usb_sequence) {
        metrics.value.terminal_eligible = true;
    }
}

static void heap_minimum(uint32_t value, uint32_t *destination, bool *seen)
{
    if (!*seen || value < *destination) *destination = value;
    *seen = true;
}

void library_metrics_heap_sample(library_metrics_heap_checkpoint_t checkpoint,
                                 uint32_t free_bytes)
{
    metrics.value.free_heap_current = free_bytes;
    heap_minimum(free_bytes, &metrics.value.free_heap_minimum,
                 &metrics.heap_seen);
    metrics.value.free_heap_valid = true;
    if (checkpoint == LIBRARY_METRICS_HEAP_POST_LIBRARY_INIT) {
        metrics.value.free_heap_after_init = free_bytes;
    } else if (checkpoint == LIBRARY_METRICS_HEAP_HOME) {
        if (!metrics.home_heap_seen)
            metrics.value.home_free_heap_first = free_bytes;
        heap_minimum(free_bytes, &metrics.value.home_free_heap_minimum,
                     &metrics.home_heap_seen);
        if (metrics.value.home_samples != UINT32_MAX)
            ++metrics.value.home_samples;
        else
            metrics.value.counter_overflow = true;
    } else if (checkpoint == LIBRARY_METRICS_HEAP_ALL_GAMES) {
        if (!metrics.all_games_heap_seen)
            metrics.value.all_games_free_heap_first = free_bytes;
        heap_minimum(free_bytes,
                     &metrics.value.all_games_free_heap_minimum,
                     &metrics.all_games_heap_seen);
        if (metrics.value.all_games_samples != UINT32_MAX)
            ++metrics.value.all_games_samples;
        else
            metrics.value.counter_overflow = true;
    }
}

void library_metrics_heap_sample_current(
    library_metrics_heap_checkpoint_t checkpoint)
{
#ifdef LIBRARY_METRICS_HOST_TEST
    (void)checkpoint;
#else
    heap_stats_t stats;
    uint32_t free_bytes;
    sys_get_heap_stats(&stats);
    free_bytes = stats.total > stats.used
                     ? (uint32_t)(stats.total - stats.used)
                     : 0U;
    library_metrics_heap_sample(checkpoint, free_bytes);
#endif
}

static uint8_t event_bit(library_metrics_event_t event)
{
    return (uint8_t)(UINT8_C(1) << (unsigned int)event);
}

uint32_t library_metrics_trace_begin(uint32_t generation)
{
    uint32_t next_id = metrics.value.trace.transition_id;
    library_metrics_trace_t trace;
    metrics.value.critical_interval_active = true;
    if (next_id != UINT32_MAX) {
        ++next_id;
    } else {
        metrics.value.counter_overflow = true;
    }
    memset(&trace, 0, sizeof(trace));
    trace.transition_id = next_id;
    trace.generation = generation;
    trace.applicable_mask = (uint8_t)(
        event_bit(LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_INPUT_RECEIVED) |
        event_bit(LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_TRANSITION_REQUESTED) |
        event_bit(LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER) |
        event_bit(LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_EXIT) |
        event_bit(LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_BEGIN) |
        event_bit(LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED) |
        event_bit(
            LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_NONEMPTY_FRAME_SUBMITTED));
    trace.ticks[LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_INPUT_RECEIVED] =
        library_metrics_ticks_now();
    trace.valid_mask =
        event_bit(LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_INPUT_RECEIVED);
    metrics.value.trace = trace;
    metrics.value.terminal_event_mask &=
        ~LIBRARY_METRICS_TERMINAL_TRANSITION;
    return next_id;
}

bool library_metrics_trace_context(uint32_t *transition_id,
                                   uint32_t *generation)
{
    if (transition_id != NULL)
        *transition_id = metrics.value.trace.transition_id;
    if (generation != NULL) *generation = metrics.value.trace.generation;
    return metrics.value.trace.valid_mask != 0U;
}

static bool trace_context_matches(uint32_t transition_id, uint32_t generation)
{
    if (metrics.value.trace.transition_id == transition_id &&
        metrics.value.trace.generation == generation &&
        transition_id != 0U) {
        return true;
    }
    metrics.value.trace.protocol_invalid = true;
    return false;
}

static bool trace_prerequisite_valid(library_metrics_event_t event)
{
    uint8_t valid = metrics.value.trace.valid_mask;
    uint8_t applicable = metrics.value.trace.applicable_mask;
    switch (event) {
        case LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_TRANSITION_REQUESTED:
            return (valid & event_bit(
                LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_INPUT_RECEIVED)) != 0U;
        case LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_QUIESCED:
            return (applicable & event_bit(event)) != 0U &&
                   (valid & event_bit(
                       LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_TRANSITION_REQUESTED)) != 0U;
        case LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER:
            if ((valid & event_bit(
                    LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_TRANSITION_REQUESTED)) == 0U)
                return false;
            return (applicable & event_bit(
                        LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_QUIESCED)) == 0U ||
                   (valid & event_bit(
                        LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_QUIESCED)) != 0U;
        case LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_EXIT:
            return (valid & event_bit(
                LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER)) != 0U;
        case LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_BEGIN:
            return (valid & event_bit(
                LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_EXIT)) != 0U;
        case LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED:
            return (valid & event_bit(
                LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_BEGIN)) != 0U;
        case LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_NONEMPTY_FRAME_SUBMITTED:
            return (valid & event_bit(
                LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED)) != 0U;
        case LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_INPUT_RECEIVED:
        default:
            return false;
    }
}

bool library_metrics_trace_record(library_metrics_event_t event,
                                  uint32_t transition_id,
                                  uint32_t generation)
{
    uint8_t bit;
    if ((unsigned int)event >= LIBRARY_METRICS_EVENT_COUNT ||
        event == LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_INPUT_RECEIVED ||
        !trace_context_matches(transition_id, generation)) {
        metrics.value.trace.protocol_invalid = true;
        return false;
    }
    bit = event_bit(event);
    if ((metrics.value.trace.valid_mask & bit) != 0U ||
        (metrics.value.trace.applicable_mask & bit) == 0U ||
        !trace_prerequisite_valid(event)) {
        metrics.value.trace.protocol_invalid = true;
        return false;
    }
    metrics.value.trace.ticks[event] = library_metrics_ticks_now();
    metrics.value.trace.valid_mask |= bit;
    if (event == LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED) {
        metrics.value.critical_interval_active = false;
        queue_terminal(LIBRARY_METRICS_TERMINAL_TRANSITION);
    }
    return true;
}

bool library_metrics_trace_accept_transition(uint32_t transition_id,
                                             uint32_t generation,
                                             bool quiescence_required)
{
    if (!trace_context_matches(transition_id, generation)) return false;
    if (quiescence_required) {
        metrics.value.trace.applicable_mask |= event_bit(
            LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_QUIESCED);
    } else {
        metrics.value.trace.applicable_mask &= (uint8_t)~event_bit(
            LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_QUIESCED);
    }
    return library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_TRANSITION_REQUESTED,
        transition_id, generation);
}

bool library_metrics_critical_interval_active(void)
{
    return metrics.value.critical_interval_active;
}

void library_metrics_snapshot(library_metrics_snapshot_t *out)
{
    if (out == NULL) return;
    *out = metrics.value;
}

static void format_row(char *row, const char *format, ...)
{
    int result;
    va_list arguments;
    va_start(arguments, format);
    result = vsnprintf(row, LIBRARY_METRICS_OVERLAY_ROW_BYTES,
                       format, arguments);
    va_end(arguments);
    row[LIBRARY_METRICS_OVERLAY_ROW_BYTES - 1U] = '\0';
    if (result < 0 || (size_t)result >= LIBRARY_METRICS_OVERLAY_ROW_BYTES)
        metrics.value.overlay_truncated = true;
}

static void format_delta(char *out, size_t capacity,
                         library_metrics_event_t event)
{
    uint8_t input_bit = event_bit(
        LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_INPUT_RECEIVED);
    uint8_t target_bit = event_bit(event);
    bool valid;
    uint32_t delta;
    if ((metrics.value.trace.valid_mask & input_bit) == 0U ||
        (metrics.value.trace.valid_mask & target_bit) == 0U) {
        (void)snprintf(out, capacity, "n/a");
        return;
    }
    delta = library_metrics_tick_delta(
        metrics.value.trace.ticks[
            LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_INPUT_RECEIVED],
        metrics.value.trace.ticks[event], &valid);
    if (!valid) {
        (void)snprintf(out, capacity, "n/a");
    } else {
        (void)snprintf(out, capacity, "%lu", (unsigned long)delta);
    }
}

bool library_metrics_format_overlay(library_metrics_overlay_kind_t kind,
                                    library_metrics_overlay_t *out)
{
    char submitted[16];
    char nonempty[16];
    if (out == NULL || metrics.value.critical_interval_active) return false;
    memset(out, 0, sizeof(*out));
    if (kind == LIBRARY_METRICS_OVERLAY_HOME) {
        format_row(out->rows[0], "L1 HOME heap %lu min %lu init %lu",
                   (unsigned long)metrics.value.free_heap_current,
                   (unsigned long)metrics.value.free_heap_minimum,
                   (unsigned long)metrics.value.free_heap_after_init);
        if (metrics.value.allocation_accounting_available) {
            format_row(out->rows[1],
                       "owned %llu/%llu bytes %lu/%lu allocs",
                       (unsigned long long)
                           metrics.value.owned_allocation_current_bytes,
                       (unsigned long long)
                           metrics.value.owned_allocation_peak_bytes,
                       (unsigned long)
                           metrics.value.owned_allocation_current_count,
                       (unsigned long)
                           metrics.value.owned_allocation_peak_count);
        } else {
            format_row(out->rows[1], "owned=n/a allocs=n/a");
        }
        format_row(out->rows[2], "gen %lu pub %lu records %lu paths %lu",
                   (unsigned long)metrics.value.attempted_generation,
                   (unsigned long)metrics.value.published_generation,
                   (unsigned long)metrics.value.record_count,
                   (unsigned long)metrics.value.retained_path_count);
        format_row(out->rows[3], "polls %lu max %lu ticks USBgap %lu",
                   (unsigned long)metrics.value.poll_count,
                   (unsigned long)metrics.value.max_poll_ticks,
                   (unsigned long)metrics.value.max_usb_gap_ticks);
    } else {
        format_delta(submitted, sizeof(submitted),
                     LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED);
        format_delta(nonempty, sizeof(nonempty),
                     LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_NONEMPTY_FRAME_SUBMITTED);
        format_row(out->rows[0], "L1 ALL id %lu gen %lu v%02x a%02x%s",
                   (unsigned long)metrics.value.trace.transition_id,
                   (unsigned long)metrics.value.trace.generation,
                   (unsigned int)metrics.value.trace.valid_mask,
                   (unsigned int)metrics.value.trace.applicable_mask,
                   metrics.value.trace.protocol_invalid ? " !" : "");
        format_row(out->rows[1], "input->submitted %s nonempty %s",
                   submitted, nonempty);
        format_row(out->rows[2], "poll e %lu/%lu b %lu/%lu total %llu",
                   (unsigned long)metrics.value.last_poll_entries,
                   (unsigned long)metrics.value.max_poll_entries,
                   (unsigned long)metrics.value.last_poll_read_bytes,
                   (unsigned long)metrics.value.max_poll_read_bytes,
                   (unsigned long long)metrics.value.total_read_bytes);
        format_row(out->rows[3], "handles d%lu f%lu warn %lu err %lu",
                   (unsigned long)metrics.value.directory_handles_current,
                   (unsigned long)metrics.value.file_handles_current,
                   (unsigned long)metrics.value.warning_count,
                   (unsigned long)metrics.value.error_count);
    }
    return true;
}

static size_t format_terminal_record(void)
{
    char owned[42];
    char allocs[22];
    char records[11];
    char path_count[11];
    char path_bytes[21];
    char warnings[11];
    char errors[11];
    const char *owned_text = "n/a";
    const char *allocs_text = "n/a";
    const char *records_text = "n/a";
    const char *path_count_text = "n/a";
    const char *path_bytes_text = "n/a";
    const char *warnings_text = "n/a";
    const char *errors_text = "n/a";
    int result;
    if (metrics.value.allocation_accounting_available) {
        (void)snprintf(
            owned, sizeof(owned), "%llu/%llu",
            (unsigned long long)
                metrics.value.owned_allocation_current_bytes,
            (unsigned long long)
                metrics.value.owned_allocation_peak_bytes);
        (void)snprintf(
            allocs, sizeof(allocs), "%lu/%lu",
            (unsigned long)metrics.value.owned_allocation_current_count,
            (unsigned long)metrics.value.owned_allocation_peak_count);
        owned_text = owned;
        allocs_text = allocs;
    }
    if (metrics.value.publication_facts_valid) {
        (void)snprintf(records, sizeof(records), "%lu",
                       (unsigned long)metrics.value.record_count);
        (void)snprintf(path_count, sizeof(path_count), "%lu",
                       (unsigned long)metrics.value.retained_path_count);
        (void)snprintf(warnings, sizeof(warnings), "%lu",
                       (unsigned long)metrics.value.warning_count);
        (void)snprintf(errors, sizeof(errors), "%lu",
                       (unsigned long)metrics.value.error_count);
        records_text = records;
        path_count_text = path_count;
        warnings_text = warnings;
        errors_text = errors;
    }
    if (metrics.value.retained_path_detail_valid) {
        (void)snprintf(path_bytes, sizeof(path_bytes), "%llu",
                       (unsigned long long)
                           metrics.value.retained_path_bytes);
        path_bytes_text = path_bytes;
    }
    result = snprintf(
        metrics.terminal_record, sizeof(metrics.terminal_record),
        "[AURORA64 L1] ev=%lu att=%lu pub=%lu state=%u "
        "heap=%lu/%lu/%lu alloc_valid=%u owned=%s allocs=%s "
        "adapt=%llu/%llu task=%llu/%llu hdl=d%lu/%lu,f%lu/%lu "
        "polls=%lu ent=%lu/%lu/%llu bytes=%lu/%lu/%llu maxpoll=%lu "
        "scan=%llu:%u pause=%lu:%u cancel=%lu:%u agap=%lu ugap=%lu "
        "publication_facts_valid=%u retained_path_detail_valid=%u "
        "records=%s paths=%s/%s warn=%s err=%s sfail=%u bfail=%u "
        "trace=id%lu,g%lu,v%02x,a%02x,p%u,t=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu "
        "flt=alloc%u,count%u,time%u,overlay%u\n",
        (unsigned long)metrics.value.terminal_event_mask,
        (unsigned long)metrics.value.attempted_generation,
        (unsigned long)metrics.value.published_generation,
        (unsigned int)metrics.value.terminal_scanner_state,
        (unsigned long)metrics.value.free_heap_current,
        (unsigned long)metrics.value.free_heap_minimum,
        (unsigned long)metrics.value.free_heap_after_init,
        metrics.value.allocation_accounting_available ? 1U : 0U,
        owned_text,
        allocs_text,
        (unsigned long long)metrics.value.adapter_context_current_bytes,
        (unsigned long long)metrics.value.adapter_context_peak_bytes,
        (unsigned long long)metrics.value.task_current_bytes,
        (unsigned long long)metrics.value.task_peak_bytes,
        (unsigned long)metrics.value.directory_handles_current,
        (unsigned long)metrics.value.directory_handles_peak,
        (unsigned long)metrics.value.file_handles_current,
        (unsigned long)metrics.value.file_handles_peak,
        (unsigned long)metrics.value.poll_count,
        (unsigned long)metrics.value.last_poll_entries,
        (unsigned long)metrics.value.max_poll_entries,
        (unsigned long long)metrics.value.total_entries,
        (unsigned long)metrics.value.last_poll_read_bytes,
        (unsigned long)metrics.value.max_poll_read_bytes,
        (unsigned long long)metrics.value.total_read_bytes,
        (unsigned long)metrics.value.max_poll_ticks,
        (unsigned long long)metrics.value.scan_duration_ticks,
        metrics.value.scan_duration_valid ? 1U : 0U,
        (unsigned long)metrics.value.pause_ticks,
        metrics.value.pause_duration_valid ? 1U : 0U,
        (unsigned long)metrics.value.cancel_ticks,
        metrics.value.cancel_duration_valid ? 1U : 0U,
        (unsigned long)metrics.value.max_action_gap_ticks,
        (unsigned long)metrics.value.max_usb_gap_ticks,
        metrics.value.publication_facts_valid ? 1U : 0U,
        metrics.value.retained_path_detail_valid ? 1U : 0U,
        records_text,
        path_count_text,
        path_bytes_text,
        warnings_text,
        errors_text,
        (unsigned int)metrics.value.first_scanner_failure,
        (unsigned int)metrics.value.first_builder_failure,
        (unsigned long)metrics.value.trace.transition_id,
        (unsigned long)metrics.value.trace.generation,
        (unsigned int)metrics.value.trace.valid_mask,
        (unsigned int)metrics.value.trace.applicable_mask,
        metrics.value.trace.protocol_invalid ? 1U : 0U,
        (unsigned long)metrics.value.trace.ticks[0],
        (unsigned long)metrics.value.trace.ticks[1],
        (unsigned long)metrics.value.trace.ticks[2],
        (unsigned long)metrics.value.trace.ticks[3],
        (unsigned long)metrics.value.trace.ticks[4],
        (unsigned long)metrics.value.trace.ticks[5],
        (unsigned long)metrics.value.trace.ticks[6],
        (unsigned long)metrics.value.trace.ticks[7],
        metrics.value.allocation_invalid ? 1U : 0U,
        metrics.value.counter_overflow ? 1U : 0U,
        metrics.value.timing_invalid ? 1U : 0U,
        metrics.value.overlay_truncated ? 1U : 0U);
    metrics.terminal_record[sizeof(metrics.terminal_record) - 1U] = '\0';
    if (result < 0) {
        metrics.value.terminal_truncated = true;
        return 0U;
    }
    if ((size_t)result >= sizeof(metrics.terminal_record)) {
        metrics.value.terminal_truncated = true;
        return sizeof(metrics.terminal_record) - 1U;
    }
    return (size_t)result;
}

bool library_metrics_emit_pending(library_metrics_writer_t writer,
                                  void *context)
{
    int written;
    if (writer == NULL || !metrics.value.terminal_pending ||
        !metrics.value.terminal_eligible ||
        metrics.value.critical_interval_active ||
        metrics.terminal_detail_incomplete) {
        return false;
    }
    if (!metrics.terminal_formatted) {
        metrics.terminal_length = format_terminal_record();
        metrics.terminal_formatted = true;
    }
    written = writer(context, metrics.terminal_record,
                     metrics.terminal_length);
    if (written < 0 || (size_t)written != metrics.terminal_length)
        return false;
    metrics.value.terminal_pending = false;
    metrics.value.terminal_eligible = false;
    metrics.value.terminal_event_mask = 0U;
    metrics.terminal_formatted = false;
    metrics.terminal_length = 0U;
    return true;
}

#endif
