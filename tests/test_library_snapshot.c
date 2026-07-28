#define TEST_NO_MAIN
#include "acutest.h"
#include "menu/library/library_snapshot.h"

#include <stdlib.h>
#include <string.h>

typedef struct allocation_header {
    size_t size;
} allocation_header_t;

typedef struct {
    size_t calls;
    size_t fail_at;
    size_t live_bytes;
    size_t peak_bytes;
    size_t live_blocks;
} snapshot_alloc_t;

static void *tracked_malloc(void *context, size_t size)
{
    snapshot_alloc_t *state = context;
    allocation_header_t *header;
    if (state->calls++ == state->fail_at) return NULL;
    header = malloc(sizeof(*header) + size);
    if (header == NULL) return NULL;
    header->size = size;
    state->live_bytes += size;
    if (state->live_bytes > state->peak_bytes) state->peak_bytes = state->live_bytes;
    ++state->live_blocks;
    return header + 1;
}

static void *tracked_calloc(void *context, size_t count, size_t size)
{
    size_t total;
    void *result;
    if (size != 0U && count > SIZE_MAX / size) return NULL;
    total = count * size;
    result = tracked_malloc(context, total);
    if (result != NULL) memset(result, 0, total);
    return result;
}

static void tracked_free(void *context, void *pointer)
{
    snapshot_alloc_t *state = context;
    allocation_header_t *header;
    if (pointer == NULL) return;
    header = (allocation_header_t *)pointer - 1;
    state->live_bytes -= header->size;
    --state->live_blocks;
    free(header);
}

static library_allocator_t tracked_allocator(snapshot_alloc_t *state)
{
    library_allocator_t allocator;
    allocator.context = state;
    allocator.malloc_fn = tracked_malloc;
    allocator.calloc_fn = tracked_calloc;
    allocator.free_fn = tracked_free;
    return allocator;
}

static library_scanner_record_t sample(const char *path, unsigned int id,
                                       rom_byte_order_t order)
{
    library_scanner_record_t value;
    memset(&value, 0, sizeof(value));
    value.logical_path = path;
    value.fingerprint.bytes[0] = (uint8_t)(id >> 8U);
    value.fingerprint.bytes[1] = (uint8_t)id;
    value.header.byte_order = order;
    memcpy(value.header.title, "Snapshot", 9U);
    memcpy(value.header.game_code, "NSME", 5U);
    memcpy(value.header.cartridge_id, "SM", 3U);
    value.header.country_code = (uint8_t)'E';
    value.header.region = ROM_REGION_NTSC_U;
    value.header.revision = 1U;
    value.source_signature.size = 4096U;
    value.source_signature.modified_time = 7;
    value.source_signature.normalized_header_crc32 = 11U;
    value.source_signature.normalized_sample_crc32 = 13U;
    return value;
}

static void begin_refresh(library_snapshot_store_t *store)
{
    TEST_ASSERT(library_snapshot_store_mark_stale(store));
    TEST_ASSERT(library_snapshot_store_begin_revalidation(store));
}

static void publish_one(library_snapshot_store_t *store,
                        const library_allocator_t *allocator,
                        const library_scanner_record_t *record)
{
    library_snapshot_builder_t *builder = NULL;
    TEST_ASSERT(library_snapshot_builder_create(&builder, allocator));
    TEST_ASSERT(library_snapshot_builder_add(builder, record));
    begin_refresh(store);
    TEST_ASSERT(library_snapshot_store_publish(store, builder, 0U, 0U));
}

void test_library_snapshot_atomic_generations(void)
{
    library_snapshot_store_t store;
    library_snapshot_builder_t *builder = NULL;
    library_snapshot_t *old;
    library_scanner_record_t a = sample("/roms/A.z64", 1U, ROM_BYTE_ORDER_Z64);
    library_scanner_record_t b = sample("/roms/B.z64", 2U, ROM_BYTE_ORDER_Z64);
    library_snapshot_store_init(&store);
    publish_one(&store, NULL, &a);
    old = library_snapshot_store_acquire(&store);
    TEST_ASSERT(old != NULL);
    TEST_CHECK(library_snapshot_generation(old) == 1U);
    TEST_CHECK(library_snapshot_status(old) == LIBRARY_SNAPSHOT_FRESH);
    TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
    TEST_ASSERT(library_snapshot_builder_add(builder, &b));
    TEST_CHECK(library_snapshot_generation(old) == 1U);
    TEST_CHECK(library_snapshot_record_count(old) == 1U);
    begin_refresh(&store);
    TEST_ASSERT(library_snapshot_store_publish(&store, builder, 2U, 0U));
    TEST_CHECK(library_snapshot_generation(old) == 1U);
    TEST_CHECK(library_snapshot_status(old) == LIBRARY_SNAPSHOT_FRESH);
    TEST_CHECK(library_snapshot_generation(store.published) == 2U);
    TEST_CHECK(library_snapshot_warning_count(store.published) == 2U);
    TEST_CHECK(store.retired != NULL);
    library_snapshot_release(old);
    library_snapshot_store_deinit(&store);
}

