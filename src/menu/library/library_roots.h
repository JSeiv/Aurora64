#ifndef LIBRARY_ROOTS_H__
#define LIBRARY_ROOTS_H__

#include "menu/library/library_fs.h"

#include <stdbool.h>
#include <stddef.h>

/* Capacity includes the terminating NUL. */
#define LIBRARY_ROOT_PATH_CAPACITY 512U
#define LIBRARY_ROOT_MAX_CONFIGURED 4U
#define LIBRARY_ROOT_MAX_EFFECTIVE 1U

typedef struct {
    size_t count;
    char paths[LIBRARY_ROOT_MAX_EFFECTIVE][LIBRARY_ROOT_PATH_CAPACITY];
} library_roots_t;

/* Failure clears output[0] when output has nonzero capacity. */
bool library_root_normalize(const char *input, char *output, size_t output_capacity);

/* ASCII-only case folding; bytes at or above 0x80 compare exactly. */
bool library_path_ascii_equal(const char *left, const char *right);

/* Failure leaves the complete root set zeroed. */
bool library_roots_configure(const char *const *inputs, size_t input_count,
                             library_roots_t *out);

/* The frozen production root set contains exactly the logical storage root. */
const library_roots_t *library_roots_default(void);

/*
 * basename must be a basename, not a path. Invalid, NULL, slash-containing,
 * or nonterminated values fail closed (are excluded).
 */
bool library_basename_is_excluded(bool at_storage_root, const char *basename);

#endif
