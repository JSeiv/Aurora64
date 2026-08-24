/**
 * @file menu.c
 * @brief Menu system implementation
 * @ingroup menu
 */

#ifdef MENU_TRANSITION_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#endif
#include "menu_state.h"
#include "library/library_service.h"
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif
#else
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <libdragon.h>

#include "actions.h"
#include "boot/boot.h"
#include "flashcart/flashcart.h"
#include "fonts.h"
#include "hdmi.h"
#include "menu_state.h"
#include "menu.h"
#include "library/library_service.h"
#include "mp3_player.h"
#include "png_decoder.h"
#include "settings.h"
#include "sound.h"
#include "usb_comm.h"
#include "utils/fs.h"
#include "views/views.h"
#endif


typedef enum {
    MENU_LIBRARY_ACTION_NONE = 0,
    MENU_LIBRARY_ACTION_ROM,
    MENU_LIBRARY_ACTION_DISK,
    MENU_LIBRARY_ACTION_EMULATOR,
    MENU_LIBRARY_ACTION_USB_REBOOT,
} menu_library_action_t;

typedef struct {
    menu_t *owner;
    menu_mode_t details_destination;
    menu_library_action_t action;
    bool details_pending;
    bool details_released;
    bool handoff_started;
    bool cancelled_library_launch;
} menu_library_transition_t;

static menu_library_transition_t menu_library_transition;

void menu_library_transition_reset(menu_t *owner)
{
    menu_library_transition.owner = owner;
    menu_library_transition.details_destination = MENU_MODE_NONE;
    menu_library_transition.action = MENU_LIBRARY_ACTION_NONE;
    menu_library_transition.details_pending = false;
    menu_library_transition.details_released = false;
    menu_library_transition.handoff_started = false;
    menu_library_transition.cancelled_library_launch = false;
}

static void menu_library_bind(menu_t *owner)
{
    if (menu_library_transition.owner != owner)
        menu_library_transition_reset(owner);
}

static bool menu_library_action_begin(menu_t *owner,
                                      menu_library_action_t action)
{
    menu_library_bind(owner);
    if (owner == NULL) return false;
    if (menu_library_transition.action != MENU_LIBRARY_ACTION_NONE &&
        menu_library_transition.action != action) return false;
    if (menu_library_transition.action == MENU_LIBRARY_ACTION_NONE) {
        menu_library_transition.action = action;
        if (owner->library_service != NULL)
            library_service_request_cancel(owner->library_service);
    }
    return true;
}

static bool menu_library_action_ready(menu_t *owner,
                                      menu_library_action_t action)
{
    if (!menu_library_action_begin(owner, action)) return false;
    return owner->library_service == NULL ||
        library_service_is_quiesced(owner->library_service);
}

bool menu_library_rom_cancel_started(menu_t *owner)
{
    menu_library_bind(owner);
    return owner != NULL &&
        menu_library_transition.action == MENU_LIBRARY_ACTION_ROM;
}

void menu_library_rom_cancel(menu_t *owner)
{
    if (menu_library_action_begin(owner, MENU_LIBRARY_ACTION_ROM) &&
        owner->load.return_mode == MENU_MODE_LIBRARY)
        menu_library_transition.cancelled_library_launch = true;
}

bool menu_library_rom_ready(menu_t *owner)
{
    menu_library_bind(owner);
    return owner != NULL &&
        menu_library_transition.action == MENU_LIBRARY_ACTION_ROM &&
        (owner->library_service == NULL ||
         library_service_is_quiesced(owner->library_service));
}

bool menu_library_disk_ready(menu_t *owner)
{
    return menu_library_action_ready(owner, MENU_LIBRARY_ACTION_DISK);
}

bool menu_library_emulator_ready(menu_t *owner)
{
    return menu_library_action_ready(owner, MENU_LIBRARY_ACTION_EMULATOR);
}

bool menu_library_usb_reboot_ready(menu_t *owner)
{
    return menu_library_action_ready(owner, MENU_LIBRARY_ACTION_USB_REBOOT);
}

void menu_library_handoff_begin(menu_t *owner)
{
    menu_library_bind(owner);
    menu_library_transition.handoff_started = true;
}