static library_snapshot_t *freeze_permutation(const size_t *order)
{
    library_scanner_record_t values[3];
    library_snapshot_builder_t *builder = NULL;
    library_snapshot_t *snapshot = NULL;
    size_t i;
    values[0] = sample("/roms/game.n64", 3U, ROM_BYTE_ORDER_N64);
    values[1] = sample("/ROMs/game.v64", 3U, ROM_BYTE_ORDER_V64);
    values[2] = sample("/roms/Game.z64", 3U, ROM_BYTE_ORDER_Z64);
    TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
    for (i = 0U; i < 3U; ++i)
        TEST_ASSERT(library_snapshot_builder_add(builder, &values[order[i]]));
    TEST_ASSERT(library_snapshot_builder_freeze(builder, 9U, &snapshot));
    library_snapshot_builder_destroy(builder);
    return snapshot;
}

void test_library_snapshot_collapse_order_lookup(void)
{
    const size_t forward[3] = { 0U, 1U, 2U };
    const size_t reverse[3] = { 2U, 1U, 0U };
    library_snapshot_t *a = freeze_permutation(forward);
    library_snapshot_t *b = freeze_permutation(reverse);
    const library_record_t *record;
    size_t i;
    TEST_ASSERT(a != NULL && b != NULL);
    TEST_CHECK(library_snapshot_record_count(a) == 1U);
    TEST_CHECK(library_snapshot_source_count(a) == 3U);
    record = library_snapshot_record_at(a, 0U);
    TEST_ASSERT(record != NULL);
    TEST_CHECK(record->source_first == 0U);
    TEST_CHECK(record->source_count == 3U);
    TEST_CHECK(record->primary_source_index == 0U);
    TEST_CHECK(strcmp(library_snapshot_source_path(a, 0U), "/roms/game.n64") == 0);
    TEST_CHECK(library_snapshot_find_fingerprint(a, &record->fingerprint) == record);
    for (i = 0U; i < 3U; ++i) {
        TEST_CHECK(strcmp(library_snapshot_source_path(a, i),
                          library_snapshot_source_path(b, i)) == 0);
        TEST_CHECK(memcmp(library_snapshot_source_at(a, i),
                          library_snapshot_source_at(b, i),
                          sizeof(library_source_t)) == 0);
    }
    library_snapshot_release(a);
    library_snapshot_release(b);
}

void test_library_snapshot_conflict_and_status(void)
{
    library_snapshot_store_t store;
    library_snapshot_builder_t *builder = NULL;
    library_scanner_record_t a = sample("/a.z64", 4U, ROM_BYTE_ORDER_Z64);
    library_scanner_record_t conflict = sample("/b.z64", 4U, ROM_BYTE_ORDER_Z64);
    library_snapshot_store_init(&store);
    TEST_CHECK(!library_snapshot_store_begin_revalidation(&store));
    TEST_CHECK(!library_snapshot_store_fail(&store));
    TEST_ASSERT(library_snapshot_store_mark_stale(&store));
    TEST_CHECK(!library_snapshot_store_mark_stale(&store));
    TEST_ASSERT(library_snapshot_store_begin_revalidation(&store));
    TEST_CHECK(!library_snapshot_store_publish(&store, NULL, 0U, 0U));
    TEST_CHECK(!library_snapshot_store_fail(&store));
    TEST_CHECK(library_snapshot_store_status(&store) == LIBRARY_SNAPSHOT_EMPTY);

    conflict.source_signature.size++;
    TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
    TEST_ASSERT(library_snapshot_builder_add(builder, &a));
    TEST_CHECK(!library_snapshot_builder_add(builder, &conflict));
    TEST_CHECK(!library_snapshot_builder_freeze(builder, 1U, &store.published));
    library_snapshot_builder_destroy(builder);

    conflict = a;
    conflict.logical_path = "/c.z64";
    conflict.source_signature.normalized_header_crc32++;
    TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
    TEST_ASSERT(library_snapshot_builder_add(builder, &a));
    TEST_CHECK(!library_snapshot_builder_add(builder, &conflict));
    library_snapshot_builder_destroy(builder);

    conflict = a;
    conflict.logical_path = "/header-conflict.z64";
    ++conflict.header.check_code;
    TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
    TEST_ASSERT(library_snapshot_builder_add(builder, &a));
    TEST_CHECK(!library_snapshot_builder_add(builder, &conflict));
    library_snapshot_builder_destroy(builder);
    library_snapshot_store_deinit(&store);
}

