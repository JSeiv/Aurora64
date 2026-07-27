#include "menu/library/library_roots.h"

#include <string.h>

static unsigned char ascii_fold(unsigned char value)
{
    if (value >= (unsigned char)'A' && value <= (unsigned char)'Z') {
        return (unsigned char)(value + ((unsigned char)'a' - (unsigned char)'A'));
    }
    return value;
}

static bool bounded_length(const char *text, size_t capacity, size_t *length)
{
    size_t index;

    if (text == NULL || length == NULL) return false;
    for (index = 0U; index < capacity; ++index) {
        if (text[index] == '\0') {
            *length = index;
            return true;
        }
    }
    return false;
}

bool library_root_normalize(const char *input, char *output, size_t output_capacity)
{
    size_t input_length;
    size_t read_at;
    size_t write_at = 1U;
    size_t component_starts[LIBRARY_ROOT_PATH_CAPACITY / 2U];
    size_t component_count = 0U;

    if (output != NULL && output_capacity > 0U) output[0] = '\0';
    if (output == NULL || output_capacity == 0U || input == NULL) return false;
    if (!bounded_length(input, LIBRARY_ROOT_PATH_CAPACITY, &input_length) ||
        input_length == 0U || input[0] != '/') return false;
    if (output_capacity < 2U) return false;

    output[0] = '/';
    output[1] = '\0';
    read_at = 1U;
    while (read_at < input_length) {
        size_t start;
        size_t length;
        size_t prior_write;

        while (read_at < input_length && input[read_at] == '/') ++read_at;
        if (read_at == input_length) break;
        start = read_at;
        while (read_at < input_length && input[read_at] != '/') ++read_at;
        length = read_at - start;
        if (length == 1U && input[start] == '.') continue;
        if (length == 2U && input[start] == '.' && input[start + 1U] == '.') {
            if (component_count == 0U) {
                output[0] = '\0';
                return false;
            }
            write_at = component_starts[--component_count];
            if (write_at > 1U) --write_at;
            output[write_at] = '\0';
            continue;
        }

        prior_write = write_at;
        if (write_at > 1U) {
            if (write_at + 1U >= output_capacity) {
                output[0] = '\0';
                return false;
            }
            output[write_at++] = '/';
        }
        if (length >= output_capacity - write_at) {
            output[0] = '\0';
            return false;
        }
        component_starts[component_count++] = (prior_write > 1U) ? prior_write + 1U : prior_write;
        memcpy(output + write_at, input + start, length);
        write_at += length;
        output[write_at] = '\0';
    }
    return true;
}

bool library_path_ascii_equal(const char *left, const char *right)
{
    size_t left_length;
    size_t right_length;
    size_t index;

    if (!bounded_length(left, LIBRARY_ROOT_PATH_CAPACITY, &left_length) ||
        !bounded_length(right, LIBRARY_ROOT_PATH_CAPACITY, &right_length) ||
        left_length != right_length) return false;
    for (index = 0U; index < left_length; ++index) {
        if (ascii_fold((unsigned char)left[index]) !=
            ascii_fold((unsigned char)right[index])) return false;
    }
    return true;
}

static int path_compare(const char *left, const char *right)
{
    size_t index = 0U;

    while (index < LIBRARY_ROOT_PATH_CAPACITY) {
        unsigned char a = ascii_fold((unsigned char)left[index]);
        unsigned char b = ascii_fold((unsigned char)right[index]);
        if (a != b) return (a < b) ? -1 : 1;
        if (a == 0U) return 0;
        ++index;
    }
    return 0;
}

static bool path_is_ancestor(const char *ancestor, const char *path)
{
    size_t ancestor_length;
    size_t path_length;
    size_t index;

    if (!bounded_length(ancestor, LIBRARY_ROOT_PATH_CAPACITY, &ancestor_length) ||
        !bounded_length(path, LIBRARY_ROOT_PATH_CAPACITY, &path_length) ||
        ancestor_length >= path_length) return false;
    if (ancestor_length == 1U && ancestor[0] == '/') return true;
    if (path[ancestor_length] != '/') return false;
    for (index = 0U; index < ancestor_length; ++index) {
        if (ascii_fold((unsigned char)ancestor[index]) !=
            ascii_fold((unsigned char)path[index])) return false;
    }
    return true;
}

