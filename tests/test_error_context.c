#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "acutest.h"
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#endif
#include "menu/menu_state.h"
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <string.h>
#include "menu/cart_load.h"
#include "menu/library/library_fs.h"
#include "menu/library/rom_header.h"

void menu_show_error(menu_t *menu, char *error_message);
void menu_show_error_context(menu_t *menu, char *error_message,
                             menu_mode_t return_mode,
                             const rom_fingerprint_t *fingerprint,
                             int32_t page_anchor);
void view_error_host_press_back(menu_t *menu);
void error_context_host_set_library_enabled(bool enabled);
void library_service_resume(library_service_t *service) { (void)service; }
void view_load_rom_set_pending_path(menu_t *menu, path_t *rom_path,
                                    menu_mode_t return_mode);
void view_load_rom_init(menu_t *menu);
void view_load_rom_display(menu_t *menu, void *display);
void view_load_rom_host_set_validation_fs(
    const library_fs_t *fs,
    int (*seek_callback)(void *context, void *handle, uint64_t offset),
    const library_source_t *source);
void view_load_rom_host_set_cart_load(cart_load_err_t (*callback)(menu_t *menu));

enum { FIXTURE_CAPACITY = 131072U, FIXTURE_SIZE = 5004U };
typedef struct {
    uint8_t bytes[FIXTURE_CAPACITY];
    uint8_t replacement[FIXTURE_CAPACITY];
    size_t length;
    size_t offset;
    uint64_t size;
    int64_t mtime;
    library_fs_entry_type_t type;
    uint64_t change_token;
    int stat_calls;
    int open_calls;
    int successful_opens;
    int seek_calls;
    int read_calls;
    int close_calls;
    int open_handles;
    int max_open_handles;
    size_t total_read_bytes;
    size_t total_requested_bytes;
    int fail_stat_call;
    int fail_open_call;
    int fail_seek_call;
    int fail_read_call;
    int short_read_call;
    int fail_close_call;
    int change_token_on_stat;
    bool use_replacement;
} load_rom_fs_t;

static int launch_count;
static cart_load_err_t launch_result;
static rom_err_t config_result;
static size_t max_observed_validation_bytes;

static int load_rom_stat(void *context, const char *path, library_stat_t *out)
{
    load_rom_fs_t *state = context;
    (void)path;
    ++state->stat_calls;
    if (state->fail_stat_call == state->stat_calls) return LIBRARY_FS_ERROR;
    memset(out, 0, sizeof(*out));
    out->type = state->type;
    out->size = state->size;
    out->modified_time = state->mtime;
    out->change_token = state->change_token +
        (state->change_token_on_stat == state->stat_calls ? 1U : 0U);
    return LIBRARY_FS_ENTRY;
}

static int load_rom_open(void *context, const char *path, void **handle)
{
    load_rom_fs_t *state = context;
    (void)path;
    ++state->open_calls;
    if (state->fail_open_call == state->open_calls) {
        *handle = NULL;
        return LIBRARY_FS_ERROR;
    }
    ++state->successful_opens;
    ++state->open_handles;
    if (state->open_handles > state->max_open_handles)
        state->max_open_handles = state->open_handles;
    state->offset = 0U;
    *handle = state;
    return LIBRARY_FS_ENTRY;
}

static int load_rom_seek(void *context, void *handle, uint64_t offset)
{
    load_rom_fs_t *state = context;
    (void)handle;
    ++state->seek_calls;
    if (state->fail_seek_call == state->seek_calls || offset > state->length)
        return LIBRARY_FS_ERROR;
    state->offset = (size_t)offset;
    return LIBRARY_FS_ENTRY;
}

static int64_t load_rom_read(void *context, void *handle, void *buffer,
                             size_t length)
{
    load_rom_fs_t *state = context;
    const uint8_t *bytes = state->use_replacement ? state->replacement : state->bytes;
    size_t amount = length;
    (void)handle;
    ++state->read_calls;
    state->total_requested_bytes += length;
    if (state->fail_read_call == state->read_calls) return LIBRARY_FS_ERROR;
    if (state->offset > state->length) return LIBRARY_FS_ERROR;
    if (amount > state->length - state->offset)
        amount = state->length - state->offset;
    if (state->short_read_call == state->read_calls && amount != 0U) --amount;
    if (amount != 0U) memcpy(buffer, bytes + state->offset, amount);
    state->offset += amount;
    state->total_read_bytes += amount;
    if (state->total_read_bytes > max_observed_validation_bytes)
        max_observed_validation_bytes = state->total_read_bytes;
    return (int64_t)amount;
}

