/**
 * @file menu_state.h
 * @brief Menu State
 * @ingroup menu 
 */

#ifndef MENU_STRUCT_H__
#define MENU_STRUCT_H__


#include <miniz.h>
#include <miniz_zip.h>
#include <time.h>

#include "boot/boot.h"
#include "disk_info.h"
#include "flashcart/flashcart.h"
#include "path.h"
#include "rom_info.h"
#include "settings.h"
#include "bookkeeping.h"
#include "library/library_service.h"

/** @brief File entry type enumeration */
typedef enum {
    ENTRY_TYPE_DIR,
    ENTRY_TYPE_DISK,
    ENTRY_TYPE_EMULATOR,  
    ENTRY_TYPE_IMAGE,
    ENTRY_TYPE_MUSIC,
    ENTRY_TYPE_OTHER,
    ENTRY_TYPE_ROM,
    ENTRY_TYPE_ROM_CHEAT,
    ENTRY_TYPE_ROM_PATCH,
    ENTRY_TYPE_SAVE,
    ENTRY_TYPE_TEXT,
    ENTRY_TYPE_ARCHIVE,
    ENTRY_TYPE_ARCHIVED,
    ENTRY_TYPE_ROM_META
} entry_type_t;

/** @brief File Entry Structure */
typedef struct {
    char *name;
    entry_type_t type;
    int64_t size;
    int32_t index;
} entry_t;

typedef struct {
    path_t *disk_path;
    disk_info_t disk_info;
} disk_slot_entry_t;

/** @brief Disk slot structure for multi-disk 64DD games. */
typedef struct {
    disk_slot_entry_t primary; // Primary disk slot
    disk_slot_entry_t swap_slot[3]; // 3 swap slots
} disk_slot_t;

/** @brief Menu Structure */
typedef struct {
    menu_mode_t mode;
    menu_mode_t next_mode;

    const char *storage_prefix;
    settings_t settings;
    bookkeeping_t bookkeeping;
    boot_params_t *boot_params;
    library_service_t *library_service;

    char *error_message;
    flashcart_err_t flashcart_err;

    time_t current_time;

    struct {
        bool go_up;
        bool go_down;
        bool go_left;
        bool go_right;
        bool go_fast;

        bool enter;
        bool back;
        bool options;
        bool settings;
        bool lz_context;
    } actions;

    struct {
        int32_t selected;
    } home;

    struct {
        rom_fingerprint_t selected_fingerprint;
        rom_fingerprint_t pending_fingerprint;
        uint32_t last_resolved_index;
        uint32_t visual_offset;
        uint32_t observed_generation;
        menu_mode_t pending_destination;
        uint8_t transition;
        uint8_t message;
        bool selected_valid;
    } library_view;

    struct {
        menu_mode_t return_mode;
    } credits;

    struct {
        bool valid;
        bool reload;
        bool archive;
        mz_zip_archive zip;
        path_t *directory;
        entry_t *list;
        int32_t entries;
        entry_t *entry;
        int32_t selected;
        path_t* select_file;
    } browser;

    struct {
        path_t *rom_path;
        path_t *pending_rom_path;
        bool pending_rom_path_set;
        menu_mode_t pending_return_mode;
        menu_mode_t return_mode;
        bool resume_from_datel;
        rom_info_t rom_info;
        disk_slot_t disk_slots;
        int32_t load_history_id;
        int32_t load_favorite_id;
        bool combined_disk_rom;
    } load;

    struct {
        bool rom_file;
        bool disk_file;
        bool emulator_file;
        bool extract_file;
    } load_pending;
} menu_t;


#endif
