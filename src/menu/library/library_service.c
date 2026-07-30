#include "menu/library/library_service.h"

#include <stdlib.h>
#include <string.h>

struct library_service {
    library_allocator_t allocator;
    library_roots_t roots;
    library_scan_budget_t budget;
    library_fs_t *fs;
    library_fs_t owned_fs;
    library_scanner_t *scanner;
    library_snapshot_builder_t *builder;
    library_snapshot_store_t store;
    const char *storage_prefix;
    menu_mode_t transition_destination;
    menu_mode_t released_origin;
    menu_mode_t released_destination;
    bool owns_fs;
    bool adapter_ready;
    bool in_poll;
    bool suppress_start;
    bool start_requested;
    bool resume_requested;
    bool pause_requested;
    bool cancel_requested;
    bool paused;
    bool failure_pending;
    bool publication_pending;
    bool transition_pending;
    bool transition_released;
    bool resume_after_transition;
};

#define LIBRARY_TARGET_HEAP_CAP_BYTES          229376U
#define LIBRARY_TARGET_ACTIVE_FIXED_BYTES      (98432U + 111960U + 14088U)
#define LIBRARY_TARGET_PUBLICATION_FIXED_BYTES 221896U

#if defined(__mips__)
_Static_assert(LIBRARY_TARGET_ACTIVE_FIXED_BYTES +
                       sizeof(struct library_service) <=
                   LIBRARY_TARGET_HEAP_CAP_BYTES,
               "active library service exceeds the 224 KiB target cap");
_Static_assert(LIBRARY_TARGET_PUBLICATION_FIXED_BYTES +
                       sizeof(struct library_service) <=
                   LIBRARY_TARGET_HEAP_CAP_BYTES,
               "publishing library service exceeds the 224 KiB target cap");
#endif

static void *service_libc_malloc(void *context, size_t size)
{
    (void)context;
    return malloc(size);
}

static void *service_libc_calloc(void *context, size_t count, size_t size)
{
    (void)context;
    return calloc(count, size);
}

static void service_libc_free(void *context, void *pointer)
{
    (void)context;
    free(pointer);
}

static bool allocator_valid(const library_allocator_t *allocator)
{
    return allocator != NULL && allocator->malloc_fn != NULL &&
           allocator->calloc_fn != NULL && allocator->free_fn != NULL;
}

static bool safe_mode(menu_mode_t mode)
{
    return mode == MENU_MODE_HOME || mode == MENU_MODE_LIBRARY;
}

static void reset_transition_state(library_service_t *service)
{
    service->transition_destination = MENU_MODE_NONE;
    service->released_origin = MENU_MODE_NONE;
    service->released_destination = MENU_MODE_NONE;
    service->transition_pending = false;
    service->transition_released = false;
    service->resume_after_transition = false;
}

static bool ensure_adapter(library_service_t *service)
{
    if (!service->owns_fs || service->adapter_ready) return true;
    if (!library_fs_libdragon_init(&service->owned_fs,
                                   service->storage_prefix)) return false;
    service->adapter_ready = true;
    return true;
}

static void release_adapter(library_service_t *service)
{
    if (!service->owns_fs || !service->adapter_ready) return;
    library_fs_libdragon_deinit(&service->owned_fs);
    service->adapter_ready = false;
}

static bool begin_revalidation(library_service_t *service)
{
    library_snapshot_status_t status =
        library_snapshot_store_status(&service->store);

    if (status == LIBRARY_SNAPSHOT_FAILED_STALE) {
        if (!library_snapshot_store_retry(&service->store)) return false;
        status = LIBRARY_SNAPSHOT_STALE;
    }
    if (status != LIBRARY_SNAPSHOT_STALE &&
        !library_snapshot_store_mark_stale(&service->store)) return false;
    if (!library_snapshot_store_begin_revalidation(&service->store)) return false;
    return true;
}

static bool settle_failure(library_service_t *service)
{
    if (library_snapshot_store_status(&service->store) ==
        LIBRARY_SNAPSHOT_REVALIDATING) {
        (void)library_snapshot_store_fail(&service->store);
        if (library_snapshot_store_status(&service->store) ==
            LIBRARY_SNAPSHOT_REVALIDATING) return false;
    }
    return true;
}

static void discard_builder(library_service_t *service)
{
    library_snapshot_builder_destroy(service->builder);
    service->builder = NULL;
    service->publication_pending = false;
}

static bool destroy_scanner(library_service_t *service)
{
    if (service->scanner == NULL) return true;
    if (!library_scanner_destroy(service->scanner)) return false;
    service->scanner = NULL;
    return true;
}

