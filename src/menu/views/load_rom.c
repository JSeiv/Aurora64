#ifdef LOAD_ROM_HOST_TEST
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#endif
#endif
#include "../bookkeeping.h"
#include "../cart_load.h"
#include "../datel_codes.h"
#include "../rom_info.h"
#ifndef LOAD_ROM_HOST_TEST
#include "../sound.h"
#include "boot/boot.h"
#include "utils/fs.h"
#include "views.h"
#else
#include "../menu_state.h"
#include "../library/library_fs.h"
#define debugf(...) ((void)0)
typedef void surface_t;
void menu_show_error(menu_t *menu, char *error_message);
void menu_show_error_context(menu_t *menu, char *error_message,
                             menu_mode_t return_mode,
                             const rom_fingerprint_t *fingerprint,
                             int32_t page_anchor);
#endif
#include "../library/rom_header.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef LOAD_ROM_HOST_TEST
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

#ifndef LOAD_ROM_HOST_TEST
static bool show_extra_info_message = false;
static bool show_advanced_info_message = false;
static component_boxart_t *boxart;
static char *rom_filename = NULL;

static int16_t current_metadata_image_index = 0;
static const file_image_type_t metadata_image_filename_cache[] = {
    IMAGE_BOXART_FRONT,
    IMAGE_BOXART_BACK,
    IMAGE_BOXART_LEFT,
    IMAGE_BOXART_RIGHT,
    IMAGE_BOXART_TOP,
    IMAGE_BOXART_BOTTOM,
    IMAGE_GAMEPAK_FRONT,
    IMAGE_GAMEPAK_BACK
};
static const uint16_t metadata_image_filename_cache_length = sizeof(metadata_image_filename_cache) / sizeof(metadata_image_filename_cache[0]);
static bool metadata_image_available[sizeof(metadata_image_filename_cache) / sizeof(metadata_image_filename_cache[0])] = {false};
static bool metadata_images_scanned = false;

static void show_details_error(menu_t *menu, char *message);

static menu_mode_t validate_return_mode(menu_mode_t return_mode) {
#if FEATURE_AURORA_HOME_ENABLED && FEATURE_AURORA_LIBRARY_ENABLED
    if (return_mode == MENU_MODE_LIBRARY) {
        return MENU_MODE_LIBRARY;
    }
#else
    (void)return_mode;
#endif
    return MENU_MODE_BROWSER;
}

static void scan_metadata_images(menu_t *menu) {
    if (metadata_images_scanned) {
        return;
    }

    path_t *path = path_init(menu->storage_prefix, "menu/metadata"); // should be METADATA_BASE_DIRECTORY
    char game_code_path[50];

    if (menu->load.rom_info.game_code[1] == 'E' && menu->load.rom_info.game_code[2] == 'D') {
        // This is using a homebrew ROM ID, use the title for the file name instead.
        // Create a null-terminated copy of the title for safe string operations
        char safe_title[21];  // 20 chars + null terminator
        memcpy(safe_title, menu->load.rom_info.title, 20);
        safe_title[20] = '\0';
        
        snprintf(game_code_path, sizeof(game_code_path), "homebrew/%s", safe_title); // should be HOMEBREW_ID_SUBDIRECTORY
        path_push(path, game_code_path);
    }
    else {
        snprintf(game_code_path, sizeof(game_code_path), "%c/%c/%c/%c",
            menu->load.rom_info.game_code[0],
            menu->load.rom_info.game_code[1],
            menu->load.rom_info.game_code[2],
            menu->load.rom_info.game_code[3]);
        path_push(path, game_code_path);

        if (!directory_exists(path_get(path))) { // Allow boxart to not specify the region code.
            path_pop(path);
        }
    }

    bool dir_exists = directory_exists(path_get(path));

    if (dir_exists) {
        // Filenames array matches metadata_image_filename_cache order for indexed access
        // Note: This mapping is also present in boxart.c but duplicated here
        // for efficient scanning without calling into the component layer
        char *filenames[] = {
            "boxart_front.png",
            "boxart_back.png",
            "boxart_left.png",
            "boxart_right.png",
            "boxart_top.png",
            "boxart_bottom.png",
            "gamepak_front.png",
            "gamepak_back.png"
        };

        for (uint16_t i = 0; i < metadata_image_filename_cache_length; i++) {
            path_push(path, filenames[i]);
            metadata_image_available[i] = file_exists(path_get(path));
            path_pop(path);
        }
    } else {
        // No directory exists, mark all images as unavailable
        for (uint16_t i = 0; i < metadata_image_filename_cache_length; i++) {
            metadata_image_available[i] = false;
        }
    }

    debugf("Metadata: Scanned metadata for ROM ID %s. \n", game_code_path);

    path_free(path);
    metadata_images_scanned = true;
}

static const char *format_rom_description(menu_t *menu) {
    const char *rom_description = NULL;

    if (menu->load.rom_info.meta.short_description != NULL && strlen(menu->load.rom_info.meta.short_description) > 0) {
        rom_description = menu->load.rom_info.meta.short_description;
    }

    return rom_description ? rom_description : "No description available.";
}

static char *convert_error_message (rom_err_t err) {
    switch (err) {
        case ROM_ERR_LOAD_IO: return "I/O error during loading ROM information and/or options";
        case ROM_ERR_SAVE_IO: return "I/O error during storing ROM options";
        case ROM_ERR_NO_FILE: return "Couldn't open ROM file";
        default: return "Unknown ROM info load error";
    }
}

static const char *format_rom_endianness (rom_endianness_t endianness) {
    switch (endianness) {
        case ENDIANNESS_BIG: return "Big (default)";
        case ENDIANNESS_LITTLE: return "Little (unsupported)";
        case ENDIANNESS_BYTE_SWAP: return "Byte swapped";
        default: return "Unknown";
    }
}

static const char *format_rom_media_type (rom_category_type_t media_type) {
    switch (media_type) {
        case N64_CART: return "Cartridge";
        case N64_DISK: return "Disk";
        case N64_CART_EXPANDABLE: return "Cartridge (Expandable)";
        case N64_DISK_EXPANDABLE: return "Disk (Expandable)";
        case N64_ALECK64: return "Aleck64";
        default: return "Unknown";
    }
}

