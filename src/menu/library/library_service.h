#ifndef LIBRARY_SERVICE_H__
#define LIBRARY_SERVICE_H__

#include "menu/library/library_fs.h"
#include "menu/library/library_roots.h"
#include "menu/library/library_scanner.h"
#include "menu/library/library_snapshot.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct library_service library_service_t;

typedef enum {
    MENU_MODE_NONE,
    MENU_MODE_STARTUP,
    MENU_MODE_HOME,
    MENU_MODE_STATIC_LIBRARY,
    MENU_MODE_BROWSER,
    MENU_MODE_FILE_INFO,
    MENU_MODE_SYSTEM_INFO,
    MENU_MODE_IMAGE_VIEWER,
    MENU_MODE_TEXT_VIEWER,
    MENU_MODE_MUSIC_PLAYER,
    MENU_MODE_CREDITS,
    MENU_MODE_SETTINGS_EDITOR,
    MENU_MODE_RTC,
    MENU_MODE_CONTROLLER_PAKFS,
    MENU_MODE_CONTROLLER_PAK_DUMP_INFO,
    MENU_MODE_CONTROLLER_PAK_DUMP_NOTE_INFO,
    MENU_MODE_FLASHCART,
    MENU_MODE_LOAD_ROM,
    MENU_MODE_LOAD_DISK,
    MENU_MODE_LOAD_EMULATOR,
    MENU_MODE_ERROR,
    MENU_MODE_FAULT,
    MENU_MODE_BOOT,
    MENU_MODE_FAVORITE,
    MENU_MODE_HISTORY,
    MENU_MODE_DATEL_CODE_EDITOR,
    MENU_MODE_EXTRACT_FILE
} menu_mode_t;

typedef struct {
    library_fs_t *fs;
    const library_roots_t *roots;
    library_scan_budget_t budget;
    const library_allocator_t *allocator;
    const char *storage_prefix;
} library_service_config_t;

/* Configuration is copied, but discovery is not started until the first safe poll. */
bool library_service_init(library_service_t **out,
                          const library_service_config_t *config);
void library_service_poll(library_service_t *service, menu_mode_t mode);
void library_service_request_pause(library_service_t *service);
void library_service_resume(library_service_t *service);
void library_service_request_cancel(library_service_t *service);
void library_service_restart(library_service_t *service);
bool library_service_is_quiesced(const library_service_t *service);

const library_snapshot_t *library_service_snapshot_acquire(
    library_service_t *service);
void library_service_snapshot_release(const library_snapshot_t *snapshot);

/* Captures and defers exits from a safe mode until pause has quiesced. */
bool library_service_coordinate_transition(library_service_t *service,
                                           menu_mode_t current,
                                           menu_mode_t *requested);

/* Exact bytes requested for the opaque service object itself. */
size_t library_service_allocation_size(const library_service_t *service);

/* Fail closed: a nonquiesced service is not freed. */
void library_service_free(library_service_t *service);

#endif