bool menu_library_handoff_started(menu_t *owner)
{
    menu_library_bind(owner);
    return owner != NULL && menu_library_transition.handoff_started;
}

void menu_library_action_failed(menu_t *owner)
{
    menu_library_bind(owner);
    menu_library_transition.action = MENU_LIBRARY_ACTION_NONE;
    menu_library_transition.handoff_started = false;
}

bool menu_library_pause_to(menu_t *owner, menu_mode_t destination)
{
    menu_library_bind(owner);
    if (owner == NULL) return false;
    if (menu_library_transition.details_released &&
        menu_library_transition.details_destination == destination) return true;
    if (!menu_library_transition.details_pending) {
        menu_library_transition.details_destination = destination;
        menu_library_transition.details_pending = true;
        if (owner->library_service != NULL)
            library_service_request_pause(owner->library_service);
    }
    owner->next_mode = owner->mode;
    if (owner->library_service != NULL &&
        !library_service_is_quiesced(owner->library_service)) return false;
    menu_library_transition.details_pending = false;
    menu_library_transition.details_released = true;
    owner->next_mode = menu_library_transition.details_destination;
    if (owner->library_service != NULL &&
        (owner->next_mode == MENU_MODE_HOME ||
         owner->next_mode == MENU_MODE_LIBRARY))
        library_service_resume(owner->library_service);
    return true;
}

void menu_library_coordinate_frame(menu_t *owner)
{
    menu_library_bind(owner);
    if (owner == NULL) return;
    if (menu_library_transition.handoff_started) return;
    if (owner->mode == MENU_MODE_ERROR &&
        owner->next_mode == MENU_MODE_LIBRARY &&
        menu_library_transition.cancelled_library_launch) {
        if (owner->library_service != NULL)
            library_service_restart(owner->library_service);
        menu_library_transition.cancelled_library_launch = false;
        return;
    }
    if (menu_library_transition.details_released) {
        if (owner->mode == MENU_MODE_LOAD_ROM &&
            owner->next_mode == menu_library_transition.details_destination)
            return;
        menu_library_transition.details_released = false;
    }
    if (menu_library_transition.details_pending) {
        (void)menu_library_pause_to(
            owner, menu_library_transition.details_destination);
        return;
    }
#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
    menu_mode_t metrics_current_mode = owner->mode;
    menu_mode_t metrics_requested_mode = owner->next_mode;
    uint32_t metrics_transition_id = 0U;
    uint32_t metrics_generation = 0U;
    bool metrics_trace_active = library_metrics_trace_context(
        &metrics_transition_id, &metrics_generation);
    bool transition_allowed;
#endif
#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
    transition_allowed =
#else
    (void)
#endif
        library_service_coordinate_transition(
            owner->library_service, owner->mode, &owner->next_mode);
#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
    if (metrics_trace_active &&
        metrics_current_mode == MENU_MODE_HOME &&
        metrics_requested_mode == MENU_MODE_LIBRARY) {
        bool immediate = owner->library_service == NULL &&
                         transition_allowed &&
                         owner->next_mode == MENU_MODE_LIBRARY;
        bool held = owner->library_service != NULL &&
                    !transition_allowed &&
                    owner->next_mode == metrics_current_mode;
        if (immediate || held) {
            (void)library_metrics_trace_accept_transition(
                metrics_transition_id, metrics_generation, held);
        }
    } else if (metrics_trace_active &&
               metrics_current_mode == MENU_MODE_HOME &&
               metrics_requested_mode == metrics_current_mode &&
               owner->library_service != NULL &&
               transition_allowed &&
               owner->next_mode == MENU_MODE_LIBRARY) {
        (void)library_metrics_trace_record(
            LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_QUIESCED,
            metrics_transition_id, metrics_generation);
    }
#endif
}

