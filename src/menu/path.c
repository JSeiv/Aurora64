/**
 * @file path.c
 * @brief Path manipulation functions
 * @ingroup menu
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "path.h"

#define PATH_CAPACITY_INITIAL   255
#define PATH_CAPACITY_ALIGNMENT 32

#ifdef PATH_HOST_TEST
static size_t path_test_fail_after = SIZE_MAX;
static size_t path_test_successes;
static size_t path_test_live;

static bool path_test_should_fail(void) { return path_test_successes >= path_test_fail_after; }
static void *path_alloc_calloc(size_t count, size_t size) {
    void *allocation;
    if (path_test_should_fail()) return NULL;
    allocation = calloc(count, size);
    if (allocation != NULL) { ++path_test_successes; ++path_test_live; }
    return allocation;
}
static void *path_alloc_realloc(void *old, size_t size) {
    void *allocation;
    if (path_test_should_fail()) return NULL;
    allocation = realloc(old, size);
    if (allocation != NULL) { ++path_test_successes; if (old == NULL) ++path_test_live; }
    return allocation;
}
static void path_alloc_free(void *allocation) {
    if (allocation != NULL) --path_test_live;
    free(allocation);
}
void path_host_test_fail_after(size_t successful_allocations) {
    path_test_successes = 0U;
    path_test_fail_after = successful_allocations;
}
void path_host_test_reset(void) {
    path_test_successes = 0U;
    path_test_fail_after = SIZE_MAX;
}
size_t path_host_test_live_allocations(void) { return path_test_live; }
#else
#define path_alloc_calloc calloc
#define path_alloc_realloc realloc
#define path_alloc_free free
#endif

/**
 * @brief Resize the path buffer to accommodate the specified minimum length.
 * 
 * @param path Pointer to the path structure.
 * @param min_length Minimum length to accommodate.
 */
static void path_resize (path_t *path, size_t min_length) {
    path->capacity = min_length > PATH_CAPACITY_INITIAL ? min_length : PATH_CAPACITY_INITIAL;
    size_t alignment = path->capacity % PATH_CAPACITY_ALIGNMENT;
    if (alignment != (PATH_CAPACITY_ALIGNMENT - 1)) {
        path->capacity += PATH_CAPACITY_ALIGNMENT - alignment;
    }
    path->buffer = path_alloc_realloc(path->buffer, (path->capacity + 1) * sizeof(char));
    assert(path->buffer != NULL);
}

/**
 * @brief Create a new path structure.
 * 
 * @param string Initial path string.
 * @return path_t* Pointer to the created path structure.
 */
path_t *path_create (const char *string) {
    if (string == NULL) {
        string = "";
    }
    path_t *path = path_alloc_calloc(1, sizeof(path_t));
    assert(path != NULL);
    path_resize(path, strlen(string));
    memset(path->buffer, 0, path->capacity + 1);
    strcpy(path->buffer, string);
    path->root = path->buffer;
    return path;
}

/**
 * @brief Append a string to the path.
 * 
 * @param path Pointer to the path structure.
 * @param string String to append.
 */
static void path_append (path_t *path, char *string) {
    size_t buffer_length = strlen(path->buffer);
    size_t string_length = strlen(string);
    size_t new_path_length = buffer_length + string_length;
    if (new_path_length > path->capacity) {
        path_resize(path, new_path_length);
    }
    strcat(path->buffer, string);
}

/**
 * @brief Initialize a new path structure with a prefix and an initial string.
 * 
 * @param prefix Path prefix.
 * @param string Initial path string.
 * @return path_t* Pointer to the initialized path structure.
 */
path_t *path_init (const char *prefix, char *string) {
    path_t *path = path_create(prefix);
    size_t prefix_length = strlen(prefix);
    if ((prefix_length > 0) && (prefix[prefix_length - 1] == '/')) {
        path->root = path->buffer + prefix_length - 1;
    } else {
        path->root = path->buffer + prefix_length;
        path_append(path, "/");
    }
    path_push(path, string);
    return path;
}

