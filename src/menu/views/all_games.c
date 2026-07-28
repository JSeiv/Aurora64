#include "../library/library_snapshot.h"
#include "../library/rom_identity.h"
#include "../library/library_service.h"
#include "../path.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef LIBRARY_VIEW_HOST_TEST
typedef struct {
    menu_mode_t mode;
    menu_mode_t next_mode;
    const char *storage_prefix;
    library_service_t *library_service;
    struct {
        bool go_up;
        bool go_down;
        bool go_left;
        bool go_right;
        bool enter;
        bool back;
    } actions;
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
} library_view_menu_t;
#else
#include "../menu_state.h"
typedef menu_t library_view_menu_t;
#endif

#define LIBRARY_COLUMNS 3U
#define LIBRARY_PAGE_SIZE 6U

enum {
    LIBRARY_VIEW_TRANSITION_IDLE,
    LIBRARY_VIEW_TRANSITION_LAUNCH,
    LIBRARY_VIEW_TRANSITION_EXIT,
    LIBRARY_VIEW_TRANSITION_SUBMITTED
};

enum {
    LIBRARY_VIEW_MESSAGE_NONE,
    LIBRARY_VIEW_MESSAGE_REMOVED,
    LIBRARY_VIEW_MESSAGE_INVALID_SOURCE,
    LIBRARY_VIEW_MESSAGE_OOM
};

enum {
    LIBRARY_VIEW_MOVE_UP,
    LIBRARY_VIEW_MOVE_DOWN,
    LIBRARY_VIEW_MOVE_LEFT,
    LIBRARY_VIEW_MOVE_RIGHT
};

void view_load_rom_set_pending_path(library_view_menu_t *menu, path_t *rom_path,
                                    menu_mode_t return_mode);

#ifdef LIBRARY_VIEW_HOST_TEST
static size_t host_snapshot_acquires;
static size_t host_snapshot_releases;

void library_view_host_snapshot_counts_reset(void)
{
    host_snapshot_acquires = 0U;
    host_snapshot_releases = 0U;
}

size_t library_view_host_snapshot_acquire_count(void)
{
    return host_snapshot_acquires;
}

size_t library_view_host_snapshot_release_count(void)
{
    return host_snapshot_releases;
}
#endif

static const library_snapshot_t *view_snapshot_acquire(
    library_service_t *service)
{
    const library_snapshot_t *snapshot =
        library_service_snapshot_acquire(service);
#ifdef LIBRARY_VIEW_HOST_TEST
    if (snapshot != NULL) ++host_snapshot_acquires;
#endif
    return snapshot;
}

static void view_snapshot_release(const library_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
#ifdef LIBRARY_VIEW_HOST_TEST
    ++host_snapshot_releases;
#endif
    library_service_snapshot_release(snapshot);
}

static size_t bounded_length(const char *text, size_t bound)
{
    size_t length = 0U;

    if (text == NULL) return 0U;
    while (length < bound && text[length] != '\0') ++length;
    return length;
}

uint32_t library_view_model_move(uint32_t selected, size_t count, uint8_t move)
{
    uint32_t candidate;
    uint32_t column;

    if (count == 0U) return 0U;
    if (selected >= count) selected = (uint32_t)(count - 1U);
    candidate = selected;
    column = selected % LIBRARY_COLUMNS;

    switch (move) {
        case LIBRARY_VIEW_MOVE_UP:
            if (selected >= LIBRARY_COLUMNS) candidate -= LIBRARY_COLUMNS;
            break;
        case LIBRARY_VIEW_MOVE_DOWN:
            if ((size_t)selected + LIBRARY_COLUMNS < count)
                candidate += LIBRARY_COLUMNS;
            break;
        case LIBRARY_VIEW_MOVE_LEFT:
            if (column > 0U) --candidate;
            break;
        case LIBRARY_VIEW_MOVE_RIGHT:
            if (column + 1U < LIBRARY_COLUMNS && (size_t)selected + 1U < count)
                ++candidate;
            break;
        default:
            break;
    }
    return candidate;
}

uint32_t library_view_model_page_offset(uint32_t selected, size_t count)
{
    if (count == 0U) return 0U;
    if (selected >= count) selected = (uint32_t)(count - 1U);
    return (selected / LIBRARY_PAGE_SIZE) * LIBRARY_PAGE_SIZE;
}