static int load_rom_close(void *context, void *handle)
{
    load_rom_fs_t *state = context;
    (void)handle;
    ++state->close_calls;
    --state->open_handles;
    return state->fail_close_call == state->close_calls ? LIBRARY_FS_ERROR : 0;
}

static library_fs_t load_rom_fs_api(load_rom_fs_t *state)
{
    library_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.context = state;
    fs.file_open_read = load_rom_open;
    fs.file_read = load_rom_read;
    fs.file_close = load_rom_close;
    fs.stat = load_rom_stat;
    return fs;
}

static size_t raw_index(rom_byte_order_t order, size_t canonical_index)
{
    if (order == ROM_BYTE_ORDER_V64) return canonical_index ^ 1U;
    if (order == ROM_BYTE_ORDER_N64) return canonical_index ^ 3U;
    return canonical_index;
}

static uint8_t canonical_value(size_t index, uint8_t fill)
{
    static const uint8_t magic[4] = { 0x80U, 0x37U, 0x12U, 0x40U };
    if (index < sizeof(magic)) return magic[index];
    return (uint8_t)(fill + (uint8_t)(index * 37U));
}

static void fixture_bytes(uint8_t *bytes, size_t length, uint8_t fill,
                          rom_byte_order_t order)
{
    size_t index;
    memset(bytes, 0, length);
    for (index = 0U; index < length; ++index)
        bytes[raw_index(order, index)] = canonical_value(index, fill);
}

static void fixture_init_size(load_rom_fs_t *state, uint8_t fill, size_t length,
                              rom_byte_order_t order)
{
    memset(state, 0, sizeof(*state));
    TEST_ASSERT(length <= FIXTURE_CAPACITY);
    TEST_ASSERT(length % (order == ROM_BYTE_ORDER_N64 ? 4U :
                          order == ROM_BYTE_ORDER_V64 ? 2U : 1U) == 0U);
    fixture_bytes(state->bytes, length, fill, order);
    memcpy(state->replacement, state->bytes, length);
    state->replacement[raw_index(order, length / 2U + 7U)] ^= 0x5AU;
    state->length = length;
    state->size = length;
    state->mtime = 12345;
    state->type = LIBRARY_FS_ENTRY_FILE;
}

static void fixture_init(load_rom_fs_t *state, uint8_t fill)
{
    fixture_init_size(state, fill, FIXTURE_SIZE, ROM_BYTE_ORDER_Z64);
}

static uint32_t crc32_byte(uint32_t crc, uint8_t value)
{
    unsigned int bit;
    crc ^= value;
    for (bit = 0U; bit < 8U; ++bit)
        crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320U : 0U);
    return crc;
}

static library_source_t source_for(const load_rom_fs_t *state,
                                   rom_byte_order_t order)
{
    library_source_t source;
    uint64_t index;
    uint64_t middle = state->size / 2U;
    uint32_t header_crc = 0xffffffffU;
    uint32_t sample_crc = 0xffffffffU;
    memset(&source, 0, sizeof(source));
    source.size = state->size;
    source.mtime_seconds = state->mtime;
    source.byte_order = order;
    for (index = 0U; index < state->size; ++index) {
        uint8_t value = state->bytes[raw_index(order, (size_t)index)];
        if (index < ROM_HEADER_METADATA_BYTES)
            header_crc = crc32_byte(header_crc, value);
        if (index < 64U || (index >= middle && index - middle < 64U) ||
            index >= state->size - 64U)
            sample_crc = crc32_byte(sample_crc, value);
    }
    source.normalized_header_crc32 = header_crc ^ 0xffffffffU;
    source.normalized_sample_crc32 = sample_crc ^ 0xffffffffU;
    return source;
}

static cart_load_err_t stage_rom(menu_t *menu)
{
    (void)menu;
    ++launch_count;
    return launch_result;
}

