#include "support/layer1_view_host_shims.h"
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#endif
#include "menu/menu_state.h"
#include "menu/path.h"
#include "menu/sound.h"
#include "menu/ui_components.h"
#include "menu/views/views.h"
#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
#include "menu/library/library_metrics.h"
#endif
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if LAYER1_VIEW_HOST_DISABLED_FS_SHIMS
#include <dir.h>
#include <errno.h>
#endif

#define LAYER1_VIEW_HOST_TEXT_CAPACITY 64U
#define LAYER1_VIEW_HOST_TEXT_BYTES 128U
#define LAYER1_VIEW_HOST_EVENT_CAPACITY 256U
#define LAYER1_VIEW_HOST_BOX_CAPACITY 64U
#define LAYER1_VIEW_HOST_PATH_BYTES 512U

static char host_text[LAYER1_VIEW_HOST_TEXT_CAPACITY]
                     [LAYER1_VIEW_HOST_TEXT_BYTES];
static float host_text_x[LAYER1_VIEW_HOST_TEXT_CAPACITY];
static float host_text_y[LAYER1_VIEW_HOST_TEXT_CAPACITY];
static size_t host_text_count;
static layer1_view_host_event_t host_events[LAYER1_VIEW_HOST_EVENT_CAPACITY];
static size_t host_event_count;
static int host_boxes[LAYER1_VIEW_HOST_BOX_CAPACITY][4];
static size_t host_box_count;
static menu_t *host_observed_menu;
static size_t host_sound_calls;
static bool host_sound_saw_input;
static int host_sound_next_mode;
static bool host_attach_saw_frame_begin;
static bool host_detach_saw_frame_submitted;
static bool host_usb_active;
static size_t host_usb_polls;
static size_t host_usb_returns;
static bool host_path_success;
static size_t host_path_calls;
static size_t host_path_live;
static size_t host_path_frees;
static char host_path_prefix[LAYER1_VIEW_HOST_PATH_BYTES];
static char host_path_logical[LAYER1_VIEW_HOST_PATH_BYTES];
static path_t *host_pending_path;
static const void *host_pending_owner;
static int host_pending_return_mode;

static void host_event(layer1_view_host_event_t event)
{
    if (host_event_count < LAYER1_VIEW_HOST_EVENT_CAPACITY)
        host_events[host_event_count++] = event;
}

static void capture_text(const char *text, size_t length, float x, float y,
                         layer1_view_host_event_t event)
{
    size_t copy_length;
    if (text == NULL || host_text_count >= LAYER1_VIEW_HOST_TEXT_CAPACITY)
        return;
    copy_length = length;
    if (copy_length >= LAYER1_VIEW_HOST_TEXT_BYTES)
        copy_length = LAYER1_VIEW_HOST_TEXT_BYTES - 1U;
    memcpy(host_text[host_text_count], text, copy_length);
    host_text[host_text_count][copy_length] = '\0';
    host_text_x[host_text_count] = x;
    host_text_y[host_text_count] = y;
    ++host_text_count;
    host_event(event);
}

static void host_path_free(path_t *path)
{
    if (path == NULL) return;
    path_free(path);
    if (host_path_live != 0U) --host_path_live;
    ++host_path_frees;
}

#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
static bool host_trace_event_valid(library_metrics_event_t event)
{
    library_metrics_snapshot_t snapshot;
    uint8_t bit = (uint8_t)(UINT8_C(1) << (unsigned int)event);
    library_metrics_snapshot(&snapshot);
    return (snapshot.trace.valid_mask & bit) != 0U;
}
#endif

void layer1_view_host_reset(void)
{
    host_path_free(host_pending_path);
    memset(host_text, 0, sizeof(host_text));
    memset(host_text_x, 0, sizeof(host_text_x));
    memset(host_text_y, 0, sizeof(host_text_y));
    memset(host_events, 0, sizeof(host_events));
    memset(host_boxes, 0, sizeof(host_boxes));
    memset(host_path_prefix, 0, sizeof(host_path_prefix));
    memset(host_path_logical, 0, sizeof(host_path_logical));
    host_text_count = 0U;
    host_event_count = 0U;
    host_box_count = 0U;
    host_observed_menu = NULL;
    host_sound_calls = 0U;
    host_sound_saw_input = false;
    host_sound_next_mode = MENU_MODE_HOME;
    host_attach_saw_frame_begin = false;
    host_detach_saw_frame_submitted = false;
    host_usb_active = false;
    host_usb_polls = 0U;
    host_usb_returns = 0U;
    host_path_success = false;
    host_path_calls = 0U;
    host_path_live = 0U;
    host_path_frees = 0U;
    host_pending_path = NULL;
    host_pending_owner = NULL;
    host_pending_return_mode = MENU_MODE_HOME;
}