uint32_t library_view_model_resolve(const library_record_t *records, size_t count,
                                    const rom_fingerprint_t *selected,
                                    bool selected_valid, uint32_t previous_index,
                                    bool *found)
{
    size_t index;

    if (found != NULL) *found = false;
    if (records != NULL && selected != NULL && selected_valid) {
        for (index = 0U; index < count; ++index) {
            if (rom_fingerprint_equal(&records[index].fingerprint, selected)) {
                if (found != NULL) *found = true;
                return (uint32_t)index;
            }
        }
    }

    /* Missing identity fallback: retain the nearest valid prior index. */
    if (count == 0U) return 0U;
    return previous_index < count ? previous_index : (uint32_t)(count - 1U);
}

static void copy_title(char *out, size_t out_size, const char *source,
                       size_t source_length, bool clean)
{
    size_t input;
    size_t output = 0U;
    bool previous_space = false;

    for (input = 0U; input < source_length && output + 1U < out_size; ++input) {
        char value = source[input];
        if (clean && value == '_') value = ' ';
        if (clean && value == ' ') {
            if (output == 0U || previous_space) continue;
            previous_space = true;
        } else {
            previous_space = false;
        }
        out[output++] = value;
    }
    while (clean && output > 0U && out[output - 1U] == ' ') --output;
    out[output] = '\0';
}

void library_view_model_title(const library_record_t *record,
                              const char *logical_path, char *out,
                              size_t out_size)
{
    size_t header_length;
    size_t path_length;
    size_t basename_offset = 0U;
    size_t basename_length;
    size_t stem_length;
    size_t index;

    if (out == NULL || out_size == 0U) return;
    out[0] = '\0';

    header_length = record == NULL
        ? 0U : bounded_length(record->title, sizeof(record->title));
    while (header_length > 0U && record->title[header_length - 1U] == ' ')
        --header_length;
    if (header_length > 0U) {
        copy_title(out, out_size, record->title, header_length, false);
        return;
    }

    path_length = bounded_length(logical_path, LIBRARY_SNAPSHOT_PATH_BYTES);
    if (logical_path == NULL || path_length == 0U ||
        path_length == LIBRARY_SNAPSHOT_PATH_BYTES) {
        copy_title(out, out_size, "Untitled", 8U, false);
        return;
    }
    for (index = 0U; index < path_length; ++index) {
        if (logical_path[index] == '/') basename_offset = index + 1U;
    }
    basename_length = path_length - basename_offset;
    if (basename_length == 0U) {
        copy_title(out, out_size, "Untitled", 8U, false);
        return;
    }

    stem_length = basename_length;
    for (index = basename_length; index > 1U; --index) {
        if (logical_path[basename_offset + index - 1U] == '.') {
            stem_length = index - 1U;
            break;
        }
    }
    if (stem_length > 0U) {
        copy_title(out, out_size, logical_path + basename_offset,
                   stem_length, true);
        if (out[0] != '\0') return;
    }

    /* Cleaning may yield no title; preserve the original filename as fallback. */
    copy_title(out, out_size, logical_path + basename_offset,
               basename_length, false);
}

bool library_view_model_source_path(const library_snapshot_t *snapshot,
                                    const library_record_t *record,
                                    const char **logical_path)
{
    size_t source_count;
    size_t first;
    size_t primary;

    if (logical_path != NULL) *logical_path = NULL;
    if (snapshot == NULL || record == NULL || logical_path == NULL ||
        record->source_count == 0U) return false;

    source_count = library_snapshot_source_count(snapshot);
    first = record->source_first;
    primary = record->primary_source_index;
    if (first >= source_count || record->source_count > source_count - first ||
        primary < first || primary >= first + record->source_count ||
        primary >= source_count ||
        library_snapshot_source_at(snapshot, primary) == NULL) return false;

    *logical_path = library_snapshot_source_path(snapshot, primary);
    if (*logical_path == NULL || (*logical_path)[0] != '/' ||
        bounded_length(*logical_path, LIBRARY_SNAPSHOT_PATH_BYTES) >=
            LIBRARY_SNAPSHOT_PATH_BYTES) {
        *logical_path = NULL;
        return false;
    }
    return true;
}

static void library_view_set_message(library_view_menu_t *menu, uint8_t message)
{
    menu->library_view.message = message;
    menu->library_view.transition = LIBRARY_VIEW_TRANSITION_IDLE;
    menu->library_view.pending_destination = MENU_MODE_LIBRARY;
    view_load_rom_set_pending_path(menu, NULL, MENU_MODE_LIBRARY);
    library_service_resume(menu->library_service);
}