void test_library_snapshot_status_immutability_and_failure(void)
{
    library_snapshot_store_t store;
    library_snapshot_t *fresh;
    library_snapshot_t *stale;
    library_scanner_record_t a = sample("/a.z64", 5U, ROM_BYTE_ORDER_Z64);
    library_snapshot_store_init(&store);
    publish_one(&store, NULL, &a);
    fresh = library_snapshot_store_acquire(&store);
    TEST_ASSERT(library_snapshot_store_mark_stale(&store));
    stale = library_snapshot_store_acquire(&store);
    TEST_CHECK(library_snapshot_status(fresh) == LIBRARY_SNAPSHOT_FRESH);
    TEST_CHECK(library_snapshot_status(stale) == LIBRARY_SNAPSHOT_STALE);
    library_snapshot_release(fresh);
    TEST_ASSERT(library_snapshot_store_begin_revalidation(&store));
    library_snapshot_release(stale);
    TEST_ASSERT(library_snapshot_store_fail(&store));
    TEST_CHECK(library_snapshot_store_status(&store) == LIBRARY_SNAPSHOT_FAILED_STALE);
    TEST_CHECK(library_snapshot_status(store.published) == LIBRARY_SNAPSHOT_FAILED_STALE);
    TEST_CHECK(!library_snapshot_store_mark_stale(&store));
    library_snapshot_store_deinit(&store);
}

void test_library_snapshot_selection_reconciliation(void)
{
    library_scanner_record_t a = sample("/old/a.z64", 10U, ROM_BYTE_ORDER_Z64);
    library_scanner_record_t b = sample("/b.z64", 11U, ROM_BYTE_ORDER_Z64);
    library_scanner_record_t replacement = sample("/old/a.z64", 12U, ROM_BYTE_ORDER_Z64);
    library_scanner_record_t renamed = sample("/new/a.z64", 10U, ROM_BYTE_ORDER_Z64);
    library_snapshot_builder_t *builder = NULL;
    library_snapshot_t *snapshot = NULL;

    TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
    TEST_ASSERT(library_snapshot_builder_add(builder, &b));
    TEST_ASSERT(library_snapshot_builder_add(builder, &a));
    TEST_ASSERT(library_snapshot_builder_freeze(builder, 1U, &snapshot));
    TEST_CHECK(library_snapshot_find_fingerprint(snapshot, &a.fingerprint) != NULL);
    library_snapshot_release(snapshot);
    library_snapshot_builder_destroy(builder);

    TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
    TEST_ASSERT(library_snapshot_builder_add(builder, &replacement));
    TEST_ASSERT(library_snapshot_builder_add(builder, &renamed));
    TEST_ASSERT(library_snapshot_builder_freeze(builder, 2U, &snapshot));
    TEST_CHECK(library_snapshot_find_fingerprint(snapshot, &a.fingerprint) != NULL);
    TEST_CHECK(library_snapshot_find_fingerprint(snapshot, &b.fingerprint) == NULL);
    TEST_CHECK(library_snapshot_find_fingerprint(snapshot, &replacement.fingerprint) != NULL);
    TEST_CHECK(strcmp(library_snapshot_source_path(snapshot,
        library_snapshot_find_fingerprint(snapshot, &a.fingerprint)->source_first),
        "/new/a.z64") == 0);
    library_snapshot_release(snapshot);
    library_snapshot_builder_destroy(builder);
}

