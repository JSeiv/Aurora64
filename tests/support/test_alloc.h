#ifndef TESTS_SUPPORT_TEST_ALLOC_H
#define TESTS_SUPPORT_TEST_ALLOC_H

#include <stddef.h>

/* fail_after stays sticky until reset; failed realloc leaves ptr valid. */
void test_alloc_fail_after(size_t successful_allocations);
void test_alloc_reset(void);
void *test_malloc(size_t size);
void *test_calloc(size_t count, size_t size);
void *test_realloc(void *ptr, size_t size);
void test_free(void *ptr);

#endif