bool library_service_init(library_service_t **out,
                          const library_service_config_t *config)
{
    library_allocator_t allocator;
    library_service_t *service;

    if (out != NULL) *out = NULL;
    if (out == NULL || config == NULL || config->roots == NULL ||
        config->roots->count == 0U ||
        config->roots->count > LIBRARY_ROOT_MAX_EFFECTIVE ||
        config->budget.max_directory_entries == 0U ||
        config->budget.max_read_bytes == 0U || config->storage_prefix == NULL) {
        return false;
    }
    if (config->allocator == NULL) {
        allocator.context = NULL;
        allocator.malloc_fn = service_libc_malloc;
        allocator.calloc_fn = service_libc_calloc;
        allocator.free_fn = service_libc_free;
    } else if (allocator_valid(config->allocator)) {
        allocator = *config->allocator;
    } else {
        return false;
    }

    service = allocator.calloc_fn(allocator.context, 1U, sizeof(*service));
    if (service == NULL) return false;
    service->allocator = allocator;
    service->roots = *config->roots;
    service->budget = config->budget;
    service->storage_prefix = config->storage_prefix;
    service->owns_fs = config->fs == NULL;
    service->fs = service->owns_fs ? &service->owned_fs : config->fs;
    library_snapshot_store_init(&service->store);

    if (!service->owns_fs && service->fs == NULL) {
        allocator.free_fn(allocator.context, service);
        return false;
    }

    *out = service;
    return true;
}

void library_service_request_pause(library_service_t *service)
{
    if (service == NULL || service->cancel_requested) return;
    service->start_requested = false;
    service->resume_requested = false;
    if (service->scanner == NULL) {
        service->paused = true;
        service->pause_requested = false;
        release_adapter(service);
        return;
    }
    if (service->publication_pending || service->failure_pending ||
        library_scanner_state(service->scanner) == LIBRARY_SCANNER_COMPLETE ||
        library_scanner_state(service->scanner) == LIBRARY_SCANNER_FAILED) {
        return;
    }
    service->pause_requested = true;
    library_scanner_request_pause(service->scanner);
}

void library_service_resume(library_service_t *service)
{
    if (service == NULL || service->cancel_requested || !service->paused) return;
    service->resume_requested = true;
}

void library_service_request_cancel(library_service_t *service)
{
    if (service == NULL) return;
    reset_transition_state(service);
    service->start_requested = false;
    service->resume_requested = false;
    service->pause_requested = false;
    service->suppress_start = true;
    service->paused = false;
    if (service->scanner == NULL && service->builder == NULL &&
        !service->failure_pending && !service->publication_pending) {
        service->cancel_requested = false;
        release_adapter(service);
        return;
    }
    service->cancel_requested = true;
    if (service->scanner != NULL)
        library_scanner_request_cancel(service->scanner);
}

void library_service_restart(library_service_t *service)
{
    if (service == NULL) return;
    reset_transition_state(service);
    service->resume_requested = false;
    service->pause_requested = false;
    service->paused = false;
    service->suppress_start = true;
    service->start_requested = true;
    if (service->scanner != NULL || service->builder != NULL ||
        service->publication_pending || service->failure_pending) {
        service->cancel_requested = true;
        if (service->scanner != NULL)
            library_scanner_request_cancel(service->scanner);
    }
}

static void poll_cancel(library_service_t *service)
{
    if (service->builder != NULL) discard_builder(service);
    if (service->scanner != NULL) {
        library_scanner_request_cancel(service->scanner);
        (void)library_scanner_poll(service->scanner, &service->budget);
        if (library_scanner_state(service->scanner) != LIBRARY_SCANNER_IDLE)
            return;
        if (!destroy_scanner(service)) return;
    }
    if (!settle_failure(service)) return;
    service->failure_pending = false;
    service->publication_pending = false;
    service->cancel_requested = false;
    release_adapter(service);
}

static void poll_pause(library_service_t *service)
{
    if (service->scanner == NULL) {
        service->pause_requested = false;
        service->paused = true;
        release_adapter(service);
        return;
    }
    library_scanner_request_pause(service->scanner);
    (void)library_scanner_poll(service->scanner, &service->budget);
    if (library_scanner_state(service->scanner) == LIBRARY_SCANNER_QUIESCED) {
        service->pause_requested = false;
        service->paused = true;
        release_adapter(service);
    }
}

static bool create_and_start(library_service_t *service, bool *retryable)
{
    *retryable = false;
    if (library_snapshot_store_refresh_blocked(&service->store)) {
        *retryable = true;
        return false;
    }
    if (!ensure_adapter(service)) return false;
    if (!library_scanner_create(&service->scanner, service->fs,
                                &service->roots, &service->allocator)) {
        return false;
    }
    if (!begin_revalidation(service) ||
        !library_scanner_start(service->scanner)) {
        (void)destroy_scanner(service);
        (void)settle_failure(service);
        return false;
    }
    service->start_requested = false;
    service->resume_requested = false;
    service->resume_after_transition = false;
    service->suppress_start = true;
    return true;
}

static void prepare_publication(library_service_t *service)
{
    if (!library_snapshot_builder_create(&service->builder,
                                         &service->allocator) ||
        !library_snapshot_builder_add_scanner(service->builder,
                                              service->scanner)) {
        discard_builder(service);
        service->failure_pending = true;
        return;
    }
    /* A complete scanner has no handles. Publication must not overlap the
       production adapter's 14 KiB context. */
    release_adapter(service);
    service->publication_pending = true;
}