static const char *format_rom_destination_market (rom_destination_type_t market_type) {
    // TODO: These are all assumptions and should be corrected if required.
    // From http://n64devkit.square7.ch/info/submission/pal/01-01.html
    switch (market_type) {
        case MARKET_JAPANESE_MULTI: return "Japanese & English"; // 1080 Snowboarding JPN
        case MARKET_BRAZILIAN: return "Brazilian (Portuguese)";
        case MARKET_CHINESE: return "Chinese";
        case MARKET_GERMAN: return "German";
        case MARKET_NORTH_AMERICA: return "American English";
        case MARKET_FRENCH: return "French";
        case MARKET_DUTCH: return "Dutch";
        case MARKET_ITALIAN: return "Italian";
        case MARKET_JAPANESE: return "Japanese";
        case MARKET_KOREAN: return "Korean";
        case MARKET_CANADIAN: return "Canadaian (English & French)";
        case MARKET_SPANISH: return "Spanish";
        case MARKET_AUSTRALIAN: return "Australian (English)";
        case MARKET_SCANDINAVIAN: return "Scandinavian";
        case MARKET_GATEWAY64_NTSC: return "LodgeNet/Gateway (NTSC)";
        case MARKET_GATEWAY64_PAL: return "LodgeNet/Gateway (PAL)";
        case MARKET_EUROPEAN_BASIC: return "PAL (includes English)"; // Mostly EU but is used on some Australian ROMs
        case MARKET_OTHER_X: return "Regional (non specific)"; // FIXME: AUS HSV Racing ROM's and Asia Top Gear Rally use this so not only EUR
        case MARKET_OTHER_Y: return "European (non specific)";
        case MARKET_OTHER_Z: return "Regional (unknown)";
        default: return "Unknown";
    }
}

static const char *format_rom_save_type (rom_save_type_t save_type, bool supports_cpak) {
    switch (save_type) {
        case SAVE_TYPE_NONE: return supports_cpak ? "Controller PAK" : "None";
        case SAVE_TYPE_EEPROM_4KBIT: return supports_cpak ?   "EEPROM 4kbit | Controller PAK" : "EEPROM 4kbit";
        case SAVE_TYPE_EEPROM_16KBIT: return supports_cpak ?  "EEPROM 16kbit | Controller PAK" : "EEPROM 16kbit";
        case SAVE_TYPE_SRAM_256KBIT: return supports_cpak ?   "SRAM 256kbit | Controller PAK" : "SRAM 256kbit";
        case SAVE_TYPE_SRAM_BANKED: return supports_cpak ?    "SRAM 768kbit / 3 banks | Controller PAK" : "SRAM 768kbit / 3 banks";
        case SAVE_TYPE_SRAM_1MBIT: return supports_cpak ?     "SRAM 1Mbit | Controller PAK" : "SRAM 1Mbit";
        case SAVE_TYPE_FLASHRAM_1MBIT: return supports_cpak ? "FlashRAM 1Mbit | Controller PAK" : "FlashRAM 1Mbit";
        case SAVE_TYPE_FLASHRAM_PKST2: return supports_cpak ? "FlashRAM (Pokemon Stadium 2) | Controller PAK" : "FlashRAM (Pokemon Stadium 2)";
        default: return "Unknown";
    }
}

static const char *format_rom_tv_type (rom_tv_type_t tv_type) {
    switch (tv_type) {
        case ROM_TV_TYPE_PAL: return "PAL";
        case ROM_TV_TYPE_NTSC: return "NTSC";
        case ROM_TV_TYPE_MPAL: return "MPAL";
        default: return "Unknown";
    }
}

static const char *format_rom_expansion_pak_info (rom_expansion_pak_t expansion_pak_info) {
    switch (expansion_pak_info) {
        case EXPANSION_PAK_REQUIRED: return "Required";
        case EXPANSION_PAK_RECOMMENDED: return "Recommended";
        case EXPANSION_PAK_SUGGESTED: return "Suggested";
        case EXPANSION_PAK_FAULTY: return "May require ROM patch";
        default: return "Not required";
    }
}

static const char *format_rom_pak_feature_info (bool pak_feature_info) {
    if (pak_feature_info) {
        return "Supported";
    } else {
        return "Not used";
    }
}

static const char *format_cic_type (rom_cic_type_t cic_type) {
    switch (cic_type) {
        case ROM_CIC_TYPE_5101: return "5101";
        case ROM_CIC_TYPE_5167: return "5167";
        case ROM_CIC_TYPE_6101: return "6101";
        case ROM_CIC_TYPE_7102: return "7102";
        case ROM_CIC_TYPE_x102: return "6102 / 7101";
        case ROM_CIC_TYPE_x103: return "6103 / 7103";
        case ROM_CIC_TYPE_x105: return "6105 / 7105";
        case ROM_CIC_TYPE_x106: return "6106 / 7106";
        case ROM_CIC_TYPE_8301: return "8301";
        case ROM_CIC_TYPE_8302: return "8302";
        case ROM_CIC_TYPE_8303: return "8303";
        case ROM_CIC_TYPE_8401: return "8401";
        case ROM_CIC_TYPE_8501: return "8501";
        default: return "Unknown";
    }
}

static const char *format_age_rating (uint32_t age_rating) {
    if (age_rating >= 18) {
        return "Adults Only";
    }
    else if (age_rating >= 17) {
        return "Mature";
    }
    else if (age_rating >= 13) {
        return "Teen";
    }
    else if (age_rating >= 10) {
        return "Everyone 10+";
    }
    else if (age_rating > 0) {
        return "Everyone";
    }
    else if (age_rating == 0) {
        return "None";
    }
    else {
        return "Unknown";
    }
}

static inline const char *format_boolean_type (bool bool_value) {
    return bool_value ? "On" : "Off";
}

static void set_cic_type (menu_t *menu, void *arg) {
    rom_cic_type_t cic_type = (rom_cic_type_t) (arg);
    rom_err_t err = rom_config_override_cic_type(menu->load.rom_path, &menu->load.rom_info, cic_type);
    if (err != ROM_OK) {
        show_details_error(menu, convert_error_message(err));
        return;
    }
    menu->browser.reload = true;
}

static void set_save_type (menu_t *menu, void *arg) {
    rom_save_type_t save_type = (rom_save_type_t) (arg);
    rom_err_t err = rom_config_override_save_type(menu->load.rom_path, &menu->load.rom_info, save_type);
    if (err != ROM_OK) {
        show_details_error(menu, convert_error_message(err));
        return;
    }
    menu->browser.reload = true;
}