rom_err_t rom_config_load(path_t *path, rom_info_t *rom_info)
{
    (void)path;
    memset(rom_info, 0, sizeof(*rom_info));
    return config_result;
}

void rom_info_free_meta(rom_info_t *rom_info) { (void)rom_info; }
char *cart_load_convert_error_message(cart_load_err_t err)
{
    (void)err;
    return "cart load failed";
}

static rom_fingerprint_t fingerprint(uint8_t value);

static menu_t load_rom_menu(const rom_fingerprint_t *expected)
{
    menu_t menu;
    memset(&menu, 0, sizeof(menu));
    menu.mode = MENU_MODE_LOAD_ROM;
    menu.next_mode = MENU_MODE_LOAD_ROM;
    menu.library_view.selected_valid = true;
    menu.library_view.selected_fingerprint = *expected;
    menu.library_view.pending_fingerprint = *expected;
    menu.library_view.visual_offset = 0U;
    return menu;
}

static void setup_library_order(load_rom_fs_t *state, library_fs_t *fs,
                                menu_t *menu, rom_byte_order_t order)
{
    rom_fingerprint_t expected = fingerprint(0xA1U);
    library_source_t source = source_for(state, order);
    *fs = load_rom_fs_api(state);
    *menu = load_rom_menu(&expected);
    launch_count = 0;
    launch_result = CART_LOAD_OK;
    config_result = ROM_OK;
    view_load_rom_host_set_validation_fs(fs, load_rom_seek, &source);
    view_load_rom_host_set_cart_load(stage_rom);
    view_load_rom_set_pending_path(menu, path_init("", "/game.z64"),
                                   MENU_MODE_LIBRARY);
}

static void setup_library(load_rom_fs_t *state, library_fs_t *fs, menu_t *menu)
{
    setup_library_order(state, fs, menu, ROM_BYTE_ORDER_Z64);
}

static void press_enter(menu_t *menu)
{
    menu->actions.enter = true;
    view_load_rom_display(menu, NULL);
    menu->actions.enter = false;
}

static void expect_validation_failure(menu_t *menu)
{
    TEST_CHECK(launch_count == 0);
    TEST_CHECK(menu->next_mode == MENU_MODE_ERROR);
    TEST_CHECK(menu->load.rom_path == NULL);
    TEST_CHECK(menu->load.pending_rom_path == NULL);
    TEST_CHECK(!menu->load.expected_fingerprint_valid);
}

void test_load_rom_sampled_and_header_replacements_are_rejected(void)
{
    load_rom_fs_t state; library_fs_t fs; menu_t menu;
    fixture_init(&state, 0x11U);
    setup_library(&state, &fs, &menu);
    state.use_replacement = true;
    view_load_rom_init(&menu);
    expect_validation_failure(&menu);
    TEST_CHECK(state.total_read_bytes == 192U);

    fixture_init(&state, 0x12U);
    setup_library(&state, &fs, &menu);
    memcpy(state.replacement, state.bytes, state.length);
    state.replacement[20] ^= 0x33U;
    state.use_replacement = true;
    view_load_rom_init(&menu);
    expect_validation_failure(&menu);
}

void test_load_rom_missing_path_fails_before_stage(void)
{
    load_rom_fs_t state; library_fs_t fs; menu_t menu;
    fixture_init(&state, 0x21U);
    setup_library(&state, &fs, &menu);
    view_load_rom_set_pending_path(&menu, NULL, MENU_MODE_LIBRARY);
    view_load_rom_init(&menu);
    TEST_CHECK(launch_count == 0);
    TEST_CHECK(menu.next_mode == MENU_MODE_ERROR);
    TEST_CHECK(menu.load.rom_path == NULL);
    TEST_CHECK(menu.load.pending_rom_path == NULL);
    TEST_CHECK(state.open_calls == 0);
}

void test_load_rom_replacement_after_details_is_rejected(void)
{
    load_rom_fs_t state; library_fs_t fs; menu_t menu;
    fixture_init(&state, 0x33U);
    setup_library(&state, &fs, &menu);
    view_load_rom_init(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_ROM);
    TEST_CHECK(state.open_calls == 1);
    state.use_replacement = true;
    press_enter(&menu);
    expect_validation_failure(&menu);
    TEST_CHECK(state.open_calls == 2);
    TEST_CHECK(state.successful_opens == state.close_calls);
}