bool menu_library_teardown(menu_t *owner, size_t poll_limit)
{
    size_t poll_count;
    menu_library_bind(owner);
    if (owner == NULL || owner->library_service == NULL) return true;
    library_service_request_cancel(owner->library_service);
    for (poll_count = 0U; poll_count < poll_limit &&
         !library_service_is_quiesced(owner->library_service); ++poll_count)
        library_service_poll(owner->library_service, MENU_MODE_BOOT);
    if (!library_service_is_quiesced(owner->library_service)) return false;
    library_service_free(owner->library_service);
    owner->library_service = NULL;
    return true;
}

#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
#ifdef MENU_TRANSITION_HOST_TEST
void usb_comm_poll(menu_t *owner);
#define MENU_LIBRARY_USB_POSTPOLL_STORAGE
#else
#define MENU_LIBRARY_USB_POSTPOLL_STORAGE static
#endif

MENU_LIBRARY_USB_POSTPOLL_STORAGE void menu_library_poll_usb_and_emit(
    menu_t *owner,
    library_metrics_writer_t writer,
    void *writer_context)
{
    usb_comm_poll(owner);
    library_metrics_record_usb_opportunity(library_metrics_ticks_now());
    (void)library_metrics_emit_pending(writer, writer_context);
}

#undef MENU_LIBRARY_USB_POSTPOLL_STORAGE
#endif

#ifndef MENU_TRANSITION_HOST_TEST

#define MENU_DIRECTORY              "/menu"
#define MENU_SETTINGS_FILE          "config.ini"
#define MENU_CUSTOM_FONT_FILE       "custom.font64"
#define MENU_ROM_LOAD_HISTORY_FILE  "history.ini"

#define MENU_CACHE_DIRECTORY        "cache"
#define BACKGROUND_CACHE_FILE       "background.data"

#define FPS_LIMIT                   (30.0f)
#define LIBRARY_DRAIN_GUARD         20000U

#ifndef FEATURE_AURORA_LIBRARY_TIMING_ENABLED
#define FEATURE_AURORA_LIBRARY_TIMING_ENABLED 0
#endif

#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
static int library_metrics_debug_writer(void *context, const char *bytes,
                                        size_t length)
{
    (void)context;
    if (bytes == NULL || length == 0U ||
        length >= LIBRARY_METRICS_TERMINAL_RECORD_BYTES) return -1;
    debugf("%.*s", (int)length, bytes);
    return (int)length;
}
#endif

static menu_t *menu;

static bool interlaced = true;

void usb_comm_transition_reset(void);

/**
 * @brief Initialize the menu system.
 * 
 * @param boot_params Pointer to the boot parameters structure.
 */