size_t layer1_view_host_text_count(void)
{
    return host_text_count;
}

const char *layer1_view_host_text_at(size_t index)
{
    return index < host_text_count ? host_text[index] : NULL;
}

bool layer1_view_host_saw_text(const char *text)
{
    return layer1_view_host_text_occurrences(text) != 0U;
}

size_t layer1_view_host_text_occurrences(const char *text)
{
    size_t index;
    size_t count = 0U;
    if (text == NULL) return 0U;
    for (index = 0U; index < host_text_count; ++index) {
        if (strcmp(host_text[index], text) == 0) ++count;
    }
    return count;
}

float layer1_view_host_text_x(size_t index)
{
    return index < host_text_count ? host_text_x[index] : 0.0f;
}

float layer1_view_host_text_y(size_t index)
{
    return index < host_text_count ? host_text_y[index] : 0.0f;
}

size_t layer1_view_host_event_count(void)
{
    return host_event_count;
}

layer1_view_host_event_t layer1_view_host_event_at(size_t index)
{
    return index < host_event_count ? host_events[index] : 0;
}

size_t layer1_view_host_box_count(void)
{
    return host_box_count;
}

bool layer1_view_host_box_at(size_t index, int *x0, int *y0, int *x1, int *y1)
{
    if (index >= host_box_count) return false;
    if (x0 != NULL) *x0 = host_boxes[index][0];
    if (y0 != NULL) *y0 = host_boxes[index][1];
    if (x1 != NULL) *x1 = host_boxes[index][2];
    if (y1 != NULL) *y1 = host_boxes[index][3];
    return true;
}

void layer1_view_host_observe_menu(void *menu)
{
    host_observed_menu = menu;
}

size_t layer1_view_host_sound_calls(void)
{
    return host_sound_calls;
}

bool layer1_view_host_sound_saw_input_trace(void)
{
    return host_sound_saw_input;
}

int layer1_view_host_sound_next_mode(void)
{
    return host_sound_next_mode;
}

bool layer1_view_host_attach_saw_frame_begin(void)
{
    return host_attach_saw_frame_begin;
}

bool layer1_view_host_detach_saw_frame_submitted(void)
{
    return host_detach_saw_frame_submitted;
}

size_t layer1_view_host_usb_poll_calls(void)
{
    return host_usb_polls;
}

size_t layer1_view_host_usb_return_count(void)
{
    return host_usb_returns;
}

bool layer1_view_host_usb_poll_active(void)
{
    return host_usb_active;
}

void layer1_view_host_set_path_success(bool enabled)
{
    host_path_success = enabled;
}

size_t layer1_view_host_path_calls(void)
{
    return host_path_calls;
}

const char *layer1_view_host_path_prefix(void)
{
    return host_path_prefix;
}

const char *layer1_view_host_path_logical(void)
{
    return host_path_logical;
}

size_t layer1_view_host_path_live_count(void)
{
    return host_path_live;
}

size_t layer1_view_host_path_free_count(void)
{
    return host_path_frees;
}

bool layer1_view_host_pending_path_matches(const void *owner,
                                           const char *full_path,
                                           int return_mode)
{
    return owner != NULL && owner == host_pending_owner &&
           host_pending_path != NULL && host_pending_path->buffer != NULL &&
           full_path != NULL &&
           strcmp(host_pending_path->buffer, full_path) == 0 &&
           return_mode == host_pending_return_mode;
}

void rdpq_attach(const surface_t *surf_color, const surface_t *surf_z)
{
    (void)surf_color;
    (void)surf_z;
#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
    host_attach_saw_frame_begin = host_trace_event_valid(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_BEGIN);
#endif
    host_event(LAYER1_VIEW_HOST_EVENT_ATTACH);
}

void rdpq_detach_show(void)
{
#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
    host_detach_saw_frame_submitted = host_trace_event_valid(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED);
#endif
    host_event(LAYER1_VIEW_HOST_EVENT_DETACH);
}