static bool path_try_create(path_t **out, const char *string, size_t root_offset) {
    path_t *created;
    size_t length;
    size_t capacity;
    size_t alignment;
    if (out == NULL) return false;
    *out = NULL;
    if (string == NULL) string = "";
    length = strlen(string);
    if (root_offset > length) return false;
    capacity = length > PATH_CAPACITY_INITIAL ? length : PATH_CAPACITY_INITIAL;
    alignment = capacity % PATH_CAPACITY_ALIGNMENT;
    if (alignment != PATH_CAPACITY_ALIGNMENT - 1U) {
        if (capacity > SIZE_MAX - (PATH_CAPACITY_ALIGNMENT - alignment)) return false;
        capacity += PATH_CAPACITY_ALIGNMENT - alignment;
    }
    created = path_alloc_calloc(1U, sizeof(*created));
    if (created == NULL) return false;
    created->buffer = path_alloc_calloc(capacity + 1U, sizeof(char));
    if (created->buffer == NULL) {
        path_alloc_free(created);
        return false;
    }
    memcpy(created->buffer, string, length + 1U);
    created->capacity = capacity;
    created->root = created->buffer + root_offset;
    *out = created;
    return true;
}

bool path_try_init(path_t **out, const char *prefix, const char *string) {
    path_t *created;
    size_t prefix_length;
    size_t path_offset;
    size_t path_length;
    size_t total;
    size_t root_offset;
    size_t capacity;
    size_t alignment;
    if (out == NULL) return false;
    *out = NULL;
    if (prefix == NULL || string == NULL) return false;
    prefix_length = strlen(prefix);
    path_offset = string[0] == '/' ? 1U : 0U;
    path_length = strlen(string + path_offset);
    if (prefix_length > SIZE_MAX - path_length - 2U) return false;
    total = prefix_length + path_length + 1U;
    capacity = total > PATH_CAPACITY_INITIAL ? total : PATH_CAPACITY_INITIAL;
    alignment = capacity % PATH_CAPACITY_ALIGNMENT;
    if (alignment != PATH_CAPACITY_ALIGNMENT - 1U) {
        if (capacity > SIZE_MAX - (PATH_CAPACITY_ALIGNMENT - alignment)) return false;
        capacity += PATH_CAPACITY_ALIGNMENT - alignment;
    }
    created = path_alloc_calloc(1U, sizeof(*created));
    if (created == NULL) return false;
    created->buffer = path_alloc_calloc(capacity + 1U, sizeof(char));
    if (created->buffer == NULL) {
        path_alloc_free(created);
        return false;
    }
    memcpy(created->buffer, prefix, prefix_length);
    if (prefix_length > 0U && prefix[prefix_length - 1U] == '/') {
        root_offset = prefix_length - 1U;
    } else {
        root_offset = prefix_length;
        created->buffer[prefix_length++] = '/';
    }
    memcpy(created->buffer + prefix_length, string + path_offset, path_length + 1U);
    created->capacity = capacity;
    created->root = created->buffer + root_offset;
    *out = created;
    return true;
}

/**
 * @brief Free the memory allocated for the path structure.
 * 
 * @param path Pointer to the path structure.
 */
void path_free (path_t *path) {
    if (path != NULL) {
        path_alloc_free(path->buffer);
        path_alloc_free(path);
    }
}

/**
 * @brief Clone the path structure.
 * 
 * @param path Pointer to the path structure to clone.
 * @return path_t* Pointer to the cloned path structure.
 */
path_t *path_clone (path_t *path) {
    path_t *cloned = path_create(path->buffer);
    cloned->root = cloned->buffer + (path->root - path->buffer);
    return cloned;
}

bool path_try_clone(path_t **out, const path_t *source) {
    ptrdiff_t root_offset;
    if (out == NULL) return false;
    *out = NULL;
    if (source == NULL || source->buffer == NULL || source->root == NULL ||
        source->root < source->buffer) return false;
    root_offset = source->root - source->buffer;
    if ((size_t)root_offset > strlen(source->buffer)) return false;
    return path_try_create(out, source->buffer, (size_t)root_offset);
}

