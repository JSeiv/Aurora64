#if !FEATURE_AURORA_HOME_ENABLED
#error "disabled closure requires FEATURE_AURORA_HOME_ENABLED=1"
#endif
#if !FEATURE_AURORA_LIBRARY_ENABLED
#error "disabled closure requires FEATURE_AURORA_LIBRARY_ENABLED=1"
#endif
#if FEATURE_AURORA_LIBRARY_TIMING_ENABLED
#error "disabled closure requires FEATURE_AURORA_LIBRARY_TIMING_ENABLED=0"
#endif
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "acutest.h"
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#endif
#include "menu/menu_state.h"
#include "menu/views/views.h"
#include "support/layer1_view_host_shims.h"
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif
#include <string.h>
void menu_library_transition_reset(menu_t *owner);
void menu_library_coordinate_frame(menu_t *owner);
static void test_phase4_grouped_timing_disabled_production(void)
{
    menu_t menu;
    memset(&menu, 0, sizeof(menu));
    layer1_view_host_reset();
    menu_library_transition_reset(&menu);
    menu.mode = MENU_MODE_HOME;
    menu.next_mode = MENU_MODE_HOME;
    menu.home.selected = 5;
    menu.actions.enter = true;
    view_home_init(&menu);
    view_home_display(&menu, NULL);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    TEST_CHECK(layer1_view_host_sound_calls() == 1U);
    TEST_CHECK(layer1_view_host_text_occurrences("Browse Files") == 1U);
    TEST_CHECK(layer1_view_host_text_occurrences("All Games") == 1U);
    TEST_CHECK(layer1_view_host_text_occurrences("A: Open All Games") == 1U);
    TEST_CHECK(layer1_view_host_box_count() == 7U);
    TEST_CHECK(!layer1_view_host_sound_saw_input_trace());
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    TEST_CHECK(layer1_view_host_text_occurrences("[AURORA64 L1]") == 0U);
    memset(&menu.actions, 0, sizeof(menu.actions));
    menu.mode = MENU_MODE_LIBRARY;
    menu.next_mode = MENU_MODE_LIBRARY;
    view_all_games_init(&menu);
    view_all_games_display(&menu, NULL);
    TEST_CHECK(layer1_view_host_text_occurrences("Library unavailable") == 1U);
    TEST_CHECK(layer1_view_host_text_occurrences("A: View details  B: Home") == 1U);
    TEST_CHECK(layer1_view_host_text_occurrences("[AURORA64 L1]") == 0U);
    TEST_CHECK(layer1_view_host_path_live_count() == 0U);
    layer1_view_host_reset();
}
TEST_LIST = {
    { "layer1 grouped timing-disabled production", test_phase4_grouped_timing_disabled_production },
    { NULL, NULL }
};
