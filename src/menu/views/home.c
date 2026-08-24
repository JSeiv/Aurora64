#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
#include "../library/library_metrics.h"
#include "../library/library_service.h"
#endif
#include "../sound.h"
#include "../ui_components/constants.h"
#include "views.h"

#define HOME_CARD_COUNT 6
#define HOME_COLUMNS 3
#define HOME_ROWS 2
#define HOME_CARD_WIDTH 168
#define HOME_CARD_HEIGHT 132
#define HOME_CARD_GAP_X 16
#define HOME_CARD_GAP_Y 16
#define HOME_CARD_X 52
#define HOME_CARD_Y 82
#define HOME_CARD_PADDING_X 12
#define HOME_CARD_PADDING_Y 12
#define HOME_TEXT_OFFSET_Y 1
#define HOME_FOCUS_WIDTH 4
#define HOME_FOCUS_INSET_Y 8

static const char *const home_labels[HOME_CARD_COUNT] = {
    "Browse Files",
    "Continue Playing\n(Placeholder)",
    "Favorites\n(Placeholder)",
    "Pearl's Games\n(Placeholder)",
    "Play Together\n(Placeholder)",
#if FEATURE_AURORA_HOME_ENABLED && FEATURE_AURORA_LIBRARY_ENABLED
    "All Games",
#else
    "All Games\n(Placeholder)",
#endif
};

#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
static void draw_layer1_overlay(void)
{
    library_metrics_overlay_t overlay;
    rdpq_textparms_t parms = {
        .width = 536, .height = 12, .align = ALIGN_LEFT,
        .valign = VALIGN_TOP, .wrap = WRAP_NONE
    };
    size_t row;

    if (library_metrics_critical_interval_active()) return;
    library_metrics_heap_sample_current(LIBRARY_METRICS_HEAP_HOME);
    if (!library_metrics_format_overlay(
            LIBRARY_METRICS_OVERLAY_HOME, &overlay)) return;
    for (row = 0U; row < LIBRARY_METRICS_OVERLAY_ROWS; ++row) {
        if (overlay.rows[row][0] == '\0') continue;
        rdpq_text_print(&parms, FNT_DEFAULT, 52,
                        370 + (int)row * 12, overlay.rows[row]);
    }
}
#endif

static void process (menu_t *menu) {
    int row = menu->home.selected / HOME_COLUMNS;
    int column = menu->home.selected % HOME_COLUMNS;
    bool selection_changed = false;

    if (menu->actions.go_up && row > 0) {
        menu->home.selected -= HOME_COLUMNS;
        selection_changed = true;
    } else if (menu->actions.go_down && row < HOME_ROWS - 1) {
        menu->home.selected += HOME_COLUMNS;
        selection_changed = true;
    } else if (menu->actions.go_left && column > 0) {
        menu->home.selected--;
        selection_changed = true;
    } else if (menu->actions.go_right && column < HOME_COLUMNS - 1) {
        menu->home.selected++;
        selection_changed = true;
    } else if (menu->actions.enter) {
        if (menu->home.selected == 0) {
            sound_play_effect(SFX_ENTER);
            menu->next_mode = MENU_MODE_BROWSER;
        }
#if FEATURE_AURORA_HOME_ENABLED && FEATURE_AURORA_LIBRARY_ENABLED
        else if (menu->home.selected == 5) {
#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
            library_metrics_snapshot_t summary;
            library_service_layer1_summary(
                menu->library_service, &summary);
            (void)library_metrics_trace_begin(
                summary.published_generation);
#endif
            sound_play_effect(SFX_ENTER);
            menu->next_mode = MENU_MODE_LIBRARY;
        }
#endif
    }

    if (selection_changed) {
        sound_play_effect(SFX_CURSOR);
    }
}

static void draw (menu_t *menu, surface_t *display) {
    char *action_text = "Placeholder";

    if (menu->home.selected == 0) {
        action_text = "A: Browse files";
    }
#if FEATURE_AURORA_HOME_ENABLED && FEATURE_AURORA_LIBRARY_ENABLED
    else if (menu->home.selected == 5) {
        action_text = "A: Open All Games";
    }
#endif

    rdpq_attach(display, NULL);

    ui_components_background_draw();
    ui_components_layout_draw();

    for (int i = 0; i < HOME_CARD_COUNT; i++) {
        int row = i / HOME_COLUMNS;
        int column = i % HOME_COLUMNS;
        int outer_x0 = HOME_CARD_X + column * (HOME_CARD_WIDTH + HOME_CARD_GAP_X);
        int outer_y0 = HOME_CARD_Y + row * (HOME_CARD_HEIGHT + HOME_CARD_GAP_Y);
        int outer_x1 = outer_x0 + HOME_CARD_WIDTH;
        int outer_y1 = outer_y0 + HOME_CARD_HEIGHT;
        int content_x0 = outer_x0 + BORDER_THICKNESS;
        int content_y0 = outer_y0 + BORDER_THICKNESS;
        int content_x1 = outer_x1 - BORDER_THICKNESS;
        int content_y1 = outer_y1 - BORDER_THICKNESS;
        rdpq_textparms_t text_parms = {
            .width = HOME_CARD_WIDTH - (2 * BORDER_THICKNESS) - (2 * HOME_CARD_PADDING_X),
            .height = HOME_CARD_HEIGHT - (2 * BORDER_THICKNESS) - (2 * HOME_CARD_PADDING_Y) - HOME_TEXT_OFFSET_Y,
            .align = ALIGN_CENTER,
            .valign = VALIGN_CENTER,
            .wrap = WRAP_WORD,
        };
        int text_x = content_x0 + HOME_CARD_PADDING_X;
        int text_y = content_y0 + HOME_CARD_PADDING_Y + HOME_TEXT_OFFSET_Y;

        ui_components_box_draw(
            content_x0,
            content_y0,
            content_x1,
            content_y1,
            i == menu->home.selected ? TAB_ACTIVE_BACKGROUND_COLOR : TAB_INACTIVE_BACKGROUND_COLOR
        );
        ui_components_border_draw(content_x0, content_y0, content_x1, content_y1);

        if (i == menu->home.selected) {
            ui_components_box_draw(
                content_x0,
                content_y0 + HOME_FOCUS_INSET_Y,
                content_x0 + HOME_FOCUS_WIDTH,
                content_y1 - HOME_FOCUS_INSET_Y,
                BORDER_COLOR
            );
        }

        rdpq_text_print(&text_parms, FNT_DEFAULT, text_x, text_y, home_labels[i]);
    }

#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
    draw_layer1_overlay();
#endif

    ui_components_actions_bar_text_draw(
        STL_DEFAULT, ALIGN_LEFT, VALIGN_TOP, action_text
    );

    rdpq_detach_show();
}

void view_home_init (menu_t *menu) {
    if (menu->home.selected < 0 || menu->home.selected >= HOME_CARD_COUNT) {
        menu->home.selected = 0;
    }
}

void view_home_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);
}