static void set_tv_type (menu_t *menu, void *arg) {
    rom_tv_type_t tv_type = (rom_tv_type_t) (arg);
    rom_err_t err = rom_config_override_tv_type(menu->load.rom_path, &menu->load.rom_info, tv_type);
    if (err != ROM_OK) {
        show_details_error(menu, convert_error_message(err));
        return;
    }
    menu->browser.reload = true;
}
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
static void set_autoload_type (menu_t *menu, void *arg) {
    (void)arg;

    if (menu->load.rom_path == NULL || path_get(menu->load.rom_path) == NULL || path_get(menu->load.rom_path)[0] == '\0') {
        return;
    }

    char *active_filename = path_last_get(menu->load.rom_path);
    if (active_filename == NULL || active_filename[0] == '\0') {
        return;
    }

    char *autoload_filename = strdup(active_filename);
    if (autoload_filename == NULL) {
        return;
    }

    path_t *autoload_directory = path_clone(menu->load.rom_path);
    if (autoload_directory == NULL) {
        free(autoload_filename);
        return;
    }
    path_pop(autoload_directory);

    char *relative_directory = strip_fs_prefix(path_get(autoload_directory));
    char *autoload_path = relative_directory == NULL ? NULL : strdup(relative_directory);
    path_free(autoload_directory);
    if (autoload_path == NULL) {
        free(autoload_filename);
        return;
    }

    free(menu->settings.rom_autoload_path);
    menu->settings.rom_autoload_path = autoload_path;
    free(menu->settings.rom_autoload_filename);
    menu->settings.rom_autoload_filename = autoload_filename;
    // FIXME: add a confirmation box here! (press start on reboot)
    menu->settings.rom_autoload_enabled = true;
    settings_save(&menu->settings);
    menu->browser.reload = true;
}
#endif

static void set_cheat_option(menu_t *menu, void *arg) {
    debugf("Load Rom: setting cheat option to %d\n", (int)arg);
    if (!is_memory_expanded()) {
        // If the Expansion pak is not installed, we cannot use cheats, and force it to off (just incase).
        rom_config_setting_set_cheats(menu->load.rom_path, &menu->load.rom_info, false);
        menu->browser.reload = true;
    }
    else {
        bool enabled = (bool)arg;
        rom_config_setting_set_cheats(menu->load.rom_path, &menu->load.rom_info, enabled);
        menu->browser.reload = true;
    }
}

#ifdef FEATURE_PATCHER_GUI_ENABLED
static void set_patcher_option(menu_t *menu, void *arg) {
    bool enabled = (bool)arg;
    rom_config_setting_set_patches(menu->load.rom_path, &menu->load.rom_info, enabled);
    menu->browser.reload = true;
}
#endif

static void add_favorite (menu_t *menu, void *arg) {
    bookkeeping_favorite_add(&menu->bookkeeping, menu->load.rom_path, NULL, BOOKKEEPING_TYPE_ROM);
}

static void iterate_metadata_image(menu_t *menu, int direction) {
    scan_metadata_images(menu);

    // Transverse to next/previous available image based on direction (1 = next, -1 = previous)
    int16_t start_metadata_image_index = current_metadata_image_index;
    int16_t new_metadata_image_index = (current_metadata_image_index + direction + metadata_image_filename_cache_length) % metadata_image_filename_cache_length;

    // Find next available image from our cached list
    while (new_metadata_image_index != start_metadata_image_index) {
        if (metadata_image_available[new_metadata_image_index]) {
            // ui_components_boxart_init returns NULL if PNG decoder is busy
            component_boxart_t *new_boxart = ui_components_boxart_init(
                menu->storage_prefix,
                menu->load.rom_info.game_code,
                menu->load.rom_info.title,
                metadata_image_filename_cache[new_metadata_image_index]
            );

            if (new_boxart != NULL) {
                // Only free old boxart after successful new allocation
                ui_components_boxart_free(boxart);
                boxart = new_boxart;
                current_metadata_image_index = new_metadata_image_index;
                sound_play_effect(SFX_SETTING);
                break;
            }
        }
        new_metadata_image_index = (new_metadata_image_index + direction + metadata_image_filename_cache_length) % metadata_image_filename_cache_length;
    }
}