bool library_roots_configure(const char *const *inputs, size_t input_count,
                             library_roots_t *out)
{
    char candidates[LIBRARY_ROOT_MAX_CONFIGURED][LIBRARY_ROOT_PATH_CAPACITY] = { { 0 } };
    bool removed[LIBRARY_ROOT_MAX_CONFIGURED] = { false, false, false, false };
    size_t candidate_count = 0U;
    size_t index;
    size_t other;
    size_t effective_count = 0U;

    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (inputs == NULL || input_count == 0U ||
        input_count > LIBRARY_ROOT_MAX_CONFIGURED) return false;

    for (index = 0U; index < input_count; ++index) {
        bool duplicate = false;
        if (!library_root_normalize(inputs[index], candidates[candidate_count],
                                    sizeof(candidates[candidate_count]))) return false;
        for (other = 0U; other < candidate_count; ++other) {
            if (library_path_ascii_equal(candidates[other], candidates[candidate_count])) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) ++candidate_count;
    }

    for (index = 1U; index < candidate_count; ++index) {
        char value[LIBRARY_ROOT_PATH_CAPACITY] = { 0 };
        size_t position = index;
        memcpy(value, candidates[index], sizeof(value));
        while (position > 0U && path_compare(value, candidates[position - 1U]) < 0) {
            memcpy(candidates[position], candidates[position - 1U], sizeof(value));
            --position;
        }
        memcpy(candidates[position], value, sizeof(value));
    }

    for (index = 0U; index < candidate_count; ++index) {
        for (other = 0U; other < candidate_count; ++other) {
            if (index != other && path_is_ancestor(candidates[other], candidates[index])) {
                removed[index] = true;
                break;
            }
        }
    }
    for (index = 0U; index < candidate_count; ++index) {
        if (!removed[index]) ++effective_count;
    }
    if (effective_count == 0U || effective_count > LIBRARY_ROOT_MAX_EFFECTIVE) return false;
    for (index = 0U; index < candidate_count; ++index) {
        if (!removed[index]) {
            size_t length;
            if (!bounded_length(candidates[index], sizeof(candidates[index]), &length)) return false;
            memcpy(out->paths[0], candidates[index], length + 1U);
            out->count = 1U;
            break;
        }
    }
    return true;
}

const library_roots_t *library_roots_default(void)
{
    static const library_roots_t roots = { 1U, { "/" } };
    return &roots;
}

static bool basename_equal(const char *basename, size_t length, const char *expected)
{
    size_t expected_length = 0U;
    size_t index;

    while (expected[expected_length] != '\0') ++expected_length;
    if (length != expected_length) return false;
    for (index = 0U; index < length; ++index) {
        if (ascii_fold((unsigned char)basename[index]) !=
            ascii_fold((unsigned char)expected[index])) return false;
    }
    return true;
}

bool library_basename_is_excluded(bool at_storage_root, const char *basename)
{
    static const char *const root_exclusions[] = {
        "menu.bin", "menu", "N64FlashcartMenu.n64", "ED64", "ED64P",
        "sc64menu.n64", "System Volume Information", ".fseventsd",
        ".Spotlight-V100", ".Trashes", ".VolumeIcon.icns",
        ".metadata_never_index"
    };
    static const char *const all_exclusions[] = {
        "desktop.ini", "Thumbs.db", ".DS_Store"
    };
    size_t length;
    size_t index;

    if (!bounded_length(basename, LIBRARY_FS_BASENAME_CAPACITY, &length) || length == 0U) return true;
    for (index = 0U; index < length; ++index) {
        if (basename[index] == '/') return true;
    }
    if (length > 2U && basename[0] == '.' && basename[1] == '_') return true;
    for (index = 0U; index < sizeof(all_exclusions) / sizeof(all_exclusions[0]); ++index) {
        if (basename_equal(basename, length, all_exclusions[index])) return true;
    }
    if (at_storage_root) {
        for (index = 0U; index < sizeof(root_exclusions) / sizeof(root_exclusions[0]); ++index) {
            if (basename_equal(basename, length, root_exclusions[index])) return true;
        }
    }
    return false;
}
