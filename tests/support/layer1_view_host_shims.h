#ifndef LAYER1_VIEW_HOST_SHIMS_H__
#define LAYER1_VIEW_HOST_SHIMS_H__

#include <stdbool.h>
#include <stddef.h>
#if defined(LAYER1_VIEW_HOST_PREINCLUDE) && LAYER1_VIEW_HOST_PREINCLUDE
#ifndef __LIBDRAGON_LIBDRAGON_H
#define __LIBDRAGON_LIBDRAGON_H
#endif
#include <graphics.h>
#include <rdpq_attach.h>
#include <rdpq_text.h>
#endif
#if defined(LAYER1_VIEW_HOST_PREINCLUDE) && \
    defined(LAYER1_VIEW_HOST_REPLACE_PATH_TRY_INIT)
#define path_try_init layer1_view_host_path_try_init
#endif
void usb_comm_transition_reset(void);

typedef enum {
    LAYER1_VIEW_HOST_EVENT_ATTACH = 1,
    LAYER1_VIEW_HOST_EVENT_DETACH,
    LAYER1_VIEW_HOST_EVENT_TEXT,
    LAYER1_VIEW_HOST_EVENT_BOX,
    LAYER1_VIEW_HOST_EVENT_BORDER,
    LAYER1_VIEW_HOST_EVENT_ACTION_BAR,
    LAYER1_VIEW_HOST_EVENT_SOUND,
    LAYER1_VIEW_HOST_EVENT_USB_POLL,
    LAYER1_VIEW_HOST_EVENT_PATH_CONSTRUCT,
    LAYER1_VIEW_HOST_EVENT_PENDING_PATH
} layer1_view_host_event_t;

void layer1_view_host_reset(void);
size_t layer1_view_host_text_count(void);
const char *layer1_view_host_text_at(size_t index);
bool layer1_view_host_saw_text(const char *text);
size_t layer1_view_host_text_occurrences(const char *text);
float layer1_view_host_text_x(size_t index);
float layer1_view_host_text_y(size_t index);

size_t layer1_view_host_event_count(void);
layer1_view_host_event_t layer1_view_host_event_at(size_t index);
size_t layer1_view_host_box_count(void);
bool layer1_view_host_box_at(size_t index, int *x0, int *y0, int *x1, int *y1);

void layer1_view_host_observe_menu(void *menu);
size_t layer1_view_host_sound_calls(void);
bool layer1_view_host_sound_saw_input_trace(void);
int layer1_view_host_sound_next_mode(void);
bool layer1_view_host_attach_saw_frame_begin(void);
bool layer1_view_host_detach_saw_frame_submitted(void);

size_t layer1_view_host_usb_poll_calls(void);
size_t layer1_view_host_usb_return_count(void);
bool layer1_view_host_usb_poll_active(void);

void layer1_view_host_set_path_success(bool enabled);
size_t layer1_view_host_path_calls(void);
const char *layer1_view_host_path_prefix(void);
const char *layer1_view_host_path_logical(void);
size_t layer1_view_host_path_live_count(void);
size_t layer1_view_host_path_free_count(void);
bool layer1_view_host_pending_path_matches(const void *owner,
                                           const char *full_path,
                                           int return_mode);

#endif
