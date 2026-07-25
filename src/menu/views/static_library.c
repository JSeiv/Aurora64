#include "../sound.h"
#include "../ui_components/constants.h"
#include "views.h"

#define STATIC_LIBRARY_COLUMNS 3
#define STATIC_LIBRARY_CARD_WIDTH 168
#define STATIC_LIBRARY_CARD_HEIGHT 132
#define STATIC_LIBRARY_CARD_GAP_X 16
#define STATIC_LIBRARY_CARD_GAP_Y 16
#define STATIC_LIBRARY_CARD_X 52
#define STATIC_LIBRARY_CARD_Y 82
#define STATIC_LIBRARY_CARD_PADDING_X 12
#define STATIC_LIBRARY_CARD_PADDING_Y 12
#define STATIC_LIBRARY_TEXT_OFFSET_Y 1
#define STATIC_LIBRARY_FOCUS_WIDTH 4
#define STATIC_LIBRARY_FOCUS_INSET_Y 8

typedef struct {
    const char *label;
    const char *relative_path;
} static_library_fixture_t;

/* TEMPORARY AURORA64 LAUNCH-PROOF FIXTURES */
static const static_library_fixture_t static_library_fixtures[] = {
    { "Banjo-Kazooie", "/⭐ Favorties/Banjo-Kazooie (U) (!).v64" },
    { "The Legend of Zelda: Ocarina of Time", "/⭐ Favorties/Legend of Zelda, The - Ocarina of Time (U) (V1.2) [!].v64" },
    { "Mario Kart 64", "/⭐ Favorties/Mario Kart 64 (U) [!].z64" },
    { "Tony Hawk's Pro Skater 2", "/⭐ Favorties/Tony Hawk's Pro Skater 2 (U) [!].z64" },
    { "Mario Party 3", "/Nintendo/Mario Party 3 (U) [!].z64" },
    { "Paper Mario", "/Nintendo/Paper Mario (U) [!].z64" },
};

enum {
    STATIC_LIBRARY_FIXTURE_COUNT = sizeof(static_library_fixtures) / sizeof(static_library_fixtures[0]),
};

_Static_assert(
    STATIC_LIBRARY_FIXTURE_COUNT == 6,
    "static library fixture count mismatch"
);

static void process (menu_t *menu) {
    int selected = menu->static_library.selected;
    int column = selected % STATIC_LIBRARY_COLUMNS;
    bool selection_changed = false;
    bool direction_pressed =
        menu->actions.go_up || menu->actions.go_down ||
        menu->actions.go_left || menu->actions.go_right;

    if (menu->actions.go_up && selected >= STATIC_LIBRARY_COLUMNS) {
        menu->static_library.selected -= STATIC_LIBRARY_COLUMNS;
        selection_changed = true;
    } else if (
        menu->actions.go_down && selected + STATIC_LIBRARY_COLUMNS < STATIC_LIBRARY_FIXTURE_COUNT
    ) {
        menu->static_library.selected += STATIC_LIBRARY_COLUMNS;
        selection_changed = true;
    } else if (menu->actions.go_left && column > 0) {
        menu->static_library.selected--;
        selection_changed = true;
    } else if (
        menu->actions.go_right && column < STATIC_LIBRARY_COLUMNS - 1 &&
        selected + 1 < STATIC_LIBRARY_FIXTURE_COUNT
    ) {
        menu->static_library.selected++;
        selection_changed = true;
    } else if (!direction_pressed && menu->actions.enter) {
        const static_library_fixture_t fixture = static_library_fixtures[selected];
        path_t *path = path_init(menu->storage_prefix, (char *)fixture.relative_path);
        view_load_rom_set_pending_path(menu, path, MENU_MODE_STATIC_LIBRARY);
        menu->next_mode = MENU_MODE_LOAD_ROM;
        sound_play_effect(SFX_ENTER);
    } else if (!direction_pressed && menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_HOME;
    }

    if (selection_changed) {
        sound_play_effect(SFX_CURSOR);
    }
}

static void draw (menu_t *menu, surface_t *display) {
    rdpq_attach(display, NULL);

    ui_components_background_draw();
    ui_components_layout_draw();

    for (int i = 0; i < STATIC_LIBRARY_FIXTURE_COUNT; i++) {
        int row = i / STATIC_LIBRARY_COLUMNS;
        int column = i % STATIC_LIBRARY_COLUMNS;
        int outer_x0 = STATIC_LIBRARY_CARD_X + column * (STATIC_LIBRARY_CARD_WIDTH + STATIC_LIBRARY_CARD_GAP_X);
        int outer_y0 = STATIC_LIBRARY_CARD_Y + row * (STATIC_LIBRARY_CARD_HEIGHT + STATIC_LIBRARY_CARD_GAP_Y);
        int outer_x1 = outer_x0 + STATIC_LIBRARY_CARD_WIDTH;
        int outer_y1 = outer_y0 + STATIC_LIBRARY_CARD_HEIGHT;
        int content_x0 = outer_x0 + BORDER_THICKNESS;
        int content_y0 = outer_y0 + BORDER_THICKNESS;
        int content_x1 = outer_x1 - BORDER_THICKNESS;
        int content_y1 = outer_y1 - BORDER_THICKNESS;
        rdpq_textparms_t text_parms = {
            .width = STATIC_LIBRARY_CARD_WIDTH - (2 * BORDER_THICKNESS) - (2 * STATIC_LIBRARY_CARD_PADDING_X),
            .height = STATIC_LIBRARY_CARD_HEIGHT - (2 * BORDER_THICKNESS) - (2 * STATIC_LIBRARY_CARD_PADDING_Y) - STATIC_LIBRARY_TEXT_OFFSET_Y,
            .align = ALIGN_CENTER,
            .valign = VALIGN_CENTER,
            .wrap = WRAP_WORD,
        };
        int text_x = content_x0 + STATIC_LIBRARY_CARD_PADDING_X;
        int text_y = content_y0 + STATIC_LIBRARY_CARD_PADDING_Y + STATIC_LIBRARY_TEXT_OFFSET_Y;

        ui_components_box_draw(
            content_x0,
            content_y0,
            content_x1,
            content_y1,
            i == menu->static_library.selected ? TAB_ACTIVE_BACKGROUND_COLOR : TAB_INACTIVE_BACKGROUND_COLOR
        );
        ui_components_border_draw(content_x0, content_y0, content_x1, content_y1);

        if (i == menu->static_library.selected) {
            ui_components_box_draw(
                content_x0,
                content_y0 + STATIC_LIBRARY_FOCUS_INSET_Y,
                content_x0 + STATIC_LIBRARY_FOCUS_WIDTH,
                content_y1 - STATIC_LIBRARY_FOCUS_INSET_Y,
                BORDER_COLOR
            );
        }

        rdpq_text_print(&text_parms, FNT_DEFAULT, text_x, text_y, static_library_fixtures[i].label);
    }

    ui_components_actions_bar_text_draw(
        STL_DEFAULT, ALIGN_LEFT, VALIGN_TOP, "A: View details\nB: Home"
    );

    rdpq_detach_show();
}

void view_static_library_init (menu_t *menu) {
    if (
        menu->static_library.selected < 0 ||
        menu->static_library.selected >= STATIC_LIBRARY_FIXTURE_COUNT
    ) {
        menu->static_library.selected = 0;
    }
}

void view_static_library_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);
}