/**
 * @brief Clone the path structure and push a string to the cloned path.
 * 
 * @param path Pointer to the path structure to clone.
 * @param string String to push to the cloned path.
 * @return path_t* Pointer to the cloned and modified path structure.
 */
path_t *path_clone_push (path_t *path, char *string) {
    path_t *cloned = path_clone(path);
    path_push(cloned, string);
    return cloned;
}

/**
 * @brief Get the current path string.
 * 
 * @param path Pointer to the path structure.
 * @return char* Pointer to the current path string.
 */
char *path_get (path_t *path) {
    return path->buffer;
}

/**
 * @brief Get the last component of the path.
 * 
 * @param path Pointer to the path structure.
 * @return char* Pointer to the last component of the path.
 */
char *path_last_get (path_t *path) {
    char *last_slash = strrchr(path->root, '/');
    return (last_slash == NULL) ? path->root : (last_slash + 1);
}

/**
 * @brief Check if the path is the root path.
 * 
 * @param path Pointer to the path structure.
 * @return true if the path is the root path, false otherwise.
 */
bool path_is_root (path_t *path) {
    return (strcmp(path->root, "/") == 0);
}

/**
 * @brief Pop the last component from the path.
 * 
 * @param path Pointer to the path structure.
 */
void path_pop (path_t *path) {
    if (path_is_root(path)) {
        return;
    }
    char *last_slash = strrchr(path->root, '/');
    if (last_slash == path->root) {
        *(last_slash + 1) = '\0';
    } else if (last_slash != NULL) {
        *last_slash = '\0';
    }
}

/**
 * @brief Push a string to the path.
 * 
 * @param path Pointer to the path structure.
 * @param string String to push to the path.
 */
void path_push (path_t *path, char *string) {
    if (path->buffer[strlen(path->buffer) - 1] != '/') {
        path_append(path, "/");
    }
    if (string[0] == '/') {
        string += 1;
    }
    path_append(path, string);
}

/**
 * @brief Push a subdirectory to the path.
 * 
 * @param path Pointer to the path structure.
 * @param string Subdirectory string to push to the path.
 */
void path_push_subdir (path_t *path, char *string) {
    char *file = path_last_get(path);
    char *tmp = alloca(strlen(file) + 1);
    strcpy(tmp, file);
    path_pop(path);
    path_push(path, string);
    path_push(path, tmp);
}

/**
 * @brief Get the file extension from the path.
 * 
 * @param path Pointer to the path structure.
 * @return char* Pointer to the file extension.
 */
char *path_ext_get (path_t *path) {
    char *buffer = path_last_get(path);
    char *last_dot = strrchr(buffer, '.');
    if (last_dot != NULL) {
        return last_dot + 1;
    }
    return NULL;
}

/**
 * @brief Remove the file extension from the path.
 * 
 * @param path Pointer to the path structure.
 */
void path_ext_remove (path_t *path) {
    char *buffer = path_last_get(path);
    char *last_dot = strrchr(buffer, '.');
    if (last_dot != NULL) {
        *last_dot = '\0';
    }
}

/**
 * @brief Replace the file extension in the path.
 * 
 * @param path Pointer to the path structure.
 * @param ext New file extension.
 */
void path_ext_replace (path_t *path, char *ext) {
    path_ext_remove(path);
    path_append(path, ".");
    path_append(path, ext);
}

/**
 * @brief Check if the path has a value.
 * 
 * @param path Pointer to the path structure.
 * @return true if the path has a value, false otherwise.
 */
bool path_has_value(path_t *path) {
    if(path != NULL) {
        if(strlen(path->buffer) > 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check if two paths match.
 * 
 * @param left Pointer to the first path structure.
 * @param right Pointer to the second path structure.
 * @return true if the paths match, false otherwise.
 */
bool path_are_match(path_t *left, path_t *right) {
    if(!path_has_value(left) && !path_has_value(right)) {
        return true;
    } else if(path_has_value(left) && path_has_value(right)) {
        return (strcmp(path_get(left), path_get(right)) == 0);
    } else {
        return false;
    }
}