void test_library_snapshot_capacity_poison_and_cancel(void)
{
    library_snapshot_builder_t *builder = NULL;
    library_snapshot_t *snapshot = NULL;
    library_scanner_record_t value;
    char path[32];
    size_t i;

    TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
    for (i = 0U; i < 512U; ++i) {
        (void)snprintf(path, sizeof(path), "/d%03lu.z64", (unsigned long)i);
        value = sample(path, 1U, ROM_BYTE_ORDER_Z64);
        TEST_ASSERT(library_snapshot_builder_add(builder, &value));
    }
    value = sample("/overflow.z64", 1U, ROM_BYTE_ORDER_Z64);
    TEST_CHECK(!library_snapshot_builder_add(builder, &value));
    TEST_CHECK(!library_snapshot_builder_freeze(builder, 1U, &snapshot));
    library_snapshot_builder_destroy(builder);

    TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
    for (i = 0U; i < 257U; ++i) {
        (void)snprintf(path, sizeof(path), "/u%03lu.z64", (unsigned long)i);
        value = sample(path, (unsigned int)i, ROM_BYTE_ORDER_Z64);
        TEST_ASSERT(library_snapshot_builder_add(builder, &value));
    }
    TEST_CHECK(!library_snapshot_builder_freeze(builder, 1U, &snapshot));
    library_snapshot_builder_destroy(builder);

    {
        char longest[LIBRARY_SNAPSHOT_PATH_BYTES];
        char too_long[LIBRARY_SNAPSHOT_PATH_BYTES + 1U];
        memset(longest, 'a', sizeof(longest));
        longest[0] = '/';
        longest[sizeof(longest) - 1U] = '\0';
        memset(too_long, 'b', sizeof(too_long));
        too_long[0] = '/';
        too_long[sizeof(too_long) - 1U] = '\0';
        TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
        value = sample(longest, 1U, ROM_BYTE_ORDER_Z64);
        TEST_ASSERT(library_snapshot_builder_add(builder, &value));
        value = sample(too_long, 1U, ROM_BYTE_ORDER_Z64);
        TEST_CHECK(!library_snapshot_builder_add(builder, &value));
        library_snapshot_builder_destroy(builder);
    }

    {
        char pooled[LIBRARY_SNAPSHOT_PATH_BYTES];
        TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
        for (i = 0U; i < 64U; ++i) {
            memset(pooled, 'p', sizeof(pooled));
            pooled[0] = '/';
            pooled[sizeof(pooled) - 3U] = (char)('A' + (i / 26U));
            pooled[sizeof(pooled) - 2U] = (char)('A' + (i % 26U));
            pooled[sizeof(pooled) - 1U] = '\0';
            value = sample(pooled, 1U, ROM_BYTE_ORDER_Z64);
            TEST_ASSERT(library_snapshot_builder_add(builder, &value));
        }
        memset(pooled, 'q', sizeof(pooled));
        pooled[0] = '/';
        pooled[sizeof(pooled) - 1U] = '\0';
        value = sample(pooled, 1U, ROM_BYTE_ORDER_Z64);
        TEST_CHECK(!library_snapshot_builder_add(builder, &value));
        library_snapshot_builder_destroy(builder);
    }

    TEST_ASSERT(library_snapshot_builder_create(&builder, NULL));
    TEST_CHECK(!library_snapshot_builder_add_scanner(builder, NULL));
    value = sample("/after-cancel.z64", 1U, ROM_BYTE_ORDER_Z64);
    TEST_CHECK(!library_snapshot_builder_add(builder, &value));
    library_snapshot_builder_destroy(builder);
}