void test_load_rom_all_validation_failures_suppress_stage(void)
{
    int scenario;
    for (scenario = 0; scenario < 11; ++scenario) {
        load_rom_fs_t state; library_fs_t fs; menu_t menu;
        fixture_init(&state, (uint8_t)(0x40 + scenario));
        setup_library(&state, &fs, &menu);
        if (scenario == 0) state.fail_stat_call = 1;
        if (scenario == 1) state.fail_open_call = 1;
        if (scenario == 2) state.fail_seek_call = 1;
        if (scenario == 3) state.fail_read_call = 1;
        if (scenario == 4) state.short_read_call = 1;
        if (scenario == 5) memset(state.bytes, 0, 4U);
        if (scenario == 6) {
            library_source_t source = source_for(&state, ROM_BYTE_ORDER_Z64);
            source.normalized_header_crc32 ^= 1U;
            view_load_rom_host_set_validation_fs(&fs, load_rom_seek, &source);
        }
        if (scenario == 7) {
            library_source_t source = source_for(&state, ROM_BYTE_ORDER_Z64);
            source.normalized_sample_crc32 ^= 1U;
            view_load_rom_host_set_validation_fs(&fs, load_rom_seek, &source);
        }
        if (scenario == 8) state.change_token_on_stat = 2;
        if (scenario == 9) state.fail_close_call = 1;
        if (scenario == 10) state.type = LIBRARY_FS_ENTRY_DIRECTORY;
        view_load_rom_init(&menu);
        expect_validation_failure(&menu);
        TEST_CHECK(state.successful_opens == state.close_calls);
        TEST_CHECK(state.open_handles == 0);
        TEST_CHECK(state.max_open_handles <= 1);
    }
}

void test_load_rom_success_is_bounded_and_stages_once(void)
{
    load_rom_fs_t state; library_fs_t fs; menu_t menu;
    fixture_init(&state, 0x61U);
    setup_library(&state, &fs, &menu);
    view_load_rom_init(&menu);
    press_enter(&menu);
    TEST_CHECK(launch_count == 1);
    TEST_CHECK(menu.next_mode == MENU_MODE_BOOT);
    TEST_CHECK(menu.load.rom_path != NULL);
    TEST_CHECK(state.stat_calls == 4);
    TEST_CHECK(state.open_calls == 2);
    TEST_CHECK(state.seek_calls == 6);
    TEST_CHECK(state.read_calls == 6);
    TEST_CHECK(state.close_calls == 2);
    TEST_CHECK(state.total_read_bytes == 384U);
    TEST_CHECK(state.total_requested_bytes == 384U);
    TEST_CHECK(state.open_handles == 0);
    path_free(menu.load.rom_path);
    menu.load.rom_path = NULL;
}

void test_load_rom_read_bound_is_independent_of_rom_size_and_order(void)
{
    load_rom_fs_t small; load_rom_fs_t large; load_rom_fs_t n64;
    library_fs_t fs; menu_t menu;
    max_observed_validation_bytes = 0U;
    fixture_init_size(&small, 0x71U, FIXTURE_SIZE, ROM_BYTE_ORDER_Z64);
    setup_library_order(&small, &fs, &menu, ROM_BYTE_ORDER_Z64);
    view_load_rom_init(&menu);
    TEST_CHECK(small.total_read_bytes == 192U);
    TEST_CHECK(small.total_read_bytes < small.length);
    path_free(menu.load.rom_path); menu.load.rom_path = NULL;

    fixture_init_size(&large, 0x72U, FIXTURE_CAPACITY, ROM_BYTE_ORDER_Z64);
    setup_library_order(&large, &fs, &menu, ROM_BYTE_ORDER_Z64);
    view_load_rom_init(&menu);
    TEST_CHECK(large.total_read_bytes == 192U);
    TEST_CHECK(large.total_read_bytes < large.length);
    path_free(menu.load.rom_path); menu.load.rom_path = NULL;

    fixture_init_size(&n64, 0x73U, FIXTURE_SIZE, ROM_BYTE_ORDER_N64);
    setup_library_order(&n64, &fs, &menu, ROM_BYTE_ORDER_N64);
    view_load_rom_init(&menu);
    TEST_CHECK(n64.total_read_bytes == 196U);
    TEST_CHECK(n64.total_read_bytes <= 200U);
    TEST_CHECK(n64.total_read_bytes < n64.length);
    TEST_CHECK(max_observed_validation_bytes == 196U);
    path_free(menu.load.rom_path); menu.load.rom_path = NULL;
}

