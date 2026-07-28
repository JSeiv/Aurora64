#include <libdragon.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "menu/library/library_fs.h"
#include "menu/library/library_roots.h"

typedef struct {
    bool test_z64;
    bool games;
    bool system_volume;
    bool aurora64;
    bool nested_v64;
    bool ignored_txt;
    bool internal_z64;
    unsigned root_entries;
    unsigned games_entries;
    unsigned repeated_cycles;
} smoke_observed_t;

static unsigned failures;

static void check(bool condition, const char *name)
{
    debugf("P3FS check=%s result=%s\n", name, condition ? "PASS" : "FAIL");
    if (!condition) ++failures;
}

static bool enumerate(library_fs_t *fs, const char *path, bool root,
                      smoke_observed_t *seen)
{
    library_dirent_t entry;
    void *handle = NULL;
    int result = fs->dir_open(fs->context, path, &handle, &entry);
    if (result != LIBRARY_FS_ENTRY || handle == NULL) return false;

    for (;;) {
        bool excluded = library_basename_is_excluded(root, entry.basename);
        debugf("P3FS entry path=%s name=%s type=%u size=%llu excluded=%u\n",
               path, entry.basename, (unsigned)entry.type,
               (unsigned long long)entry.size, (unsigned)excluded);
        if (root) {
            ++seen->root_entries;
            if (strcmp(entry.basename, "test.z64") == 0) seen->test_z64 = true;
            if (strcmp(entry.basename, "Games") == 0) seen->games = true;
            if (strcmp(entry.basename, "System Volume Information") == 0) {
                seen->system_volume = excluded;
            }
            if (strcmp(entry.basename, "aurora64") == 0) seen->aurora64 = !excluded;
        } else if (strcmp(path, "/Games") == 0) {
            ++seen->games_entries;
            if (strcmp(entry.basename, "nested.v64") == 0) seen->nested_v64 = true;
            if (strcmp(entry.basename, "ignored.txt") == 0) seen->ignored_txt = true;
        } else if (strcmp(path, "/aurora64") == 0 &&
                   strcmp(entry.basename, "internal.z64") == 0) {
            seen->internal_z64 = true;
        }

        result = fs->dir_next(fs->context, handle, &entry);
        if (result == LIBRARY_FS_EOF) return true;
        if (result != LIBRARY_FS_ENTRY) {
            (void)fs->dir_close(fs->context, handle);
            return false;
        }
    }
}

static bool verify_file(library_fs_t *fs, const char *path,
                        const uint8_t expected_magic[4])
{
    library_stat_t stat_value;
    uint8_t header[4] = {0};
    void *handle = NULL;
    int64_t amount;
    bool ok = fs->stat(fs->context, path, &stat_value) == LIBRARY_FS_ENTRY &&
              stat_value.type == LIBRARY_FS_ENTRY_FILE && stat_value.size == 4096U;
    if (fs->file_open_read(fs->context, path, &handle) != LIBRARY_FS_ENTRY) return false;
    amount = fs->file_read(fs->context, handle, header, sizeof(header));
    ok = ok && amount == 4 && memcmp(header, expected_magic, 4) == 0;
    ok = ok && fs->file_close(fs->context, handle) == LIBRARY_FS_ENTRY;
    ok = ok && fs->file_read(fs->context, handle, header, 1U) == -1;
    return ok;
}

static bool repeat_directory_cycles(library_fs_t *fs, unsigned count,
                                    smoke_observed_t *seen)
{
    unsigned cycle;

    for (cycle = 0U; cycle < count; ++cycle) {
        library_dirent_t entry;
        void *handle = NULL;
        unsigned entries = 1U;
        int result = fs->dir_open(fs->context, "/Games", &handle, &entry);

        if (result != LIBRARY_FS_ENTRY || handle == NULL) return false;
        while ((result = fs->dir_next(fs->context, handle, &entry)) ==
               LIBRARY_FS_ENTRY) {
            if (++entries > 8U) {
                (void)fs->dir_close(fs->context, handle);
                return false;
            }
        }
        if (result != LIBRARY_FS_EOF || entries != 2U) return false;

        handle = NULL;
        if (fs->dir_open(fs->context, "/Games", &handle, &entry) !=
                LIBRARY_FS_ENTRY ||
            handle == NULL ||
            fs->dir_close(fs->context, handle) != LIBRARY_FS_ENTRY) {
            return false;
        }
        ++seen->repeated_cycles;
    }
    return true;
}

