#ifdef ERROR_CONTEXT_HOST_TEST
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "../menu_state.h"
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif
#else
#include "views.h"
#include "../sound.h"
#endif

#include <string.h>

#ifdef ERROR_CONTEXT_HOST_TEST
static bool host_library_enabled = true;

void error_context_host_set_library_enabled(bool enabled)
{
    host_library_enabled = enabled;
}
#endif

static bool library_return_enabled(void)
{
#ifdef ERROR_CONTEXT_HOST_TEST
    return host_library_enabled;
#elif FEATURE_AURORA_HOME_ENABLED && FEATURE_AURORA_LIBRARY_ENABLED
    return true;
#else
    return false;
#endif
}

static void clear_error_context(menu_t *menu)
{
    memset(&menu->error_context, 0, sizeof(menu->error_context));
    menu->error_context.return_mode = MENU_MODE_BROWSER;
}

static void process(menu_t *menu)
{
    if (menu->actions.back) {
#ifndef ERROR_CONTEXT_HOST_TEST
        sound_play_effect(SFX_EXIT);
#endif
        if (library_return_enabled() && menu->error_context.valid &&
            menu->error_context.return_mode == MENU_MODE_LIBRARY &&
            menu->error_context.fingerprint_valid &&
            menu->error_context.page_anchor >= 0 &&
            menu->error_context.page_anchor % 6 == 0) {
            rom_fingerprint_t fingerprint = menu->error_context.fingerprint;
            uint32_t anchor = (uint32_t)menu->error_context.page_anchor;

            clear_error_context(menu);
            menu->library_view.selected_fingerprint = fingerprint;
            menu->library_view.selected_valid = true;
            /* Page and prior selected index are distinct. Reconciliation owns
             * index fallback and must retain the pre-error selected index. */
            menu->library_view.visual_offset = anchor;
            menu->next_mode = MENU_MODE_LIBRARY;
            if (menu->library_service != NULL)
                library_service_resume(menu->library_service);
        } else {
            clear_error_context(menu);
            menu->next_mode = MENU_MODE_BROWSER;
        }
    }
}

#ifndef ERROR_CONTEXT_HOST_TEST
static void draw(menu_t *menu, surface_t *d)
{
    rdpq_attach(d, NULL);
    ui_components_background_draw();
    if (menu->error_message)
        ui_components_messagebox_draw(menu->error_message);
    else
        ui_components_messagebox_draw("Unspecified error");
    rdpq_detach_show();
}
#endif

static void deinit(menu_t *menu)
{
    menu->error_message = NULL;
    menu->flashcart_err = FLASHCART_OK;
}

void view_error_init(menu_t *menu)
{
#ifndef ERROR_CONTEXT_HOST_TEST
    if (menu->flashcart_err != FLASHCART_OK) {
        debugf("Flashcart error [%d]: %s\n", menu->flashcart_err,
               flashcart_convert_error_message(menu->flashcart_err));
    }
#else
    (void)menu;
#endif
}

#ifndef ERROR_CONTEXT_HOST_TEST
void view_error_display(menu_t *menu, surface_t *display)
{
    process(menu);
    draw(menu, display);
    if (menu->next_mode != MENU_MODE_ERROR) deinit(menu);
}
#else
void view_error_host_press_back(menu_t *menu)
{
    process(menu);
    if (menu->next_mode != MENU_MODE_ERROR) deinit(menu);
}
#endif

void menu_show_error(menu_t *menu, char *error_message)
{
#ifndef ERROR_CONTEXT_HOST_TEST
    sound_play_effect(SFX_ERROR);
#endif
    clear_error_context(menu);
    menu->next_mode = MENU_MODE_ERROR;
    menu->error_message = error_message;
}

void menu_show_error_context(menu_t *menu, char *error_message,
                             menu_mode_t return_mode,
                             const rom_fingerprint_t *fingerprint,
                             int32_t page_anchor)
{
    menu_show_error(menu, error_message);
    if (!library_return_enabled() || return_mode != MENU_MODE_LIBRARY ||
        fingerprint == NULL || page_anchor < 0 || page_anchor % 6 != 0) return;

    menu->error_context.valid = true;
    menu->error_context.return_mode = MENU_MODE_LIBRARY;
    menu->error_context.fingerprint_valid = true;
    menu->error_context.fingerprint = *fingerprint;
    menu->error_context.page_anchor = page_anchor;
}