rdpq_textmetrics_t rdpq_text_printn(const rdpq_textparms_t *parms,
                                    uint8_t font_id, float x0, float y0,
                                    const char *utf8_text, int nbytes)
{
    rdpq_textmetrics_t result;
    (void)parms;
    (void)font_id;
    memset(&result, 0, sizeof(result));
    if (nbytes > 0)
        capture_text(utf8_text, (size_t)nbytes, x0, y0,
                     LAYER1_VIEW_HOST_EVENT_TEXT);
    return result;
}

void ui_components_background_draw(void)
{
}

void ui_components_layout_draw(void)
{
}

void ui_components_box_draw(int x0, int y0, int x1, int y1, color_t color)
{
    (void)color;
    if (host_box_count < LAYER1_VIEW_HOST_BOX_CAPACITY) {
        host_boxes[host_box_count][0] = x0;
        host_boxes[host_box_count][1] = y0;
        host_boxes[host_box_count][2] = x1;
        host_boxes[host_box_count][3] = y1;
        ++host_box_count;
    }
    host_event(LAYER1_VIEW_HOST_EVENT_BOX);
}

void ui_components_border_draw(int x0, int y0, int x1, int y1)
{
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    host_event(LAYER1_VIEW_HOST_EVENT_BORDER);
}

void ui_components_actions_bar_text_draw(menu_font_type_t style,
                                          rdpq_align_t align,
                                          rdpq_valign_t valign,
                                          char *fmt, ...)
{
    char text[LAYER1_VIEW_HOST_TEXT_BYTES];
    va_list arguments;
    int length;
    (void)style;
    (void)align;
    (void)valign;
    va_start(arguments, fmt);
    length = vsnprintf(text, sizeof(text), fmt, arguments);
    va_end(arguments);
    if (length > 0)
        capture_text(text, strlen(text), 0.0f, 0.0f,
                     LAYER1_VIEW_HOST_EVENT_ACTION_BAR);
}

void sound_play_effect(sound_effect_t sfx)
{
    (void)sfx;
    ++host_sound_calls;
    host_sound_next_mode = host_observed_menu == NULL
                               ? MENU_MODE_HOME
                               : host_observed_menu->next_mode;
#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
    host_sound_saw_input = host_trace_event_valid(
        LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_INPUT_RECEIVED);
#endif
    host_event(LAYER1_VIEW_HOST_EVENT_SOUND);
}

void usb_comm_poll(menu_t *menu)
{
    (void)menu;
    host_usb_active = true;
    ++host_usb_polls;
    host_event(LAYER1_VIEW_HOST_EVENT_USB_POLL);
    host_usb_active = false;
    ++host_usb_returns;
}

void usb_comm_transition_reset(void)
{
    host_usb_active = false;
}

bool layer1_view_host_path_try_init(path_t **out, const char *prefix,
                                         const char *logical)
{
    if (out == NULL) return false;
    *out = NULL;
    ++host_path_calls;
    snprintf(host_path_prefix, sizeof(host_path_prefix), "%s",
             prefix == NULL ? "" : prefix);
    snprintf(host_path_logical, sizeof(host_path_logical), "%s",
             logical == NULL ? "" : logical);
    if (!host_path_success) return false;
    if (!path_try_init(out, prefix, logical)) return false;
    ++host_path_live;
    return true;
}

void view_load_rom_set_pending_path(menu_t *menu, path_t *rom_path,
                                    menu_mode_t return_mode)
{
    if (host_pending_path != rom_path) host_path_free(host_pending_path);
    host_pending_path = rom_path;
    host_pending_owner = menu;
    host_pending_return_mode = return_mode;
    host_event(LAYER1_VIEW_HOST_EVENT_PENDING_PATH);
}

#if LAYER1_VIEW_HOST_DISABLED_FS_SHIMS
FILE *library_fs_host_test_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int dir_findfirst(const char *const path, dir_t *dir)
{
    (void)path;
    if (dir != NULL) memset(dir, 0, sizeof(*dir));
    errno = ENOENT;
    return -1;
}

int dir_findnext(const char *const path, dir_t *dir)
{
    (void)path;
    (void)dir;
    errno = ENOENT;
    return -1;
}

int dir_findclose(const char *const path, dir_t *dir)
{
    (void)path;
    (void)dir;
    return 0;
}
#endif