void test_library_snapshot_oom_retains_publication(void)
{
    {
        snapshot_alloc_t state = { 0U, 0U, 0U, 0U, 0U };
        library_allocator_t allocator = tracked_allocator(&state);
        library_snapshot_builder_t *builder = NULL;
        TEST_CHECK(!library_snapshot_builder_create(&builder, &allocator));
        TEST_CHECK(builder == NULL);
        TEST_CHECK(state.live_bytes == 0U);
    }
    {
        snapshot_alloc_t state = { 0U, SIZE_MAX, 0U, 0U, 0U };
        library_allocator_t allocator = tracked_allocator(&state);
        library_snapshot_builder_t *builder = NULL;
        library_scanner_record_t value =
            sample("/staging.z64", 19U, ROM_BYTE_ORDER_Z64);
        TEST_ASSERT(library_snapshot_builder_create(&builder, &allocator));
        state.fail_at = state.calls;
        TEST_CHECK(!library_snapshot_builder_add(builder, &value));
        library_snapshot_builder_destroy(builder);
        TEST_CHECK(state.live_bytes == 0U);
    }
    {
        snapshot_alloc_t state = { 0U, SIZE_MAX, 0U, 0U, 0U };
        library_allocator_t allocator = tracked_allocator(&state);
        library_snapshot_store_t store;
        library_scanner_record_t value =
            sample("/status.z64", 19U, ROM_BYTE_ORDER_Z64);
        library_snapshot_store_init(&store);
        publish_one(&store, &allocator, &value);
        state.fail_at = state.calls;
        TEST_CHECK(!library_snapshot_store_mark_stale(&store));
        TEST_CHECK(library_snapshot_store_status(&store) == LIBRARY_SNAPSHOT_FRESH);
        TEST_CHECK(library_snapshot_find_fingerprint(store.published,
                                                     &value.fingerprint) != NULL);
        library_snapshot_store_deinit(&store);
        TEST_CHECK(state.live_bytes == 0U);
    }
    {
        size_t failure;
        for (failure = 0U; failure < 4U; ++failure) {
        snapshot_alloc_t state = { 0U, SIZE_MAX, 0U, 0U, 0U };
        library_allocator_t allocator = tracked_allocator(&state);
        library_snapshot_store_t store;
        library_snapshot_builder_t *builder = NULL;
        library_snapshot_t *old;
        library_scanner_record_t a = sample("/a.z64", 20U, ROM_BYTE_ORDER_Z64);
        library_scanner_record_t b = sample("/b.z64", 21U, ROM_BYTE_ORDER_Z64);
        size_t base_calls;
        library_snapshot_store_init(&store);
        publish_one(&store, &allocator, &a);
        old = library_snapshot_store_acquire(&store);
        TEST_ASSERT(old != NULL);
        TEST_ASSERT(library_snapshot_builder_create(&builder, &allocator));
        TEST_ASSERT(library_snapshot_builder_add(builder, &b));
        begin_refresh(&store);
        base_calls = state.calls;
        state.fail_at = base_calls + failure;
        if (failure < 3U) {
            TEST_CHECK(!library_snapshot_store_publish(&store, builder, 0U, 0U));
            TEST_CHECK(library_snapshot_generation(old) == 1U);
            TEST_CHECK(library_snapshot_find_fingerprint(old, &a.fingerprint) != NULL);
            library_snapshot_builder_destroy(builder);
        } else {
            /* Freeze has seed + payload + handle allocations; later offsets succeed. */
            TEST_ASSERT(library_snapshot_store_publish(&store, builder, 0U, 0U));
        }
        library_snapshot_release(old);
        library_snapshot_store_deinit(&store);
        TEST_CHECK(state.live_blocks == 0U);
        TEST_CHECK(state.live_bytes == 0U);
        }
    }
}

void test_library_snapshot_heap_accounting(void)
{
    snapshot_alloc_t state = { 0U, SIZE_MAX, 0U, 0U, 0U };
    library_allocator_t allocator = tracked_allocator(&state);
    library_snapshot_store_t store;
    library_snapshot_builder_t *builder = NULL;
    library_snapshot_t *old;
    library_scanner_record_t value;
    char path[48];
    size_t i;
    size_t exposed;
    library_snapshot_store_init(&store);
    TEST_ASSERT(library_snapshot_builder_create(&builder, &allocator));
    for (i = 0U; i < 512U; ++i) {
        (void)snprintf(path, sizeof(path), "/r/%03lu/game.z64", (unsigned long)i);
        value = sample(path, (unsigned int)(i % 256U), ROM_BYTE_ORDER_Z64);
        TEST_ASSERT(library_snapshot_builder_add(builder, &value));
    }
    TEST_CHECK(library_snapshot_builder_allocation_size(builder) == state.live_bytes);
    begin_refresh(&store);
    TEST_ASSERT(library_snapshot_store_publish(&store, builder, 0U, 0U));
    old = library_snapshot_store_acquire(&store);
    TEST_ASSERT(old != NULL);

    TEST_ASSERT(library_snapshot_builder_create(&builder, &allocator));
    for (i = 0U; i < 512U; ++i) {
        (void)snprintf(path, sizeof(path), "/s/%03lu/game.z64", (unsigned long)i);
        value = sample(path, (unsigned int)(i % 256U), ROM_BYTE_ORDER_Z64);
        TEST_ASSERT(library_snapshot_builder_add(builder, &value));
    }
    begin_refresh(&store);
    TEST_ASSERT(library_snapshot_store_publish(&store, builder, 0U, 0U));

    exposed = library_snapshot_store_allocation_size(&store);
    TEST_CHECK(exposed == state.live_bytes);
    TEST_CHECK(state.peak_bytes <= LIBRARY_SNAPSHOT_MAX_LIVE_HEAP);
    TEST_CHECK(exposed <= LIBRARY_SNAPSHOT_MAX_LIVE_HEAP);
    TEST_CHECK(sizeof(((library_record_t *)0)->source_first) == sizeof(uint32_t));
    TEST_CHECK(sizeof(((library_record_t *)0)->source_count) == sizeof(uint16_t));
    TEST_CHECK(sizeof(((library_record_t *)0)->primary_source_index) == sizeof(uint16_t));
    library_snapshot_release(old);
    library_snapshot_store_deinit(&store);
    TEST_CHECK(state.live_bytes == 0U);
}