void test_load_rom_cart_failure_and_browser_routes(void)
{
    load_rom_fs_t state; library_fs_t fs; menu_t menu;
    fixture_init(&state, 0x81U);
    setup_library(&state, &fs, &menu);
    launch_result = CART_LOAD_ERR_ROM_LOAD_FAIL;
    view_load_rom_init(&menu);
    press_enter(&menu);
    TEST_CHECK(launch_count == 1);
    TEST_CHECK(menu.next_mode == MENU_MODE_ERROR);
    TEST_CHECK(menu.error_context.valid);
    TEST_CHECK(menu.load.rom_path == NULL);
    TEST_CHECK(state.successful_opens == state.close_calls);

    fixture_init(&state, 0x82U);
    setup_library(&state, &fs, &menu);
    view_load_rom_set_pending_path(&menu, path_init("", "/browser.z64"),
                                   MENU_MODE_BROWSER);
    launch_result = CART_LOAD_ERR_ROM_LOAD_FAIL;
    view_load_rom_init(&menu);
    press_enter(&menu);
    TEST_CHECK(launch_count == 1);
    TEST_CHECK(menu.next_mode == MENU_MODE_ERROR);
    TEST_CHECK(!menu.error_context.valid);
    TEST_CHECK(menu.load.rom_path == NULL);
    TEST_CHECK(state.open_calls == 0);
}

void test_load_rom_repeated_failures_leak_no_paths_or_handles(void)
{
    uint8_t cycle;
    for (cycle = 0U; cycle < 32U; ++cycle) {
        load_rom_fs_t state; library_fs_t fs; menu_t menu;
        fixture_init(&state, (uint8_t)(cycle + 1U));
        setup_library(&state, &fs, &menu);
        state.fail_read_call = 2;
        view_load_rom_init(&menu);
        TEST_CHECK(menu.load.rom_path == NULL);
        TEST_CHECK(menu.load.pending_rom_path == NULL);
        TEST_CHECK(state.successful_opens == 1);
        TEST_CHECK(state.close_calls == 1);
        TEST_CHECK(state.open_handles == 0);
    }
}

static rom_fingerprint_t fingerprint(uint8_t value)
{
    rom_fingerprint_t result = { { 0U } };
    result.bytes[0] = value;
    result.bytes[31] = (uint8_t)(value ^ 0xA5U);
    return result;
}

static menu_t clean_menu(void)
{
    menu_t menu;
    memset(&menu, 0, sizeof(menu));
    menu.mode = MENU_MODE_ERROR;
    menu.next_mode = MENU_MODE_ERROR;
    return menu;
}

static void back(menu_t *menu)
{
    menu->actions.back = true;
    view_error_host_press_back(menu);
    menu->actions.back = false;
}