static void publish_snapshot(library_service_t *service)
{
    const library_scanner_stats_t *stats =
        library_scanner_stats(service->scanner);
    uint32_t warnings = stats == NULL
                            ? 0U
                            : stats->candidate_failures +
                                  stats->mutation_failures;

    if (library_snapshot_store_publish(&service->store, service->builder,
                                       warnings, 0U)) {
        service->builder = NULL; /* consumed by the store */
        service->publication_pending = false;
        (void)destroy_scanner(service);
        return;
    }
    if (library_snapshot_store_status(&service->store) ==
        LIBRARY_SNAPSHOT_REVALIDATING) {
        return; /* retired-reader refusal happened before freeze; retry */
    }
    discard_builder(service);
    (void)destroy_scanner(service);
}

void library_service_poll(library_service_t *service, menu_mode_t mode)
{
    library_scan_result_t result;
    bool retryable;
    bool safe;

    if (service == NULL || service->in_poll) return;
    service->in_poll = true;
    safe = safe_mode(mode);

    if (service->cancel_requested) {
        poll_cancel(service);
        service->in_poll = false;
        return;
    }
    if (service->pause_requested) {
        poll_pause(service);
        service->in_poll = false;
        return;
    }
    if (service->failure_pending) {
        if (settle_failure(service)) {
            discard_builder(service);
            (void)destroy_scanner(service);
            service->failure_pending = false;
            release_adapter(service);
        }
        service->in_poll = false;
        return;
    }
    if (service->publication_pending) {
        publish_snapshot(service);
        service->in_poll = false;
        return;
    }
    if (service->scanner != NULL &&
        library_scanner_state(service->scanner) == LIBRARY_SCANNER_COMPLETE) {
        prepare_publication(service);
        service->in_poll = false;
        return;
    }

    if (safe && service->paused &&
        (service->resume_requested || service->resume_after_transition)) {
        if (ensure_adapter(service) &&
            library_scanner_resume(service->scanner)) {
            service->paused = false;
            service->resume_requested = false;
            service->resume_after_transition = false;
        }
    }
    if (safe && service->scanner == NULL &&
        (service->start_requested || !service->suppress_start)) {
        if (!create_and_start(service, &retryable)) {
            if (!retryable) service->start_requested = false;
            service->suppress_start = true;
            release_adapter(service);
            service->in_poll = false;
            return;
        }
    }

    if (service->scanner != NULL &&
        library_scanner_state(service->scanner) == LIBRARY_SCANNER_SCANNING) {
        result = library_scanner_poll(service->scanner, &service->budget);
        if (result == LIBRARY_SCAN_FAILED_RESULT)
            service->failure_pending = true;
    }
    service->in_poll = false;
}

bool library_service_is_quiesced(const library_service_t *service)
{
    library_scanner_state_t state;
    if (service == NULL) return true;
    if (service->in_poll || service->start_requested ||
        service->resume_requested || service->pause_requested ||
        service->cancel_requested || service->failure_pending ||
        service->publication_pending || service->builder != NULL) {
        return false;
    }
    if (service->scanner == NULL) return true;
    state = library_scanner_state(service->scanner);
    return service->paused && state == LIBRARY_SCANNER_QUIESCED;
}

const library_snapshot_t *library_service_snapshot_acquire(
    library_service_t *service)
{
    return service == NULL ? NULL
                           : library_snapshot_store_acquire(&service->store);
}

void library_service_snapshot_release(const library_snapshot_t *snapshot)
{
    library_snapshot_release((library_snapshot_t *)snapshot);
}

bool library_service_coordinate_transition(library_service_t *service,
                                           menu_mode_t current,
                                           menu_mode_t *requested)
{
    if (service == NULL || requested == NULL) return true;
    if (service->transition_pending) {
        if (!library_service_is_quiesced(service)) {
            *requested = current;
            return false;
        }
        *requested = service->transition_destination;
        service->released_origin = current;
        service->released_destination = service->transition_destination;
        service->transition_released = true;
        service->transition_pending = false;
        service->resume_after_transition = true;
        return true;
    }
    if (service->transition_released) {
        if (current == service->released_origin &&
            *requested == service->released_destination) {
            return true;
        }
        if (current != service->released_origin)
            service->transition_released = false;
    }
    if (!safe_mode(current) || *requested == current) return true;
    service->transition_destination = *requested;
    service->transition_pending = true;
    service->transition_released = false;
    library_service_request_pause(service);
    *requested = current;
    return false;
}

size_t library_service_allocation_size(const library_service_t *service)
{
    return service == NULL ? 0U : sizeof(*service);
}

void library_service_free(library_service_t *service)
{
    library_allocator_t allocator;
    if (service == NULL || !library_service_is_quiesced(service)) return;
    if (service->builder != NULL) return;
    if (!destroy_scanner(service)) return;
    release_adapter(service);
    library_snapshot_store_deinit(&service->store);
    allocator = service->allocator;
    allocator.free_fn(allocator.context, service);
}