static void reconcile(library_view_menu_t *menu, const library_snapshot_t *snapshot)
{
    size_t count = library_snapshot_record_count(snapshot);
    const library_record_t *records = count == 0U
        ? NULL : library_snapshot_record_at(snapshot, 0U);
    uint32_t generation = library_snapshot_generation(snapshot);
    bool had_selection = menu->library_view.selected_valid;
    bool found = false;
    uint32_t resolved = library_view_model_resolve(
        records, count, &menu->library_view.selected_fingerprint,
        had_selection, menu->library_view.last_resolved_index, &found);

    if (count == 0U) {
        menu->library_view.selected_valid = false;
        menu->library_view.last_resolved_index = 0U;
        menu->library_view.visual_offset = 0U;
    } else {
        const library_record_t *record =
            library_snapshot_record_at(snapshot, resolved);
        menu->library_view.last_resolved_index = resolved;
        menu->library_view.visual_offset =
            library_view_model_page_offset(resolved, count);
        if (record != NULL) {
            menu->library_view.selected_fingerprint = record->fingerprint;
            menu->library_view.selected_valid = true;
        }
        if (had_selection && !found &&
            menu->library_view.observed_generation != 0U &&
            menu->library_view.observed_generation != generation) {
            menu->library_view.message = LIBRARY_VIEW_MESSAGE_REMOVED;
        }
    }
    menu->library_view.observed_generation = generation;
}

static void select_index(library_view_menu_t *menu, const library_snapshot_t *snapshot,
                         uint32_t index)
{
    const library_record_t *record = library_snapshot_record_at(snapshot, index);

    if (record == NULL) return;
    menu->library_view.last_resolved_index = index;
    menu->library_view.visual_offset = library_view_model_page_offset(
        index, library_snapshot_record_count(snapshot));
    menu->library_view.selected_fingerprint = record->fingerprint;
    menu->library_view.selected_valid = true;
    menu->library_view.message = LIBRARY_VIEW_MESSAGE_NONE;
}

static void begin_launch(library_view_menu_t *menu)
{
    if (!menu->library_view.selected_valid) return;
    menu->library_view.pending_fingerprint =
        menu->library_view.selected_fingerprint;
    menu->library_view.transition = LIBRARY_VIEW_TRANSITION_LAUNCH;
    menu->library_view.message = LIBRARY_VIEW_MESSAGE_NONE;
    view_load_rom_set_pending_path(menu, NULL, MENU_MODE_LIBRARY);
    library_service_request_pause(menu->library_service);
}

static void begin_exit(library_view_menu_t *menu, menu_mode_t destination)
{
    menu->library_view.pending_destination = destination;
    menu->library_view.transition = LIBRARY_VIEW_TRANSITION_EXIT;
    library_service_request_pause(menu->library_service);
}

static void complete_launch(library_view_menu_t *menu)
{
    const library_snapshot_t *snapshot;
    const library_record_t *record;
    const char *logical_path;
    path_t *owned_path = NULL;

    if (!library_service_is_quiesced(menu->library_service)) return;
    snapshot = view_snapshot_acquire(menu->library_service);
    if (snapshot == NULL) {
        library_view_set_message(menu, LIBRARY_VIEW_MESSAGE_REMOVED);
        return;
    }

    record = library_snapshot_find_fingerprint(
        snapshot, &menu->library_view.pending_fingerprint);
    if (record == NULL) {
        view_snapshot_release(snapshot);
        library_view_set_message(menu, LIBRARY_VIEW_MESSAGE_REMOVED);
        return;
    }
    if (!library_view_model_source_path(snapshot, record, &logical_path)) {
        view_snapshot_release(snapshot);
        library_view_set_message(menu, LIBRARY_VIEW_MESSAGE_INVALID_SOURCE);
        return;
    }
    if (!path_try_init(&owned_path, menu->storage_prefix, logical_path)) {
        view_snapshot_release(snapshot);
        library_view_set_message(menu, LIBRARY_VIEW_MESSAGE_OOM);
        return;
    }

    view_snapshot_release(snapshot);
    view_load_rom_set_pending_path(menu, owned_path, MENU_MODE_LIBRARY);
    menu->library_view.transition = LIBRARY_VIEW_TRANSITION_SUBMITTED;
    menu->next_mode = MENU_MODE_LOAD_ROM;
}