static void menu_init (boot_params_t *boot_params) {    
    menu = calloc(1, sizeof(menu_t));
    assert(menu != NULL);
    menu_library_transition_reset(menu);
    usb_comm_transition_reset();

    menu->boot_params = boot_params;

    menu->mode = MENU_MODE_NONE;
    menu->next_mode = MENU_MODE_STARTUP;

    menu->flashcart_err = flashcart_init(&menu->storage_prefix);
    if (menu->flashcart_err != FLASHCART_OK) {
        menu->next_mode = MENU_MODE_FAULT;
    }

    joypad_init();
    timer_init();
    rtc_init();
    rspq_init();
    rdpq_init();
    dfs_init(DFS_DEFAULT_LOCATION);

    actions_init();
    sound_init_default();
    sound_init_sfx();

    hdmi_clear_game_id();

    path_t *path = path_init(menu->storage_prefix, MENU_DIRECTORY);

    directory_create(path_get(path));

    path_push(path, MENU_SETTINGS_FILE);
    settings_init(path_get(path));
    settings_load(&menu->settings);
    path_pop(path);

    path_push(path, MENU_ROM_LOAD_HISTORY_FILE);
    bookkeeping_init(path_get(path));
    bookkeeping_load(&menu->bookkeeping);
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    path_pop(path);

    library_service_config_t library_config = {
        .fs = NULL,
        .roots = library_roots_default(),
        .budget = {
            .max_directory_entries = 8U,
            .max_read_bytes = 4096U,
            .max_ticks = TICKS_FROM_MS(10U),
        },
        .allocator = NULL,
        .storage_prefix = menu->storage_prefix,
    };
    if (menu->flashcart_err == FLASHCART_OK &&
        !library_service_init(&menu->library_service, &library_config)) {
        menu->next_mode = MENU_MODE_FAULT;
    }

    // Force interlacing off
    interlaced = !menu->settings.force_progressive_scan;

    resolution_t resolution = {
        .width = 640,
        .height = 480,
        .interlaced = interlaced ? INTERLACE_HALF : INTERLACE_OFF,
    };

    display_init(resolution, DEPTH_16_BPP, 2, GAMMA_NONE, interlaced ? FILTERS_DISABLED : FILTERS_RESAMPLE);
    
    if (menu->settings.pal60_enabled) { // it is not given that hardware VI mods understand the output
        tv_type_t tv_type = get_tv_type();
        if (tv_type == TV_PAL) {
            // Set VI timing so it will use 60Hz signal.
            vi_set_timing_preset(&VI_TIMING_PAL60);

            // FIXME: timeout and restore to PAL 50Hz if not shown, 
            // this should be added as a button confirm, or reset combo, rather than re-setting via manual edit of the INI?.
            //vi_set_timing_preset(&VI_TIMING_PAL);
        }
    }
    
    display_set_fps_limit(FPS_LIMIT);

    path_push(path, MENU_CUSTOM_FONT_FILE);
    fonts_init(path_get(path));
    path_pop(path);

    path_push(path, MENU_CACHE_DIRECTORY);
    directory_create(path_get(path));

    path_push(path, BACKGROUND_CACHE_FILE);
    ui_components_background_init(path_get(path));

    path_free(path);

    sound_use_sfx(menu->settings.soundfx_enabled);

    menu->browser.directory = path_init(menu->storage_prefix, menu->settings.default_directory);
    if (!directory_exists(path_get(menu->browser.directory))) {
        path_free(menu->browser.directory);
        menu->browser.directory = path_init(menu->storage_prefix, "/");
    }

    debugf("N64FlashcartMenu debugging...\n");
}

/**
 * @brief Deinitialize the menu system.
 * 
 * @param menu Pointer to the menu structure.
 */
static bool menu_deinit (menu_t *menu) {
    if (!menu_library_teardown(menu, LIBRARY_DRAIN_GUARD)) return false;

    ui_components_background_free();
    rspq_wait();  // Execute deferred callbacks (e.g., display list freeing) before closing RSPQ

    hdmi_send_game_id(menu->boot_params);

    rom_info_free_meta(&menu->load.rom_info);
    path_free(menu->load.rom_path);
    menu->load.rom_path = NULL;
    path_free(menu->load.pending_rom_path);
    menu->load.pending_rom_path = NULL;
    menu->load.pending_rom_path_set = false;
    menu->load_pending.rom_file = false;

    path_free(menu->load.disk_slots.primary.disk_path);
    for (int i = 0; i < menu->browser.entries; i++) {
        free(menu->browser.list[i].name);
    }
    free(menu->browser.list);
    path_free(menu->browser.directory);
    free(menu);

    display_close();

    sound_deinit();
    
    rspq_wait();  // Execute deferred callbacks before closing RSPQ
    rspq_close();
    rdpq_close();
    rtc_close();
    timer_close();
    joypad_close();

    flashcart_deinit();
    return true;
}

/**
 * @brief View structure containing initialization and display functions.
 */
typedef const struct {
    menu_mode_t id; /**< View ID */
    void (*init) (menu_t *menu); /**< Initialization function */
    void (*show) (menu_t *menu, surface_t *display); /**< Display function */
} view_t;