void test_error_context_missing_rom_fails_closed_and_returns_to_identity(void)
{
    menu_t menu = clean_menu();
    rom_fingerprint_t expected = fingerprint(0x31U);
    menu.library_view.last_resolved_index = 14U;
    menu_show_error_context(&menu, "Couldn't open ROM file", MENU_MODE_LIBRARY,
                            &expected, 12);
    back(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    TEST_CHECK(menu.library_view.selected_valid);
    TEST_CHECK(memcmp(&menu.library_view.selected_fingerprint, &expected,
                      sizeof(expected)) == 0);
    TEST_CHECK(menu.library_view.last_resolved_index == 14U);
    TEST_CHECK(menu.library_view.visual_offset == 12U);
}

void test_error_context_absent_identity_has_deterministic_fallback(void)
{
    menu_t menu = clean_menu();
    rom_fingerprint_t missing = fingerprint(0x42U);
    menu.library_view.last_resolved_index = 14U;
    menu_show_error_context(&menu, "load failed", MENU_MODE_LIBRARY, &missing, 12);
    back(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    TEST_CHECK(menu.library_view.last_resolved_index == 14U);
    TEST_CHECK(menu.library_view.visual_offset == 12U);
}

void test_error_context_forged_stale_and_disabled_fall_back(void)
{
    static const menu_mode_t forged[] = { MENU_MODE_BROWSER, MENU_MODE_HISTORY,
        MENU_MODE_FAVORITE, MENU_MODE_LOAD_ROM, MENU_MODE_DATEL_CODE_EDITOR };
    rom_fingerprint_t selected = fingerprint(0x53U);
    size_t i;
    for (i = 0U; i < sizeof(forged) / sizeof(forged[0]); ++i) {
        menu_t menu = clean_menu();
        menu_show_error_context(&menu, "forged", forged[i], &selected, 6);
        back(&menu);
        TEST_CHECK(menu.next_mode == MENU_MODE_BROWSER);
    }
    {
        menu_t menu = clean_menu();
        menu_show_error_context(&menu, "stale", MENU_MODE_LIBRARY, NULL, 6);
        back(&menu);
        TEST_CHECK(menu.next_mode == MENU_MODE_BROWSER);
    }
    {
        menu_t menu = clean_menu();
        error_context_host_set_library_enabled(false);
        menu_show_error_context(&menu, "disabled", MENU_MODE_LIBRARY, &selected, 6);
        back(&menu);
        TEST_CHECK(menu.next_mode == MENU_MODE_BROWSER);
        error_context_host_set_library_enabled(true);
    }
}

void test_error_context_is_one_shot_and_ordinary_origins_stay_browser(void)
{
    menu_t menu = clean_menu();
    rom_fingerprint_t selected = fingerprint(0x64U);
    menu_show_error_context(&menu, "context", MENU_MODE_LIBRARY, &selected, 0);
    back(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    menu.next_mode = MENU_MODE_ERROR;
    back(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_BROWSER);
    menu = clean_menu();
    menu_show_error(&menu, "ordinary Browser/History/Favorites/autoload/Datel");
    back(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_BROWSER);
}

void test_error_context_repeated_failure_cycles_are_clean(void)
{
    menu_t menu = clean_menu();
    uint8_t cycle;
    for (cycle = 0U; cycle < 32U; ++cycle) {
        rom_fingerprint_t selected = fingerprint((uint8_t)(cycle + 1U));
        menu.next_mode = MENU_MODE_ERROR;
        menu_show_error_context(&menu, "details failure", MENU_MODE_LIBRARY,
            &selected, (int32_t)((cycle / 6U) * 6U));
        back(&menu);
        TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
        TEST_CHECK(!menu.error_context.valid);
        TEST_CHECK(menu.load.rom_path == NULL);
        TEST_CHECK(menu.load.pending_rom_path == NULL);
    }
}

TEST_LIST = {
    { "load-rom/sampled-header-replacements", test_load_rom_sampled_and_header_replacements_are_rejected },
    { "load-rom/missing-path", test_load_rom_missing_path_fails_before_stage },
    { "load-rom/replacement-after-details-immediate-check", test_load_rom_replacement_after_details_is_rejected },
    { "load-rom/all-validation-failures", test_load_rom_all_validation_failures_suppress_stage },
    { "load-rom/success-bounded-stage-once", test_load_rom_success_is_bounded_and_stages_once },
    { "load-rom/fixed-bound-not-full-traversal", test_load_rom_read_bound_is_independent_of_rom_size_and_order },
    { "load-rom/cart-failure-browser-unchanged", test_load_rom_cart_failure_and_browser_routes },
    { "load-rom/repeated-failures-no-leaks", test_load_rom_repeated_failures_leak_no_paths_or_handles },
    { "error-context/missing-rom-return-identity-page", test_error_context_missing_rom_fails_closed_and_returns_to_identity },
    { "error-context/absent-identity-fallback", test_error_context_absent_identity_has_deterministic_fallback },
    { "error-context/forged-stale-disabled-browser", test_error_context_forged_stale_and_disabled_fall_back },
    { "error-context/one-shot-ordinary-browser", test_error_context_is_one_shot_and_ordinary_origins_stay_browser },
    { "error-context/repeated-failure-cycles", test_error_context_repeated_failure_cycles_are_clean },
    { NULL, NULL }
};
