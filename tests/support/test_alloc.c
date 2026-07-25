#include "test_alloc.h"

#include <stdbool.h>
#include <stdlib.h>

static bool failure_enabled;
static size_t successful_allocations;
static size_t failure_threshold;

static bool should_fail(void)
{
    return failure_enabled && successful_allocations >= failure_threshold;
}

static void record_success(void *allocation)
{
    if (allocation != NULL) {
        successful_allocations++;
    }
}

void test_alloc_fail_after(size_t allocation_count)
{
    failure_enabled = true;
    successful_allocations = 0U;
    failure_threshold = allocation_count;
}

void test_alloc_reset(void)
{
    failure_enabled = false;
    successful_allocations = 0U;
    failure_threshold = 0U;
}

void *test_malloc(size_t size)
{
    void *allocation;

    if (should_fail()) {
        return NULL;
    }

    allocation = malloc(size);
    record_success(allocation);
    return allocation;
}

void *test_calloc(size_t count, size_t size)
{
    void *allocation;

    if (should_fail()) {
        return NULL;
    }

    allocation = calloc(count, size);
    record_success(allocation);
    return allocation;
}

void *test_realloc(void *ptr, size_t size)
{
    void *allocation;

    if (should_fail()) {
        return NULL;
    }

    allocation = realloc(ptr, size);
    record_success(allocation);
    return allocation;
}

void test_free(void *ptr)
{
    free(ptr);
}