static bool process_input(library_view_menu_t *menu, const library_snapshot_t *snapshot)
{
    size_t count = snapshot == NULL ? 0U
                                    : library_snapshot_record_count(snapshot);
    uint32_t old_index = menu->library_view.last_resolved_index;
    uint32_t index = old_index;

    if (menu->actions.back) {
        begin_exit(menu, MENU_MODE_HOME);
        return true;
    }
    if (snapshot == NULL) return false;

    if (menu->actions.go_up)
        index = library_view_model_move(index, count, LIBRARY_VIEW_MOVE_UP);
    else if (menu->actions.go_down)
        index = library_view_model_move(index, count, LIBRARY_VIEW_MOVE_DOWN);
    else if (menu->actions.go_left)
        index = library_view_model_move(index, count, LIBRARY_VIEW_MOVE_LEFT);
    else if (menu->actions.go_right)
        index = library_view_model_move(index, count, LIBRARY_VIEW_MOVE_RIGHT);
    else if (menu->actions.enter) {
        begin_launch(menu);
        return menu->library_view.transition == LIBRARY_VIEW_TRANSITION_LAUNCH;
    }

    if (index != old_index) select_index(menu, snapshot, index);
    return false;
}

void library_view_model_step(library_view_menu_t *menu)
{
    const library_snapshot_t *snapshot;
    bool began_transition = false;

    if (menu == NULL || menu->library_service == NULL) return;

    snapshot = view_snapshot_acquire(menu->library_service);
    if (snapshot != NULL) {
        reconcile(menu, snapshot);
    }
    if (menu->library_view.transition == LIBRARY_VIEW_TRANSITION_IDLE)
        began_transition = process_input(menu, snapshot);
    if (snapshot != NULL) view_snapshot_release(snapshot);

    if (began_transition) return;
    if (menu->library_view.transition == LIBRARY_VIEW_TRANSITION_LAUNCH) {
        complete_launch(menu);
    } else if (menu->library_view.transition == LIBRARY_VIEW_TRANSITION_EXIT &&
               library_service_is_quiesced(menu->library_service)) {
        menu->next_mode = menu->library_view.pending_destination;
        menu->library_view.transition = LIBRARY_VIEW_TRANSITION_SUBMITTED;
    }
}

#ifndef LIBRARY_VIEW_HOST_TEST
#include "../sound.h"
#include "../ui_components/constants.h"
#include "views.h"

#define LIBRARY_CARD_WIDTH 168
#define LIBRARY_CARD_HEIGHT 132
#define LIBRARY_CARD_GAP_X 16
#define LIBRARY_CARD_GAP_Y 16
#define LIBRARY_CARD_X 52
#define LIBRARY_CARD_Y 82
#define LIBRARY_CARD_PADDING_X 12
#define LIBRARY_CARD_PADDING_Y 12
#define LIBRARY_TEXT_OFFSET_Y 1
#define LIBRARY_FOCUS_WIDTH 4
#define LIBRARY_FOCUS_INSET_Y 8

_Static_assert(sizeof(((menu_t *)0)->library_view) == 84U,
               "library view state must remain bounded by-value state");

static const char *status_text(const library_snapshot_t *snapshot,
                               const menu_t *menu)
{
    if (menu->library_view.message == LIBRARY_VIEW_MESSAGE_REMOVED)
        return "Selection changed: game was removed";
    if (menu->library_view.message == LIBRARY_VIEW_MESSAGE_INVALID_SOURCE)
        return "Game source is no longer valid";
    if (menu->library_view.message == LIBRARY_VIEW_MESSAGE_OOM)
        return "Not enough memory to open game";
    if (snapshot == NULL) {
        return library_service_is_quiesced(menu->library_service)
            ? "Library unavailable" : "Scanning library...";
    }
    if (library_snapshot_error_count(snapshot) != 0U)
        return "Library scan completed with errors";
    if (library_snapshot_warning_count(snapshot) != 0U)
        return "Library scan completed with warnings";
    switch (library_snapshot_status(snapshot)) {
        case LIBRARY_SNAPSHOT_EMPTY: return "No games found";
        case LIBRARY_SNAPSHOT_STALE: return "Library is stale";
        case LIBRARY_SNAPSHOT_REVALIDATING: return "Scanning library...";
        case LIBRARY_SNAPSHOT_FRESH: return "Library ready";
        case LIBRARY_SNAPSHOT_FAILED_STALE:
            return "Library refresh failed; showing previous games";
    }
    return "Library status unavailable";
}