static component_context_menu_t set_cic_type_context_menu = { .list = {
    {.text = "Automatic", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_AUTOMATIC) },
    {.text = "CIC-6101", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_6101) },
    {.text = "CIC-7102", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_7102) },
    {.text = "CIC-6102 / CIC-7101", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x102) },
    {.text = "CIC-6103 / CIC-7103", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x103) },
    {.text = "CIC-6105 / CIC-7105", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x105) },
    {.text = "CIC-6106 / CIC-7106", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x106) },
    {.text = "Aleck64 CIC-5101", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_5101) },
    {.text = "64DD ROM conversion CIC-5167", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_5167) },
    {.text = "NDDJ0 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8301) },
    {.text = "NDDJ1 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8302) },
    {.text = "NDDJ2 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8303) },
    {.text = "NDXJ0 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8401) },
    {.text = "NDDE0 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8501) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_save_type_context_menu = { .list = {
    { .text = "Automatic", .action = set_save_type, .arg = (void *) (SAVE_TYPE_AUTOMATIC) },
    { .text = "None", .action = set_save_type, .arg = (void *) (SAVE_TYPE_NONE) },
    { .text = "EEPROM 4kbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_EEPROM_4KBIT) },
    { .text = "EEPROM 16kbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_EEPROM_16KBIT) },
    { .text = "SRAM 256kbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_SRAM_256KBIT) },
    { .text = "SRAM 768kbit / 3 banks", .action = set_save_type, .arg = (void *) (SAVE_TYPE_SRAM_BANKED) },
    { .text = "SRAM 1Mbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_SRAM_1MBIT) },
    { .text = "FlashRAM 1Mbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_FLASHRAM_1MBIT) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_tv_type_context_menu = { .list = {
    { .text = "Automatic", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_AUTOMATIC) },
    { .text = "PAL", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_PAL) },
    { .text = "NTSC", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_NTSC) },
    { .text = "MPAL", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_MPAL) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_cheat_options_menu = { .list = {
    { .text = "Enable", .action = set_cheat_option, .arg = (void *) (true)},
    { .text = "Disable", .action = set_cheat_option, .arg = (void *) (false)},
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

#ifdef FEATURE_PATCHER_GUI_ENABLED
static component_context_menu_t set_patcher_options_menu = { .list = {
    { .text = "Enable", .action = set_patcher_option, .arg = (void *) (true)},
    { .text = "Disable", .action = set_patcher_option, .arg = (void *) (false)},
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

static void set_menu_next_mode (menu_t *menu, void *arg) {
    menu_mode_t next_mode = (menu_mode_t) (arg);
    if (next_mode == MENU_MODE_DATEL_CODE_EDITOR) {
        menu->load.resume_from_datel = true;
    }
    menu->next_mode = next_mode;
}

static component_context_menu_t options_context_menu = { .list = {
    { .text = "Set CIC Type", .submenu = &set_cic_type_context_menu },
    { .text = "Set Save Type", .submenu = &set_save_type_context_menu },
    { .text = "Set TV Type", .submenu = &set_tv_type_context_menu },
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    { .text = "Set ROM to autoload", .action = set_autoload_type },
#endif
    { .text = "Use Cheats", .submenu = &set_cheat_options_menu },
    { .text = "Datel Code Editor", .action = set_menu_next_mode, .arg = (void *) (MENU_MODE_DATEL_CODE_EDITOR) },
#ifdef FEATURE_PATCHER_GUI_ENABLED
    { .text = "Use Patches", .submenu = &set_patcher_options_menu },
#endif
    { .text = "Add to favorites", .action = add_favorite },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static void process (menu_t *menu) {
    if (ui_components_context_menu_process(menu, &options_context_menu)) {
        return;
    }

    if (menu->actions.enter) {
        menu->load_pending.rom_file = true;
    } else if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = validate_return_mode(menu->load.return_mode);
    } else if (menu->actions.options) {
        ui_components_context_menu_show(&options_context_menu);
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.lz_context) {
        if (show_extra_info_message) {
            show_extra_info_message = false;
        } else {
            show_extra_info_message = true;
        }
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.settings) { // TODO: change to go_right/go_left when those are implemented
        if (show_advanced_info_message) {
            show_advanced_info_message = false;
        } else {
            show_advanced_info_message = true;
        }
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.go_right) {
        iterate_metadata_image(menu, 1);
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_left) {
        iterate_metadata_image(menu, -1);
        sound_play_effect(SFX_CURSOR);
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (menu->load_pending.rom_file && menu->settings.loading_progress_bar_enabled) {
        ui_components_loader_draw(0.0f, NULL);
    } else {
#endif
        ui_components_layout_draw();

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "%s\n"
            "%.20s\n",
            rom_filename,
            menu->load.rom_info.title
        );

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n\n\n\t%.120s\n",
            format_rom_description(menu)
            
        );

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n\n\n\n\n\n\n\n\n\n\n"
            "Save type:\t\t%s\n"
            "TV region:\t\t%s\n"
            "\n"
            "Expansion PAK:\t%s\n"
            "Rumble PAK:\t\t%s\n"
            "Transfer PAK:\t\t%s\n"
            "\n"
            "Datel Cheats:\t\t%s\n"
            "Patches:\t\t\t%s\n"
            ,
            
            format_rom_save_type(rom_info_get_save_type(&menu->load.rom_info), menu->load.rom_info.features.controller_pak),
            format_rom_tv_type(rom_info_get_tv_type(&menu->load.rom_info)),
            format_rom_expansion_pak_info(menu->load.rom_info.features.expansion_pak),
            format_rom_pak_feature_info(menu->load.rom_info.features.rumble_pak),
            format_rom_pak_feature_info(menu->load.rom_info.features.transfer_pak),
            format_boolean_type(menu->load.rom_info.settings.cheats_enabled),
            format_boolean_type(menu->load.rom_info.settings.patches_enabled)
        );

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "A: Load and run ROM\n"
            "%s\n",
            validate_return_mode(menu->load.return_mode) == MENU_MODE_LIBRARY ? "B: Library" : "B: Back"
        );

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "Start: Adv. Info\n"
            "◀ Change game image ▶\n"
        );

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_RIGHT, VALIGN_TOP,
            "L|Z: Extra Info\n"
            "R: Adv. Options\n"
        );

        if (boxart != NULL) {
            ui_components_boxart_draw(boxart);
        }

        if (show_extra_info_message) {
            ui_components_messagebox_draw(
                "EXTRA ROM INFO\n"
                "\n"
                "Title: %.20s\n"
                "Age Rating: %s\n"
                "Release Date: %s\n"
                "Author: %s\n"
                "Website: %s\n"
                "License: %s\n"
                "Game code: %c%c%c%c\n"
                "Media type: %s\n"
                "Variant: %s\n"
                "Version: %hhu\n"
                "CIC: %s\n\n\n"
                "Press L|Z to return.\n",
                menu->load.rom_info.title,
                format_age_rating(menu->load.rom_info.meta.age_rating),
                menu->load.rom_info.meta.release_date,
                menu->load.rom_info.meta.author,
                menu->load.rom_info.meta.website,
                menu->load.rom_info.meta.osi_license,
                menu->load.rom_info.game_code[0], menu->load.rom_info.game_code[1], menu->load.rom_info.game_code[2], menu->load.rom_info.game_code[3],
                format_rom_media_type(menu->load.rom_info.category_code),
                format_rom_destination_market(menu->load.rom_info.destination_code),
                menu->load.rom_info.version,
                format_cic_type(rom_info_get_cic_type(&menu->load.rom_info))
            );
        }

        if (show_advanced_info_message) {
            ui_components_messagebox_draw(
                "ADVANCED ROM INFO\n"
                "\n"
                "Boot address: 0x%08lX\n"
                "SDK version: %.1f%c\n"
                "Clock Rate: %.2fMHz\n"
                "Check code: 0x%016llX\n"
                "Endianness: %s\n\n\n"
                "Press START to return.\n",
                menu->load.rom_info.boot_address,
                (menu->load.rom_info.libultra.version / 10.0f), menu->load.rom_info.libultra.revision,
                menu->load.rom_info.clock_rate,
                menu->load.rom_info.check_code,
                format_rom_endianness(menu->load.rom_info.endianness)
            );
        }

        ui_components_context_menu_draw(&options_context_menu);
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    }
#endif

    rdpq_detach_show();
}

static void draw_progress (float progress) {
    surface_t *d = (progress >= 1.0f) ? display_get() : display_try_get();

    if (d) {
        rdpq_attach(d, NULL);

        ui_components_background_draw();

        ui_components_loader_draw(progress, "Loading ROM...");  

        rdpq_detach_show();
    }
}

#else
static char *rom_filename;
static const library_fs_t *host_validation_fs;
static int (*host_validation_seek)(void *context, void *handle, uint64_t offset);
static library_source_t host_expected_source;
static cart_load_err_t (*host_cart_load)(menu_t *menu);
static void show_details_error(menu_t *menu, char *message);
static void deinit(void);

static menu_mode_t validate_return_mode(menu_mode_t return_mode)
{
    return return_mode == MENU_MODE_LIBRARY ? MENU_MODE_LIBRARY : MENU_MODE_BROWSER;
}

static char *convert_error_message(rom_err_t err)
{
    return err == ROM_ERR_NO_FILE ? "Couldn't open ROM file" :
        "I/O error during loading ROM information and/or options";
}

void view_load_rom_host_set_validation_fs(
    const library_fs_t *fs,
    int (*seek_callback)(void *context, void *handle, uint64_t offset),
    const library_source_t *source)
{
    host_validation_fs = fs;
    host_validation_seek = seek_callback;
    if (source != NULL) host_expected_source = *source;
    else memset(&host_expected_source, 0, sizeof(host_expected_source));
}

void view_load_rom_host_set_cart_load(cart_load_err_t (*callback)(menu_t *menu))
{
    host_cart_load = callback;
}
#endif

static bool validate_library_source(menu_t *menu);

static void load (menu_t *menu) {
    debugf("Load ROM: load function called\n");
    cart_load_err_t err;
    if (!validate_library_source(menu)) {
        show_details_error(menu, "Indexed ROM changed or is no longer available");
        return;
    }
#ifdef LOAD_ROM_HOST_TEST
    err = host_cart_load == NULL ? CART_LOAD_ERR_ROM_LOAD_FAIL : host_cart_load(menu);
#else
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (!menu->settings.loading_progress_bar_enabled) {
        err = cart_load_n64_rom_and_save(menu, NULL);
    } else  {
        err = cart_load_n64_rom_and_save(menu, draw_progress);
    }
#else
    err = cart_load_n64_rom_and_save(menu, draw_progress);
#endif
#endif

    if (err != CART_LOAD_OK) {
        show_details_error(menu, cart_load_convert_error_message(err));
        return;
    }

#ifdef LOAD_ROM_HOST_TEST
    menu->next_mode = MENU_MODE_BOOT;
#else
    bookkeeping_history_add(&menu->bookkeeping, menu->load.rom_path, NULL, BOOKKEEPING_TYPE_ROM);

    menu->next_mode = MENU_MODE_BOOT;

    menu->boot_params->device_type = BOOT_DEVICE_TYPE_ROM;
    menu->boot_params->detect_cic_seed = rom_info_get_cic_seed(&menu->load.rom_info, &menu->boot_params->cic_seed);
    switch (rom_info_get_tv_type(&menu->load.rom_info)) {
        case ROM_TV_TYPE_PAL: menu->boot_params->tv_type = BOOT_TV_TYPE_PAL; break;
        case ROM_TV_TYPE_NTSC: menu->boot_params->tv_type = BOOT_TV_TYPE_NTSC; break;
        case ROM_TV_TYPE_MPAL: menu->boot_params->tv_type = BOOT_TV_TYPE_MPAL; break;
        default: menu->boot_params->tv_type = BOOT_TV_TYPE_PASSTHROUGH; break;
    }

    // Handle cheat codes only if Expansion Pak is present and cheats are enabled
    if (is_memory_expanded() && menu->load.rom_info.settings.cheats_enabled) {
        uint32_t tmp_cheats[MAX_CHEAT_CODE_ARRAYLIST_SIZE];
        size_t cheat_item_count = generate_enabled_cheats_array(get_cheat_codes(), tmp_cheats);

        if (cheat_item_count > 2) { // account for at least one valid cheat code (address and value), excluding the last two 0s
            // Allocate memory for the cheats array
            uint32_t *cheats = malloc(cheat_item_count * sizeof(uint32_t));
            if (cheats) {
                memcpy(cheats, tmp_cheats, cheat_item_count * sizeof(uint32_t));
                for (size_t i = 0; i + 1 < cheat_item_count; i += 2) {
                    debugf("Cheat %u: Address: 0x%08lX, Value: 0x%08lX\n", i / 2, cheats[i], cheats[i + 1]);
                }
                debugf("Cheats enabled, %u cheats found\n", cheat_item_count / 2);
                menu->boot_params->cheat_list = cheats;
            } else {
                debugf("Failed to allocate memory for cheat list\n");
                menu->boot_params->cheat_list = NULL;
            }
        } else {
            debugf("Cheats enabled, but no cheats found\n");
            menu->boot_params->cheat_list = NULL;
        }
    } else {
        debugf("Cheats disabled or Expansion Pak not present\n");
        menu->boot_params->cheat_list = NULL;
    }
#endif
}

#ifndef LOAD_ROM_HOST_TEST
static void deinit (void) {
#ifndef LOAD_ROM_HOST_TEST
    ui_components_boxart_free(boxart);
#endif
    boxart = NULL;
    current_metadata_image_index = 0;
    metadata_images_scanned = false;

    // Clear availability cache
    for (uint16_t i = 0; i < metadata_image_filename_cache_length; i++) {
        metadata_image_available[i] = false;
    }

#ifndef LOAD_ROM_HOST_TEST
    ui_components_context_menu_init(&options_context_menu);
#endif
    show_extra_info_message = false;
    show_advanced_info_message = false;
}
#else
static void deinit(void) { }
#endif

static void release_active_allocations(menu_t *menu) {
    deinit();
    rom_info_free_meta(&menu->load.rom_info);
    memset(&menu->load.rom_info, 0, sizeof(menu->load.rom_info));
    path_free(menu->load.rom_path);
    menu->load.rom_path = NULL;
    rom_filename = NULL;
}

static void clear_active_details(menu_t *menu, bool clear_load_pending) {
    release_active_allocations(menu);
    path_free(menu->load.pending_rom_path);
    menu->load.pending_rom_path = NULL;
    menu->load.pending_rom_path_set = false;
    menu->load.expected_fingerprint_valid = false;
    memset(&menu->load.expected_fingerprint, 0,
           sizeof(menu->load.expected_fingerprint));
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    menu->load.resume_from_datel = false;
    menu->load.return_mode = MENU_MODE_BROWSER;
    menu->load.pending_return_mode = MENU_MODE_BROWSER;
    if (clear_load_pending) {
        menu->load_pending.rom_file = false;
    }
}

static void show_details_error(menu_t *menu, char *message) {
    menu_mode_t return_mode = menu->load.return_mode;
    rom_fingerprint_t fingerprint = menu->library_view.selected_fingerprint;
    bool fingerprint_valid = menu->library_view.selected_valid;
    int32_t page_anchor = (int32_t)menu->library_view.visual_offset;

    clear_active_details(menu, true);
    menu_show_error_context(menu, message, return_mode,
        fingerprint_valid ? &fingerprint : NULL, page_anchor);
}

typedef struct {
    library_source_t source;
} validation_expectation_t;

typedef struct {
    uint64_t first;
    uint64_t last;
} validation_range_t;

#ifndef LOAD_ROM_HOST_TEST
static int validation_file_open(void *context, const char *path, void **handle)
{
    FILE *file;
    (void)context;
    if (handle == NULL) return LIBRARY_FS_ERROR;
    *handle = NULL;
    file = fopen(path, "rb");
    if (file == NULL) return LIBRARY_FS_ERROR;
    *handle = file;
    return LIBRARY_FS_ENTRY;
}

static int64_t validation_file_read(void *context, void *handle, void *buffer,
                                    size_t length)
{
    FILE *file = handle;
    size_t amount;
    (void)context;
    amount = fread(buffer, 1U, length, file);
    if (amount == 0U && ferror(file)) return LIBRARY_FS_ERROR;
    return (int64_t)amount;
}

static int validation_file_close(void *context, void *handle)
{
    (void)context;
    return fclose(handle);
}

static int validation_file_seek(void *context, void *handle, uint64_t offset)
{
    (void)context;
    if (offset > (uint64_t)LONG_MAX) return LIBRARY_FS_ERROR;
    return fseek((FILE *)handle, (long)offset, SEEK_SET) == 0 ?
        LIBRARY_FS_ENTRY : LIBRARY_FS_ERROR;
}

static int validation_stat(void *context, const char *path, library_stat_t *out)
{
    struct stat value;
    (void)context;
    if (out == NULL || stat(path, &value) != 0 || value.st_size < 0)
        return LIBRARY_FS_ERROR;
    memset(out, 0, sizeof(*out));
    out->type = S_ISREG(value.st_mode) ? LIBRARY_FS_ENTRY_FILE :
        LIBRARY_FS_ENTRY_UNKNOWN;
    out->size = (uint64_t)value.st_size;
    out->modified_time = (int64_t)value.st_mtime;
    return LIBRARY_FS_ENTRY;
}

static const library_fs_t production_validation_fs = {
    .context = NULL,
    .file_open_read = validation_file_open,
    .file_read = validation_file_read,
    .file_close = validation_file_close,
    .stat = validation_stat
};
#endif

/* Copy every source-signature field and the path-derived expectation before
 * releasing the snapshot. No snapshot object or snapshot-owned path escapes. */
static bool resolve_validation_expectation(menu_t *menu,
                                           validation_expectation_t *out)
{
    if (out == NULL || menu->load.return_mode != MENU_MODE_LIBRARY ||
        !menu->load.expected_fingerprint_valid || menu->load.rom_path == NULL)
        return false;
#ifdef LOAD_ROM_HOST_TEST
    out->source = host_expected_source;
    return host_validation_fs != NULL && host_validation_seek != NULL;
#else
    const library_snapshot_t *snapshot;
    const library_record_t *record;
    const library_source_t *source;
    const char *logical_path;
    path_t *resolved_path = NULL;
    bool valid = false;

    if (menu->library_service == NULL) return false;
    snapshot = library_service_snapshot_acquire(menu->library_service);
    if (snapshot == NULL) return false;
    record = library_snapshot_find_fingerprint(
        snapshot, &menu->load.expected_fingerprint);
    if (record == NULL || record->source_count == 0U ||
        record->primary_source_index < record->source_first ||
        record->primary_source_index >=
            record->source_first + record->source_count) goto done;
    source = library_snapshot_source_at(snapshot, record->primary_source_index);
    logical_path = library_snapshot_source_path(snapshot,
                                                record->primary_source_index);
    if (source == NULL || logical_path == NULL ||
        !path_try_init(&resolved_path, menu->storage_prefix, logical_path) ||
        strcmp(path_get(resolved_path), path_get(menu->load.rom_path)) != 0)
        goto done;
    out->source = *source;
    valid = true;

done:
    path_free(resolved_path);
    library_service_snapshot_release(snapshot);
    return valid;
#endif
}

static bool validation_stat_expected(const library_stat_t *value,
                                     const library_source_t *expected)
{
    return value->type == LIBRARY_FS_ENTRY_FILE &&
        value->size == expected->size &&
        value->modified_time == expected->mtime_seconds &&
        value->size >= ROM_HEADER_METADATA_BYTES;
}

static bool validation_stat_same(const library_stat_t *left,
                                 const library_stat_t *right)
{
    return left->type == right->type && left->size == right->size &&
        left->modified_time == right->modified_time &&
        left->change_token == right->change_token;
}

static uint32_t validation_crc32_byte(uint32_t crc, uint8_t value)
{
    unsigned int bit;
    crc ^= value;
    for (bit = 0U; bit < 8U; ++bit)
        crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320U : 0U);
    return crc;
}

static uint32_t validation_crc32_data(uint32_t crc, const uint8_t *bytes,
                                      size_t length)
{
    size_t index;
    for (index = 0U; index < length; ++index)
        crc = validation_crc32_byte(crc, bytes[index]);
    return crc;
}

static size_t validation_unit(rom_byte_order_t order)
{
    if (order == ROM_BYTE_ORDER_Z64) return 1U;
    if (order == ROM_BYTE_ORDER_V64) return 2U;
    if (order == ROM_BYTE_ORDER_N64) return 4U;
    return 0U;
}

static size_t validation_ranges(uint64_t size, validation_range_t ranges[3])
{
    validation_range_t input[3];
    size_t input_count = 3U;
    size_t index;
    size_t count = 0U;

    input[0].first = 0U;
    input[0].last = ROM_HEADER_METADATA_BYTES;
    input[1].first = size / 2U;
    input[1].last = input[1].first + 64U;
    if (input[1].last > size) input[1].last = size;
    input[2].first = size - 64U;
    input[2].last = size;
    if (input[2].first < input[1].first) {
        validation_range_t swap = input[1];
        input[1] = input[2];
        input[2] = swap;
    }
    for (index = 0U; index < input_count; ++index) {
        if (count != 0U && input[index].first <= ranges[count - 1U].last) {
            if (input[index].last > ranges[count - 1U].last)
                ranges[count - 1U].last = input[index].last;
        } else {
            ranges[count++] = input[index];
        }
    }
    return count;
}

static bool validation_read_range(
    const library_fs_t *fs,
    int (*seek_callback)(void *context, void *handle, uint64_t offset),
    void *handle, rom_byte_order_t order, uint64_t first, uint64_t last,
    uint8_t raw[200], uint8_t normalized[200], const uint8_t **exact,
    size_t *exact_length)
{
    size_t unit = validation_unit(order);
    uint64_t aligned_first;
    uint64_t aligned_last;
    size_t amount;
    int64_t received;

    if (unit == 0U || first > last) return false;
    aligned_first = first - first % unit;
    aligned_last = last + (unit - last % unit) % unit;
    if (aligned_last < aligned_first || aligned_last - aligned_first > 200U)
        return false;
    amount = (size_t)(aligned_last - aligned_first);
    if (seek_callback(fs->context, handle, aligned_first) != LIBRARY_FS_ENTRY)
        return false;
    received = fs->file_read(fs->context, handle, raw, amount);
    if (received != (int64_t)amount ||
        !rom_normalize_prefix(order, raw, amount, normalized, amount))
        return false;
    *exact = normalized + (size_t)(first - aligned_first);
    *exact_length = (size_t)(last - first);
    return true;
}

/* This bounded signature detects ordinary source changes. A deliberately
 * constructed replacement preserving size, mtime, byte order, header CRC and
 * sampled CRC can evade it; this is not cryptographic proof of launch bytes. */
static bool validation_pass(
    const library_fs_t *fs,
    int (*seek_callback)(void *context, void *handle, uint64_t offset),
    const char *path, const validation_expectation_t *expected)
{
    uint8_t raw[200];
    uint8_t normalized[200];
    uint8_t canonical_header[ROM_HEADER_METADATA_BYTES];
    validation_range_t ranges[3];
    library_stat_t before;
    library_stat_t after;
    rom_header_t header;
    const uint8_t *exact;
    size_t exact_length;
    size_t range_count;
    size_t range_index;
    size_t unit;
    void *handle = NULL;
    uint32_t header_crc = 0xffffffffU;
    uint32_t sample_crc = 0xffffffffU;
    bool valid = false;
    int open_result;
    int close_result = LIBRARY_FS_ERROR;

    if (fs == NULL || seek_callback == NULL || path == NULL || expected == NULL ||
        fs->file_open_read == NULL || fs->file_read == NULL ||
        fs->file_close == NULL || fs->stat == NULL ||
        fs->stat(fs->context, path, &before) != LIBRARY_FS_ENTRY ||
        !validation_stat_expected(&before, &expected->source)) return false;

    unit = validation_unit(expected->source.byte_order);
    if (unit == 0U || expected->source.size % unit != 0U) return false;
    open_result = fs->file_open_read(fs->context, path, &handle);
    if (open_result != LIBRARY_FS_ENTRY || handle == NULL) {
        if (handle != NULL) (void)fs->file_close(fs->context, handle);
        return false;
    }

    if (!validation_read_range(fs, seek_callback, handle,
            expected->source.byte_order, 0U, ROM_HEADER_METADATA_BYTES,
            raw, normalized, &exact, &exact_length) ||
        !rom_header_parse(raw, ROM_HEADER_METADATA_BYTES, &header) ||
        header.byte_order != expected->source.byte_order) goto done;
    memcpy(canonical_header, exact, sizeof(canonical_header));
    header_crc = validation_crc32_data(header_crc, canonical_header,
                                       sizeof(canonical_header));

    range_count = validation_ranges(expected->source.size, ranges);
    for (range_index = 0U; range_index < range_count; ++range_index) {
        uint64_t first = ranges[range_index].first;
        if (first == 0U) {
            sample_crc = validation_crc32_data(sample_crc, canonical_header,
                                               sizeof(canonical_header));
            first = ROM_HEADER_METADATA_BYTES;
        }
        if (first == ranges[range_index].last) continue;
        if (!validation_read_range(fs, seek_callback, handle,
                expected->source.byte_order, first, ranges[range_index].last,
                raw, normalized, &exact, &exact_length)) goto done;
        sample_crc = validation_crc32_data(sample_crc, exact, exact_length);
    }
    header_crc ^= 0xffffffffU;
    sample_crc ^= 0xffffffffU;
    if (header_crc != expected->source.normalized_header_crc32 ||
        sample_crc != expected->source.normalized_sample_crc32 ||
        fs->stat(fs->context, path, &after) != LIBRARY_FS_ENTRY ||
        !validation_stat_expected(&after, &expected->source) ||
        !validation_stat_same(&before, &after)) goto done;
    valid = true;

done:
    close_result = fs->file_close(fs->context, handle);
    if (close_result != 0) valid = false;
    return valid;
}

static bool validate_library_source(menu_t *menu)
{
    validation_expectation_t expected;
    const library_fs_t *fs;
    int (*seek_callback)(void *context, void *handle, uint64_t offset);
    const char *path;

    if (menu->load.return_mode != MENU_MODE_LIBRARY) return true;
    if (!resolve_validation_expectation(menu, &expected)) return false;
#ifdef LOAD_ROM_HOST_TEST
    fs = host_validation_fs;
    seek_callback = host_validation_seek;
#else
    fs = &production_validation_fs;
    seek_callback = validation_file_seek;
#endif
    path = path_get(menu->load.rom_path);
    return validation_pass(fs, seek_callback, path, &expected);
}

static bool resolve_rom_path(menu_t *menu, bool *autoload, bool *resume) {
    enum {
        ROM_SOURCE_EXPLICIT,
        ROM_SOURCE_HISTORY,
        ROM_SOURCE_FAVORITE,
        ROM_SOURCE_BROWSER,
    } source;
    path_t *selected_path = NULL;
    menu_mode_t return_mode = MENU_MODE_BROWSER;
    rom_fingerprint_t expected_fingerprint = { { 0U } };
    bool expected_fingerprint_valid = false;
    int32_t selected_id = -1;

    *autoload = false;
    *resume = false;

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (menu->settings.rom_autoload_enabled && path_has_value(menu->load.rom_path) && menu->load_pending.rom_file) {
        deinit();
        rom_info_free_meta(&menu->load.rom_info);
        rom_filename = NULL;
        rom_filename = path_last_get(menu->load.rom_path);
        menu->load.load_history_id = -1;
        menu->load.load_favorite_id = -1;
        menu->load.resume_from_datel = false;
        menu->load.return_mode = MENU_MODE_BROWSER;
        *autoload = true;
        return true;
    }
#endif

    if (menu->load.resume_from_datel) {
        menu->load.resume_from_datel = false;
        if (path_has_value(menu->load.rom_path)) {
            *resume = true;
            return true;
        }

        clear_active_details(menu, true);
        menu_show_error(menu, convert_error_message(ROM_ERR_NO_FILE));
        return false;
    }

    if (menu->load.pending_rom_path_set) {
        source = ROM_SOURCE_EXPLICIT;
        return_mode = validate_return_mode(menu->load.pending_return_mode);
    } else if (menu->load.load_history_id != -1) {
        source = ROM_SOURCE_HISTORY;
        selected_id = menu->load.load_history_id;
    } else if (menu->load.load_favorite_id != -1) {
        source = ROM_SOURCE_FAVORITE;
        selected_id = menu->load.load_favorite_id;
    } else {
        source = ROM_SOURCE_BROWSER;
    }

    if (source == ROM_SOURCE_EXPLICIT) {
        selected_path = menu->load.pending_rom_path;
        menu->load.pending_rom_path = NULL;
        menu->load.pending_rom_path_set = false;
        expected_fingerprint = menu->load.expected_fingerprint;
        expected_fingerprint_valid = menu->load.expected_fingerprint_valid;
    }

    clear_active_details(menu, true);

    if (source == ROM_SOURCE_EXPLICIT) {
        menu->load.expected_fingerprint = expected_fingerprint;
        menu->load.expected_fingerprint_valid = expected_fingerprint_valid;
    }

    switch (source) {
        case ROM_SOURCE_EXPLICIT:
            break;
        case ROM_SOURCE_HISTORY:
            if (selected_id >= 0 && selected_id < HISTORY_COUNT &&
                menu->bookkeeping.history_items[selected_id].bookkeeping_type == BOOKKEEPING_TYPE_ROM &&
                path_has_value(menu->bookkeeping.history_items[selected_id].primary_path)) {
                selected_path = path_clone(menu->bookkeeping.history_items[selected_id].primary_path);
            }
            break;
        case ROM_SOURCE_FAVORITE:
            if (selected_id >= 0 && selected_id < FAVORITES_COUNT &&
                menu->bookkeeping.favorite_items[selected_id].bookkeeping_type == BOOKKEEPING_TYPE_ROM &&
                path_has_value(menu->bookkeeping.favorite_items[selected_id].primary_path)) {
                selected_path = path_clone(menu->bookkeeping.favorite_items[selected_id].primary_path);
            }
            break;
        case ROM_SOURCE_BROWSER:
            if (path_has_value(menu->browser.directory) && menu->browser.entry != NULL &&
                menu->browser.entry->name != NULL && menu->browser.entry->name[0] != '\0') {
                selected_path = path_clone_push(menu->browser.directory, menu->browser.entry->name);
            }
            break;
    }

    if (!path_has_value(selected_path)) {
        path_free(selected_path);
        menu_show_error_context(menu, convert_error_message(ROM_ERR_NO_FILE),
            return_mode, menu->library_view.selected_valid
                ? &menu->library_view.selected_fingerprint : NULL,
            (int32_t)menu->library_view.visual_offset);
        return false;
    }

    menu->load.rom_path = selected_path;
    menu->load.return_mode = return_mode;
    rom_filename = path_last_get(menu->load.rom_path);
    return true;
}

void view_load_rom_set_pending_path(menu_t *menu, path_t *rom_path, menu_mode_t return_mode) {
    path_free(menu->load.pending_rom_path);
    menu->load.pending_rom_path = rom_path;
    menu->load.pending_rom_path_set = rom_path != NULL;
    menu->load.pending_return_mode = validate_return_mode(return_mode);
    menu->load.expected_fingerprint_valid =
        menu->load.pending_return_mode == MENU_MODE_LIBRARY &&
        menu->library_view.selected_valid;
    if (menu->load.expected_fingerprint_valid)
        menu->load.expected_fingerprint = menu->library_view.pending_fingerprint;
    else
        memset(&menu->load.expected_fingerprint, 0,
               sizeof(menu->load.expected_fingerprint));
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
}

void view_load_rom_init (menu_t *menu) {
    bool autoload = false;
    bool resume = false;
    if (!resolve_rom_path(menu, &autoload, &resume) || resume) {
        return;
    }

    if (!validate_library_source(menu)) {
        show_details_error(menu, "Indexed ROM changed or is no longer available");
        return;
    }

    debugf("Load ROM: loading ROM info from %s\n", path_get(menu->load.rom_path));
    rom_err_t err = rom_config_load(menu->load.rom_path, &menu->load.rom_info);
    if (err != ROM_OK) {
        // FIXME: use bookkeeping_favorite_remove() here instead of just showing an error and leaving the broken favorite / history item in place
        show_details_error(menu, convert_error_message(err));
        return;
    }
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (!autoload) {
#endif
#ifndef LOAD_ROM_HOST_TEST
        current_metadata_image_index = 0;
        boxart = ui_components_boxart_init(menu->storage_prefix, menu->load.rom_info.game_code, menu->load.rom_info.title, IMAGE_BOXART_FRONT);
        ui_components_context_menu_init(&options_context_menu);
#endif
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    }
#endif

}

void view_load_rom_display (menu_t *menu, surface_t *display) {
#ifndef LOAD_ROM_HOST_TEST
    process(menu);

    draw(menu, display);
#else
    (void)display;
    if (menu->actions.enter) menu->load_pending.rom_file = true;
#endif

    if (menu->load_pending.rom_file) {
        menu->load_pending.rom_file = false;
        load(menu);
    }

    if (menu->next_mode == MENU_MODE_BOOT) {
        deinit();
    } else if (menu->next_mode != MENU_MODE_LOAD_ROM &&
               menu->next_mode != MENU_MODE_DATEL_CODE_EDITOR) {
        clear_active_details(menu, true);
    }
}