int main(void)
{
    static const uint8_t z64_magic[4] = {0x80, 0x37, 0x12, 0x40};
    static const uint8_t v64_magic[4] = {0x37, 0x80, 0x40, 0x12};
    smoke_observed_t seen = {0};
    library_fs_t fs = {0};
    const library_roots_t *roots;
    void *early = NULL;
    library_dirent_t first;
    bool usb = debug_init_usblog();
    bool sd = debug_init_sdfs("sd:/", -1);

    timer_init();
    debugf("P3FS begin usb=%u sd=%u\n", (unsigned)usb, (unsigned)sd);
    check(sd, "sdfs-mount");
    roots = library_roots_default();
    check(roots != NULL && roots->count == 1U && strcmp(roots->paths[0], "/") == 0,
          "default-logical-root");
    check(library_fs_libdragon_init(&fs, "sd:/"), "adapter-init");

    if (sd && fs.context != NULL) {
        check(enumerate(&fs, "/", true, &seen), "enumerate-root-to-eof");
        check(enumerate(&fs, "/Games", false, &seen), "enumerate-games-to-eof");
        check(enumerate(&fs, "/aurora64", false, &seen), "enumerate-aurora64-to-eof");
        check(seen.test_z64 && seen.games && seen.system_volume && seen.aurora64,
              "root-fixture-and-policy");
        check(seen.nested_v64 && seen.ignored_txt, "games-fixture");
        check(seen.internal_z64, "aurora64-fixture");
        check(library_basename_is_excluded(true, "System Volume Information"),
              "root-system-volume-excluded");
        check(!library_basename_is_excluded(false, "System Volume Information"),
              "nested-system-volume-not-root-rule");
        check(verify_file(&fs, "/test.z64", z64_magic), "read-test-z64");
        check(verify_file(&fs, "/Games/nested.v64", v64_magic), "read-nested-v64");
        check(verify_file(&fs, "/System Volume Information/excluded.z64", z64_magic),
              "adapter-can-read-policy-excluded-file");
        check(verify_file(&fs, "/aurora64/internal.z64", z64_magic),
              "read-internal-z64");
        check(fs.ticks_now(fs.context) != 0U, "ticks-now");

        check(fs.dir_open(fs.context, "/Games", &early, &first) == LIBRARY_FS_ENTRY,
              "early-open");
        check(fs.dir_close(fs.context, early) == LIBRARY_FS_ENTRY, "early-close");
        check(fs.dir_close(fs.context, early) == LIBRARY_FS_EOF, "repeat-close-noop");
        check(repeat_directory_cycles(&fs, 32U, &seen),
              "32-eof-and-early-close-cycles");
    }

    library_fs_libdragon_deinit(&fs);
    if (sd) {
        FILE *evidence = fopen("sd:/aurora64/fs-smoke-result.txt", "wb");
        if (evidence != NULL) {
            fprintf(evidence,
                    "P3FS summary result=%s failures=%u root_entries=%u games_entries=%u cycles=%u\n",
                    failures == 0U ? "PASS" : "FAIL", failures,
                    seen.root_entries, seen.games_entries, seen.repeated_cycles);
            fclose(evidence);
        }
        debug_close_sdfs();
    }
    debugf("P3FS summary result=%s failures=%u root_entries=%u games_entries=%u cycles=%u\n",
           failures == 0U ? "PASS" : "FAIL", failures,
           seen.root_entries, seen.games_entries, seen.repeated_cycles);
    while (1) {
        debugf("P3FS summary result=%s failures=%u root_entries=%u games_entries=%u cycles=%u\n",
               failures == 0U ? "PASS" : "FAIL", failures,
               seen.root_entries, seen.games_entries, seen.repeated_cycles);
        wait_ms(1000);
    }
}