static void draw(menu_t *menu, surface_t *display,
                 const library_snapshot_t *snapshot)
{
    size_t count = snapshot == NULL ? 0U
                                    : library_snapshot_record_count(snapshot);
    size_t slot;
    rdpq_textparms_t status_parms = {
        .width = 536, .height = 20, .align = ALIGN_LEFT,
        .valign = VALIGN_TOP, .wrap = WRAP_NONE
    };

    rdpq_attach(display, NULL);
    ui_components_background_draw();
    ui_components_layout_draw();
    for (slot = 0U; slot < LIBRARY_PAGE_SIZE; ++slot) {
        size_t index = menu->library_view.visual_offset + slot;
        const library_record_t *record;
        const char *logical_path = NULL;
        char title[64];
        int row;
        int column;
        int x0;
        int y0;
        int x1;
        int y1;
        rdpq_textparms_t parms;

        if (index >= count) break;
        record = library_snapshot_record_at(snapshot, index);
        if (record != NULL)
            (void)library_view_model_source_path(snapshot, record, &logical_path);
        library_view_model_title(record, logical_path, title, sizeof(title));
        row = (int)(slot / LIBRARY_COLUMNS);
        column = (int)(slot % LIBRARY_COLUMNS);
        x0 = LIBRARY_CARD_X + column * (LIBRARY_CARD_WIDTH + LIBRARY_CARD_GAP_X);
        y0 = LIBRARY_CARD_Y + row * (LIBRARY_CARD_HEIGHT + LIBRARY_CARD_GAP_Y);
        x1 = x0 + LIBRARY_CARD_WIDTH;
        y1 = y0 + LIBRARY_CARD_HEIGHT;
        parms = (rdpq_textparms_t){
            .width = LIBRARY_CARD_WIDTH - 2 * BORDER_THICKNESS -
                     2 * LIBRARY_CARD_PADDING_X,
            .height = LIBRARY_CARD_HEIGHT - 2 * BORDER_THICKNESS -
                      2 * LIBRARY_CARD_PADDING_Y,
            .align = ALIGN_CENTER,
            .valign = VALIGN_CENTER,
            .wrap = WRAP_WORD,
        };
        ui_components_box_draw(
            x0 + BORDER_THICKNESS, y0 + BORDER_THICKNESS,
            x1 - BORDER_THICKNESS, y1 - BORDER_THICKNESS,
            index == menu->library_view.last_resolved_index
                ? TAB_ACTIVE_BACKGROUND_COLOR : TAB_INACTIVE_BACKGROUND_COLOR);
        ui_components_border_draw(x0 + BORDER_THICKNESS,
                                  y0 + BORDER_THICKNESS,
                                  x1 - BORDER_THICKNESS,
                                  y1 - BORDER_THICKNESS);
        if (index == menu->library_view.last_resolved_index) {
            ui_components_box_draw(
                x0 + BORDER_THICKNESS,
                y0 + BORDER_THICKNESS + LIBRARY_FOCUS_INSET_Y,
                x0 + BORDER_THICKNESS + LIBRARY_FOCUS_WIDTH,
                y1 - BORDER_THICKNESS - LIBRARY_FOCUS_INSET_Y,
                BORDER_COLOR);
        }
        rdpq_text_print(&parms, FNT_DEFAULT,
                        x0 + BORDER_THICKNESS + LIBRARY_CARD_PADDING_X,
                        y0 + BORDER_THICKNESS + LIBRARY_CARD_PADDING_Y +
                            LIBRARY_TEXT_OFFSET_Y,
                        title);
    }
    ui_components_actions_bar_text_draw(
        STL_DEFAULT, ALIGN_LEFT, VALIGN_TOP,
        menu->library_view.transition == LIBRARY_VIEW_TRANSITION_IDLE
            ? "A: View details  B: Home" : "Pausing library work...");
    rdpq_text_print(&status_parms, FNT_DEFAULT, 52, 55,
                    status_text(snapshot, menu));
    rdpq_detach_show();
}

void view_all_games_init(menu_t *menu)
{
    menu->library_view.transition = LIBRARY_VIEW_TRANSITION_IDLE;
    menu->library_view.pending_destination = MENU_MODE_LIBRARY;
    library_service_resume(menu->library_service);
}

void view_all_games_display(menu_t *menu, surface_t *display)
{
    const library_snapshot_t *snapshot;

    library_view_model_step(menu);
    snapshot = view_snapshot_acquire(menu->library_service);
    draw(menu, display, snapshot);
    if (snapshot != NULL) view_snapshot_release(snapshot);
}
#endif