static view_t menu_views[] = {
    { MENU_MODE_STARTUP, view_startup_init, view_startup_display },
    { MENU_MODE_HOME, view_home_init, view_home_display },
    { MENU_MODE_LIBRARY, view_all_games_init, view_all_games_display },
    { MENU_MODE_BROWSER, view_browser_init, view_browser_display },
    { MENU_MODE_FILE_INFO, view_file_info_init, view_file_info_display },
    { MENU_MODE_SYSTEM_INFO, view_system_info_init, view_system_info_display },
    { MENU_MODE_IMAGE_VIEWER, view_image_viewer_init, view_image_viewer_display },
    { MENU_MODE_TEXT_VIEWER, view_text_viewer_init, view_text_viewer_display },
    { MENU_MODE_MUSIC_PLAYER, view_music_player_init, view_music_player_display },
    { MENU_MODE_CREDITS, view_credits_init, view_credits_display },
    { MENU_MODE_SETTINGS_EDITOR, view_settings_init, view_settings_display },
    { MENU_MODE_RTC, view_rtc_init, view_rtc_display },
    { MENU_MODE_CONTROLLER_PAKFS, view_controller_pakfs_init, view_controller_pakfs_display },
    { MENU_MODE_CONTROLLER_PAK_DUMP_INFO, view_controller_pak_dump_info_init, view_controller_pak_dump_info_display },
    { MENU_MODE_CONTROLLER_PAK_DUMP_NOTE_INFO, view_controller_pak_note_dump_info_init, view_controller_pak_note_dump_info_display },
    { MENU_MODE_FLASHCART, view_flashcart_info_init, view_flashcart_info_display },
    { MENU_MODE_LOAD_ROM, view_load_rom_init, view_load_rom_display },
    { MENU_MODE_LOAD_DISK, view_load_disk_init, view_load_disk_display },
    { MENU_MODE_LOAD_EMULATOR, view_load_emulator_init, view_load_emulator_display },
    { MENU_MODE_ERROR, view_error_init, view_error_display },
    { MENU_MODE_FAULT, view_fault_init, view_fault_display },
    { MENU_MODE_FAVORITE, view_favorite_init, view_favorite_display },
    { MENU_MODE_HISTORY, view_history_init, view_history_display },
    { MENU_MODE_DATEL_CODE_EDITOR, view_datel_code_editor_init, view_datel_code_editor_display },
    { MENU_MODE_EXTRACT_FILE, view_extract_file_init, view_extract_file_display }
};

/**
 * @brief Get the view structure for the specified menu mode.
 * 
 * @param id The menu mode ID.
 * @return view_t* Pointer to the view structure.
 */
static view_t *menu_get_view (menu_mode_t id) {
    for (int i = 0; i < sizeof(menu_views) / sizeof(view_t); i++) {
        if (menu_views[i].id == id) {
            return &menu_views[i];
        }
    }
    return NULL;
}

/**
 * @brief Run the menu system.
 * 
 * @param boot_params Pointer to the boot parameters structure.
 */
void menu_run (boot_params_t *boot_params) {
    menu_init(boot_params);

    while (true) {
        if (menu_library_handoff_started(menu) &&
            menu->next_mode == MENU_MODE_BOOT) {
            menu->mode = MENU_MODE_BOOT;
            break;
        }
        surface_t *display = display_try_get();

        if (display != NULL) {
#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
            library_metrics_record_action_opportunity(
                library_metrics_ticks_now());
#endif
            actions_update(menu);

            view_t *view = menu_get_view(menu->mode);
            if (view && view->show) {
                view->show(menu, display);
            } else {
                rdpq_attach_clear(display, NULL);
                rdpq_detach_wait();
                display_show(display);
            }

            if (menu_library_handoff_started(menu)) {
                if (menu->next_mode == MENU_MODE_BOOT) {
                    menu->mode = MENU_MODE_BOOT;
                    break;
                }
                menu_library_action_failed(menu);
            }

            library_service_poll(menu->library_service, menu->mode);
            menu_library_coordinate_frame(menu);

            if (menu->mode == MENU_MODE_BOOT) {
                break;
            }

            while (menu->mode != menu->next_mode) {
                menu->mode = menu->next_mode;

                view_t *next_view = menu_get_view(menu->next_mode);
                if (next_view && next_view->init) {
                    next_view->init(menu);
                }
            }

            time(&menu->current_time);
        }

        sound_poll();

        png_decoder_poll();

#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
        menu_library_poll_usb_and_emit(
            menu, library_metrics_debug_writer, NULL);
#else
        usb_comm_poll(menu);
#endif
    }

    if (!menu_deinit(menu)) abort();

    while (exception_reset_time() > 0) {
        // Do nothing if reset button was pressed
    }
}
#endif
