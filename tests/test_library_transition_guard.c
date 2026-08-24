#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#if LAYER1_FOUNDATION_FOCUSED_TESTS
#define TEST_NO_MAIN
#endif
#ifndef TASK11_STUBS_ONLY
#include "acutest.h"
#endif
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#endif
#include "menu/menu_state.h"
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "menu/cart_load.h"
#include "menu/library/library_fs.h"
#include "menu/library/rom_header.h"

#if !LAYER1_FOUNDATION_FOCUSED_TESTS
void menu_library_transition_reset(menu_t *menu);
bool menu_library_rom_cancel_started(menu_t *menu);
void menu_library_rom_cancel(menu_t *menu);
bool menu_library_rom_ready(menu_t *menu);
bool menu_library_disk_ready(menu_t *menu);
bool menu_library_emulator_ready(menu_t *menu);
bool menu_library_usb_reboot_ready(menu_t *menu);
void menu_library_handoff_begin(menu_t *menu);
bool menu_library_handoff_started(menu_t *menu);
void menu_library_action_failed(menu_t *menu);
bool menu_library_pause_to(menu_t *menu, menu_mode_t destination);
void menu_library_coordinate_frame(menu_t *menu);
bool menu_library_teardown(menu_t *menu, size_t poll_limit);

#ifdef TASK11_STUBS_ONLY
void library_service_request_pause(library_service_t *service) { (void)service; }
#ifndef TASK10_LINK
void library_service_resume(library_service_t *service) { (void)service; }
#endif
void library_service_request_cancel(library_service_t *service) { (void)service; }
void library_service_restart(library_service_t *service) { (void)service; }
bool library_service_is_quiesced(const library_service_t *service)
{ (void)service; return true; }
void library_service_poll(library_service_t *service, menu_mode_t mode)
{ (void)service; (void)mode; }
void library_service_free(library_service_t *service) { (void)service; }
bool library_service_coordinate_transition(library_service_t *service,
                                           menu_mode_t current,
                                           menu_mode_t *requested)
{ (void)service; (void)current; (void)requested; return true; }
#else

typedef struct {
    bool scanner_active;
    bool paused;
    bool pause_requested;
    bool cancel_requested;
    bool publication_pending;
    bool cache_write_pending;
    bool callback_pending;
    bool unpublished_builder;
    unsigned int open_handles;
    unsigned int in_io_callbacks;
    bool transition_pending;
    bool transition_released;
    bool resume_after_transition;
    menu_mode_t transition_destination;
    menu_mode_t released_origin;
    menu_mode_t released_destination;
    unsigned int pause_requests;
    unsigned int cancel_requests;
    unsigned int resumes;
    unsigned int restarts;
    unsigned int generations;
    unsigned int polls;
    unsigned int coordinate_calls;
    unsigned int frees;
    unsigned int builder_discards;
} fake_service_t;

static bool fake_exact_quiesced(const fake_service_t *service)
{
    return !service->pause_requested && !service->cancel_requested &&
        !service->publication_pending && !service->cache_write_pending &&
        !service->callback_pending && !service->unpublished_builder &&
        service->open_handles == 0U && service->in_io_callbacks == 0U &&
        (!service->scanner_active || service->paused);
}

void library_service_request_pause(library_service_t *opaque)
{
    fake_service_t *service = (fake_service_t *)opaque;
    ++service->pause_requests;
    service->pause_requested = !fake_exact_quiesced(service);
    if (!service->scanner_active && !service->publication_pending &&
        !service->cache_write_pending && !service->callback_pending &&
        service->open_handles == 0U && service->in_io_callbacks == 0U) {
        service->pause_requested = false;
        service->paused = true;
    }
}

void library_service_resume(library_service_t *opaque)
{
    fake_service_t *service = (fake_service_t *)opaque;
    if (!service->paused || service->cancel_requested) return;
    ++service->resumes;
    service->paused = false;
}

void library_service_request_cancel(library_service_t *opaque)
{
    fake_service_t *service = (fake_service_t *)opaque;
    ++service->cancel_requests;
    service->pause_requested = false;
    service->paused = false;
    service->cancel_requested = !fake_exact_quiesced(service) ||
        service->scanner_active || service->unpublished_builder;
}

void library_service_restart(library_service_t *opaque)
{
    fake_service_t *service = (fake_service_t *)opaque;
    ++service->restarts;
    ++service->generations;
    service->scanner_active = true;
    service->paused = false;
}

bool library_service_is_quiesced(const library_service_t *opaque)
{
    return opaque == NULL || fake_exact_quiesced((const fake_service_t *)opaque);
}

void library_service_poll(library_service_t *opaque, menu_mode_t mode)
{
    fake_service_t *service = (fake_service_t *)opaque;
    ++service->polls;
    if (service->cancel_requested) {
        if (service->unpublished_builder) {
            service->unpublished_builder = false;
            ++service->builder_discards;
        } else if (service->publication_pending) service->publication_pending = false;
        else if (service->cache_write_pending) service->cache_write_pending = false;
        else if (service->callback_pending) service->callback_pending = false;
        else if (service->in_io_callbacks != 0U) --service->in_io_callbacks;
        else if (service->open_handles != 0U) --service->open_handles;
        else {
            service->scanner_active = false;
            service->cancel_requested = false;
        }
        return;
    }
    if (service->pause_requested) {
        if (service->publication_pending) service->publication_pending = false;
        else if (service->cache_write_pending) service->cache_write_pending = false;
        else if (service->callback_pending) service->callback_pending = false;
        else if (service->in_io_callbacks != 0U) --service->in_io_callbacks;
        else if (service->open_handles != 0U) --service->open_handles;
        else {
            service->pause_requested = false;
            service->paused = true;
        }
        return;
    }
    if ((mode == MENU_MODE_HOME || mode == MENU_MODE_LIBRARY) &&
        service->resume_after_transition && service->paused) {
        service->resume_after_transition = false;
        library_service_resume(opaque);
    }
}

void library_service_free(library_service_t *opaque)
{
    ++((fake_service_t *)opaque)->frees;
}

bool library_service_coordinate_transition(library_service_t *opaque,
                                           menu_mode_t current,
                                           menu_mode_t *requested)
{
    fake_service_t *service = (fake_service_t *)opaque;
    bool safe = current == MENU_MODE_HOME || current == MENU_MODE_LIBRARY;
    if (opaque == NULL || requested == NULL) return true;
    ++service->coordinate_calls;
    if (service->transition_pending) {
        if (!library_service_is_quiesced(opaque)) {
            *requested = current;
            return false;
        }
        *requested = service->transition_destination;
        service->released_origin = current;
        service->released_destination = *requested;
        service->transition_released = true;
        service->transition_pending = false;
        service->resume_after_transition = true;
        return true;
    }
    if (service->transition_released) {
        if (current == service->released_origin &&
            *requested == service->released_destination) return true;
        if (current != service->released_origin)
            service->transition_released = false;
    }
    if (!safe || *requested == current) return true;
    service->transition_destination = *requested;
    service->transition_pending = true;
    library_service_request_pause(opaque);
    *requested = current;
    return false;
}

static void setup(menu_t *menu, fake_service_t *service, menu_mode_t mode)
{
    memset(menu, 0, sizeof(*menu));
    memset(service, 0, sizeof(*service));
    menu->mode = mode;
    menu->next_mode = mode;
    menu->library_service = (library_service_t *)service;
    service->scanner_active = true;
    menu_library_transition_reset(menu);
}

static void make_exact_quiesced(fake_service_t *service, bool paused)
{
    service->scanner_active = paused;
    service->paused = paused;
    service->pause_requested = false;
    service->cancel_requested = false;
    service->publication_pending = false;
    service->cache_write_pending = false;
    service->callback_pending = false;
    service->unpublished_builder = false;
    service->open_handles = 0U;
    service->in_io_callbacks = 0U;
}

static void test_home_browser_delays_and_resumes_later(void)
{
    menu_t menu; fake_service_t service;
    setup(&menu, &service, MENU_MODE_HOME);
    service.open_handles = 1U;
    menu.next_mode = MENU_MODE_BROWSER;
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_HOME);
    TEST_CHECK(service.pause_requests == 1U);
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(service.pause_requests == 1U);
    make_exact_quiesced(&service, true);
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_BROWSER);
    menu.mode = MENU_MODE_BROWSER;
    menu.next_mode = MENU_MODE_HOME;
    menu_library_coordinate_frame(&menu);
    menu.mode = MENU_MODE_HOME;
    library_service_poll(menu.library_service, menu.mode);
    TEST_CHECK(service.resumes == 1U);
}

static void test_home_library_and_library_details_are_coordinated(void)
{
    menu_t menu; fake_service_t service;
    setup(&menu, &service, MENU_MODE_HOME);
    service.callback_pending = true;
    menu.next_mode = MENU_MODE_LIBRARY;
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_HOME);
    make_exact_quiesced(&service, true);
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    menu.mode = MENU_MODE_LIBRARY;
    service.paused = false;
    service.callback_pending = true;
    menu.next_mode = MENU_MODE_LOAD_ROM;
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    make_exact_quiesced(&service, true);
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_ROM);
}

static void test_details_back_pauses_and_resumes_same_builder(void)
{
    menu_t menu; fake_service_t service;
    unsigned int release_resumes;

    setup(&menu, &service, MENU_MODE_LIBRARY);
    service.open_handles = 1U;
    menu.next_mode = MENU_MODE_LOAD_ROM;
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    TEST_CHECK(service.pause_requests == 1U);
    make_exact_quiesced(&service, true);
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_ROM);

    menu.mode = MENU_MODE_LOAD_ROM;
    menu.next_mode = MENU_MODE_LOAD_ROM;
    service.paused = false;
    service.scanner_active = true;
    service.open_handles = 3U;

    /* One edge-triggered B request: later frames must advance this state. */
    TEST_CHECK(!menu_library_pause_to(&menu, MENU_MODE_LIBRARY));
    TEST_CHECK(service.pause_requests == 2U);
    TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_ROM);
    TEST_CHECK(service.cancel_requests == 0U);
    TEST_CHECK(service.restarts == 0U);

    library_service_poll(menu.library_service, menu.mode);
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_ROM);
    library_service_poll(menu.library_service, menu.mode);
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_ROM);
    library_service_poll(menu.library_service, menu.mode);
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_ROM);

    library_service_poll(menu.library_service, menu.mode);
    TEST_CHECK(library_service_is_quiesced(menu.library_service));
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    TEST_CHECK(service.resumes == 1U);
    TEST_CHECK(service.builder_discards == 0U);
    TEST_CHECK(service.cancel_requests == 0U);
    TEST_CHECK(service.restarts == 0U);

    release_resumes = service.resumes;
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LIBRARY);
    TEST_CHECK(service.resumes == release_resumes);
}

static unsigned int rom_stages;
static cart_load_err_t rom_stage_result;

static cart_load_err_t stage_rom(menu_t *menu)
{
    (void)menu;
    ++rom_stages;
    return rom_stage_result;
}

rom_err_t rom_config_load(path_t *path, rom_info_t *info)
{
    (void)path;
    memset(info, 0, sizeof(*info));
    return ROM_OK;
}
void rom_info_free_meta(rom_info_t *info) { (void)info; }
char *cart_load_convert_error_message(cart_load_err_t error)
{ (void)error; return "cart load failed"; }
void menu_show_error_context(menu_t *menu, char *message,
                             menu_mode_t return_mode,
                             const rom_fingerprint_t *fingerprint,
                             int32_t page_anchor)
{
    menu->error_message = message;
    menu->next_mode = MENU_MODE_ERROR;
    menu->error_context.valid = return_mode == MENU_MODE_LIBRARY;
    menu->error_context.return_mode = return_mode;
    menu->error_context.fingerprint_valid = fingerprint != NULL;
    if (fingerprint != NULL) menu->error_context.fingerprint = *fingerprint;
    menu->error_context.page_anchor = page_anchor;
}
void menu_show_error(menu_t *menu, char *message)
{
    menu->error_message = message;
    menu->next_mode = MENU_MODE_ERROR;
}

void view_load_rom_set_pending_path(menu_t *menu, path_t *path,
                                    menu_mode_t return_mode);
void view_load_rom_init(menu_t *menu);
void view_load_rom_display(menu_t *menu, void *display);
void view_load_rom_host_set_cart_load(cart_load_err_t (*callback)(menu_t *menu));
void view_load_rom_host_set_validation_fs(
    const library_fs_t *fs,
    int (*seek_callback)(void *context, void *handle, uint64_t offset),
    const library_source_t *source);

#define ROM_BYTES 5004U
static uint8_t validation_bytes[ROM_BYTES];
typedef struct {
    size_t offset;
    size_t total_read;
    unsigned int opens;
    unsigned int closes;
    bool replacement;
} validation_state_t;
static validation_state_t validation_state;

static uint32_t crc_byte(uint32_t crc, uint8_t value)
{
    unsigned int bit;
    crc ^= value;
    for (bit = 0U; bit < 8U; ++bit)
        crc = (crc >> 1U) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    return crc;
}
static int validation_stat(void *context, const char *path, library_stat_t *out)
{
    (void)context; (void)path;
    memset(out, 0, sizeof(*out));
    out->type = LIBRARY_FS_ENTRY_FILE;
    out->size = ROM_BYTES;
    out->modified_time = 77;
    return LIBRARY_FS_ENTRY;
}
static int validation_open(void *context, const char *path, void **handle)
{
    validation_state_t *state = context;
    (void)path;
    ++state->opens;
    state->offset = 0U;
    *handle = state;
    return LIBRARY_FS_ENTRY;
}
static int validation_seek(void *context, void *handle, uint64_t offset)
{
    validation_state_t *state = context;
    (void)handle;
    if (offset > ROM_BYTES) return LIBRARY_FS_ERROR;
    state->offset = (size_t)offset;
    return LIBRARY_FS_ENTRY;
}
static int64_t validation_read(void *context, void *handle, void *buffer,
                               size_t length)
{
    validation_state_t *state = context;
    uint8_t *out = buffer;
    size_t index;
    (void)handle;
    if (state->offset + length > ROM_BYTES) return LIBRARY_FS_ERROR;
    for (index = 0U; index < length; ++index) {
        uint8_t value = validation_bytes[state->offset + index];
        if (state->replacement && state->offset + index == ROM_BYTES / 2U + 7U)
            value ^= 0x5aU;
        out[index] = value;
    }
    state->offset += length;
    state->total_read += length;
    return (int64_t)length;
}
static int validation_close(void *context, void *handle)
{
    validation_state_t *state = context;
    (void)handle;
    ++state->closes;
    return 0;
}

static void setup_rom(menu_t *menu, fake_service_t *service,
                      library_fs_t *fs, library_source_t *source)
{
    size_t index;
    uint32_t header_crc = 0xffffffffU;
    uint32_t sample_crc = 0xffffffffU;
    setup(menu, service, MENU_MODE_LOAD_ROM);
    memset(&validation_state, 0, sizeof(validation_state));
    for (index = 0U; index < ROM_BYTES; ++index)
        validation_bytes[index] = (uint8_t)(index * 37U + 11U);
    validation_bytes[0] = 0x80U; validation_bytes[1] = 0x37U;
    validation_bytes[2] = 0x12U; validation_bytes[3] = 0x40U;
    memset(source, 0, sizeof(*source));
    source->size = ROM_BYTES;
    source->mtime_seconds = 77;
    source->byte_order = ROM_BYTE_ORDER_Z64;
    for (index = 0U; index < ROM_BYTES; ++index) {
        if (index < ROM_HEADER_METADATA_BYTES)
            header_crc = crc_byte(header_crc, validation_bytes[index]);
        if (index < 64U ||
            (index >= ROM_BYTES / 2U && index < ROM_BYTES / 2U + 64U) ||
            index >= ROM_BYTES - 64U)
            sample_crc = crc_byte(sample_crc, validation_bytes[index]);
    }
    source->normalized_header_crc32 = header_crc ^ 0xffffffffU;
    source->normalized_sample_crc32 = sample_crc ^ 0xffffffffU;
    memset(fs, 0, sizeof(*fs));
    fs->context = &validation_state;
    fs->stat = validation_stat;
    fs->file_open_read = validation_open;
    fs->file_read = validation_read;
    fs->file_close = validation_close;
    menu->library_view.selected_valid = true;
    memset(&menu->library_view.selected_fingerprint, 0xa1,
           sizeof(menu->library_view.selected_fingerprint));
    menu->library_view.pending_fingerprint =
        menu->library_view.selected_fingerprint;
    view_load_rom_host_set_validation_fs(fs, validation_seek, source);
    view_load_rom_host_set_cart_load(stage_rom);
    view_load_rom_set_pending_path(menu, path_init("", "/game.z64"),
                                   MENU_MODE_LIBRARY);
    rom_stages = 0U;
    rom_stage_result = CART_LOAD_OK;
}

static void test_rom_validation_precedes_cancel_and_stages_once(void)
{
    menu_t menu; fake_service_t service; library_fs_t fs;
    library_source_t source;
    setup_rom(&menu, &service, &fs, &source);
    view_load_rom_init(&menu);
    validation_state.total_read = 0U;
    menu.actions.enter = true;
    view_load_rom_display(&menu, NULL);
    menu.actions.enter = false;
    TEST_CHECK(service.cancel_requests == 1U);
    TEST_CHECK(rom_stages == 0U);
    TEST_CHECK(validation_state.total_read <= 196U);
    TEST_CHECK(!menu_library_handoff_started(&menu));
    make_exact_quiesced(&service, false);
    view_load_rom_display(&menu, NULL);
    TEST_CHECK(rom_stages == 1U);
    TEST_CHECK(menu.next_mode == MENU_MODE_BOOT);
    TEST_CHECK(menu_library_handoff_started(&menu));
    {
        unsigned int coordinate_calls = service.coordinate_calls;
        unsigned int polls = service.polls;
        menu_library_coordinate_frame(&menu);
        TEST_CHECK(service.coordinate_calls == coordinate_calls);
        TEST_CHECK(service.polls == polls);
    }
    view_load_rom_display(&menu, NULL);
    TEST_CHECK(rom_stages == 1U);
    path_free(menu.load.rom_path);
}

static void test_stale_rom_fails_before_cancel_or_stage(void)
{
    menu_t menu; fake_service_t service; library_fs_t fs;
    library_source_t source;
    setup_rom(&menu, &service, &fs, &source);
    view_load_rom_init(&menu);
    validation_state.replacement = true;
    menu.actions.enter = true;
    view_load_rom_display(&menu, NULL);
    menu.actions.enter = false;
    TEST_CHECK(service.cancel_requests == 0U);
    TEST_CHECK(rom_stages == 0U);
    TEST_CHECK(menu.next_mode == MENU_MODE_ERROR);
    TEST_CHECK(validation_state.total_read <= 392U);
    TEST_CHECK(validation_state.opens == validation_state.closes);
}

static void test_cart_failure_restarts_new_generation_on_context_return(void)
{
    menu_t menu; fake_service_t service; library_fs_t fs;
    library_source_t source;
    setup_rom(&menu, &service, &fs, &source);
    view_load_rom_init(&menu);
    rom_stage_result = CART_LOAD_ERR_ROM_LOAD_FAIL;
    menu.actions.enter = true;
    view_load_rom_display(&menu, NULL);
    menu.actions.enter = false;
    make_exact_quiesced(&service, false);
    view_load_rom_display(&menu, NULL);
    TEST_CHECK(rom_stages == 1U);
    TEST_CHECK(menu.next_mode == MENU_MODE_ERROR);
    menu_library_action_failed(&menu);
    menu.mode = MENU_MODE_ERROR;
    menu.next_mode = MENU_MODE_LIBRARY;
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(service.restarts == 1U);
    TEST_CHECK(service.generations == 1U);
    menu_library_coordinate_frame(&menu);
    TEST_CHECK(service.restarts == 1U);
}

static unsigned int disk_stages;
static unsigned int emulator_stages;
static void stage_disk(menu_t *menu)
{ ++disk_stages; menu->next_mode = MENU_MODE_BOOT; }
static void stage_emulator(menu_t *menu)
{ ++emulator_stages; menu->next_mode = MENU_MODE_BOOT; }
void view_load_disk_host_set_loader(void (*callback)(menu_t *menu));
void view_load_disk_host_stage_pending(menu_t *menu);
void view_load_emulator_host_set_loader(void (*callback)(menu_t *menu));
void view_load_emulator_host_stage_pending(menu_t *menu);

static void test_disk_and_emulator_use_typed_cancel_guards(void)
{
    menu_t menu; fake_service_t service;
    setup(&menu, &service, MENU_MODE_LOAD_DISK);
    menu.load_pending.disk_file = true;
    disk_stages = 0U;
    view_load_disk_host_set_loader(stage_disk);
    view_load_disk_host_stage_pending(&menu);
    TEST_CHECK(service.cancel_requests == 1U && disk_stages == 0U);
    make_exact_quiesced(&service, false);
    view_load_disk_host_stage_pending(&menu);
    TEST_CHECK(disk_stages == 1U && menu_library_handoff_started(&menu));

    setup(&menu, &service, MENU_MODE_LOAD_EMULATOR);
    menu.load_pending.emulator_file = true;
    emulator_stages = 0U;
    view_load_emulator_host_set_loader(stage_emulator);
    view_load_emulator_host_stage_pending(&menu);
    TEST_CHECK(service.cancel_requests == 1U && emulator_stages == 0U);
    make_exact_quiesced(&service, false);
    view_load_emulator_host_stage_pending(&menu);
    TEST_CHECK(emulator_stages == 1U && menu_library_handoff_started(&menu));
}

void usb_comm_transition_reset(void);
void usb_comm_host_queue_reboot(menu_t *menu);
void usb_comm_host_complete_reboot(menu_t *menu);

static void test_late_usb_reboot_persists_until_exact_quiescence(void)
{
    menu_t menu; fake_service_t service; boot_params_t params;
    setup(&menu, &service, MENU_MODE_HOME);
    memset(&params, 0, sizeof(params));
    menu.boot_params = &params;
    usb_comm_transition_reset();
    menu_library_coordinate_frame(&menu);
    usb_comm_host_queue_reboot(&menu);
    usb_comm_host_complete_reboot(&menu);
    TEST_CHECK(service.cancel_requests == 1U);
    TEST_CHECK(menu.next_mode == MENU_MODE_HOME);
    TEST_CHECK(!menu_library_handoff_started(&menu));
    service.publication_pending = true;
    make_exact_quiesced(&service, false);
    service.callback_pending = true;
    usb_comm_host_complete_reboot(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_HOME);
    service.callback_pending = false;
    usb_comm_host_complete_reboot(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_BOOT);
    TEST_CHECK(menu_library_handoff_started(&menu));
    TEST_CHECK(params.device_type == BOOT_DEVICE_TYPE_ROM);
}

static void test_usb_cannot_consume_loader_action(void)
{
    menu_t menu; fake_service_t service; boot_params_t params;
    setup(&menu, &service, MENU_MODE_LOAD_DISK);
    memset(&params, 0, sizeof(params));
    menu.boot_params = &params;
    menu.load_pending.disk_file = true;
    disk_stages = 0U;
    view_load_disk_host_set_loader(stage_disk);
    usb_comm_transition_reset();
    view_load_disk_host_stage_pending(&menu);
    usb_comm_host_queue_reboot(&menu);
    make_exact_quiesced(&service, false);
    usb_comm_host_complete_reboot(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_DISK);
    TEST_CHECK(!menu_library_handoff_started(&menu));
    view_load_disk_host_stage_pending(&menu);
    TEST_CHECK(disk_stages == 1U);
    TEST_CHECK(menu.next_mode == MENU_MODE_BOOT);
}

static void test_loader_actions_cannot_replace_or_consume_each_other(void)
{
    menu_t menu; fake_service_t service;
    setup(&menu, &service, MENU_MODE_LOAD_DISK);
    menu.load_pending.disk_file = true;
    menu.load_pending.emulator_file = true;
    disk_stages = 0U;
    emulator_stages = 0U;
    view_load_disk_host_set_loader(stage_disk);
    view_load_emulator_host_set_loader(stage_emulator);
    view_load_disk_host_stage_pending(&menu);
    view_load_disk_host_stage_pending(&menu);
    TEST_CHECK(service.cancel_requests == 1U);
    make_exact_quiesced(&service, false);
    view_load_emulator_host_stage_pending(&menu);
    TEST_CHECK(emulator_stages == 0U);
    TEST_CHECK(menu.load_pending.emulator_file);
    TEST_CHECK(!menu_library_handoff_started(&menu));
    view_load_disk_host_stage_pending(&menu);
    TEST_CHECK(disk_stages == 1U);
    TEST_CHECK(menu.load_pending.emulator_file);
    TEST_CHECK(menu_library_handoff_started(&menu));
}

static void test_loader_cannot_consume_usb_reboot_readiness(void)
{
    menu_t menu; fake_service_t service; boot_params_t params;
    setup(&menu, &service, MENU_MODE_HOME);
    memset(&params, 0, sizeof(params));
    menu.boot_params = &params;
    menu.load_pending.disk_file = true;
    disk_stages = 0U;
    view_load_disk_host_set_loader(stage_disk);
    usb_comm_transition_reset();
    usb_comm_host_queue_reboot(&menu);
    usb_comm_host_complete_reboot(&menu);
    TEST_CHECK(service.cancel_requests == 1U);
    make_exact_quiesced(&service, false);
    view_load_disk_host_stage_pending(&menu);
    TEST_CHECK(disk_stages == 0U);
    TEST_CHECK(menu.load_pending.disk_file);
    TEST_CHECK(!menu_library_handoff_started(&menu));
    usb_comm_host_complete_reboot(&menu);
    TEST_CHECK(menu.next_mode == MENU_MODE_BOOT);
    TEST_CHECK(menu_library_handoff_started(&menu));
}

static void test_cancel_drains_all_work_and_discards_builder(void)
{
    menu_t menu; fake_service_t service;
    setup(&menu, &service, MENU_MODE_LOAD_DISK);
    service.unpublished_builder = true;
    service.publication_pending = true;
    service.cache_write_pending = true;
    service.callback_pending = true;
    service.open_handles = 1U;
    service.in_io_callbacks = 1U;
    TEST_CHECK(!menu_library_disk_ready(&menu));
    while (!library_service_is_quiesced(menu.library_service) &&
           service.polls < 20U)
        library_service_poll(menu.library_service, menu.mode);
    TEST_CHECK(service.polls < 20U);
    TEST_CHECK(service.builder_discards == 1U);
    TEST_CHECK(menu_library_disk_ready(&menu));
    TEST_CHECK(service.open_handles == 0U);
    TEST_CHECK(service.in_io_callbacks == 0U);
    TEST_CHECK(!service.publication_pending);
    TEST_CHECK(!service.cache_write_pending);
    TEST_CHECK(!service.callback_pending);
}

static void test_teardown_success_and_bounded_failure_are_fail_closed(void)
{
    menu_t menu; fake_service_t service;
    setup(&menu, &service, MENU_MODE_HOME);
    service.open_handles = 10U;
    TEST_CHECK(!menu_library_teardown(&menu, 3U));
    TEST_CHECK(service.polls == 3U);
    TEST_CHECK(service.frees == 0U);
    TEST_CHECK(menu.library_service != NULL);
    make_exact_quiesced(&service, false);
    TEST_CHECK(menu_library_teardown(&menu, 3U));
    TEST_CHECK(service.frees == 1U);
    TEST_CHECK(menu.library_service == NULL);
}

TEST_LIST = {
    { "Home to Browser delayed pause and later resume", test_home_browser_delays_and_resumes_later },
    { "Home to All Games and All Games to Details", test_home_library_and_library_details_are_coordinated },
    { "Details B resumes paused builder", test_details_back_pauses_and_resumes_same_builder },
    { "Details A validates then cancels and stages once", test_rom_validation_precedes_cancel_and_stages_once },
    { "stale ROM rejected before cancellation", test_stale_rom_fails_before_cancel_or_stage },
    { "cart failure restarts fresh generation", test_cart_failure_restarts_new_generation_on_context_return },
    { "disk and emulator guarded production seams", test_disk_and_emulator_use_typed_cancel_guards },
    { "late USB reboot exact-quiescence guard", test_late_usb_reboot_persists_until_exact_quiescence },
    { "USB cannot consume loader readiness", test_usb_cannot_consume_loader_action },
    { "loaders cannot replace or consume typed readiness", test_loader_actions_cannot_replace_or_consume_each_other },
    { "loader cannot consume USB readiness", test_loader_cannot_consume_usb_reboot_readiness },
    { "cancel drains work and discards builder", test_cancel_drains_all_work_and_discards_builder },
    { "bounded fail-closed teardown", test_teardown_success_and_bounded_failure_are_fail_closed },
    { NULL, NULL }
};
#endif
#else

#include "menu/library/library_metrics.h"
#include "menu/views/views.h"
#include "support/fake_library_fs.h"
#include "support/layer1_view_host_shims.h"
#include "support/rom_fixture_builder.h"

#include <stdio.h>
#include <stdlib.h>

#define CLOSURE_GUARD 20000U

void menu_library_transition_reset(menu_t *menu);
void menu_library_coordinate_frame(menu_t *menu);
bool menu_library_teardown(menu_t *menu, size_t poll_limit);
void menu_library_poll_usb_and_emit(menu_t *menu,
                                    library_metrics_writer_t writer,
                                    void *writer_context);

_Static_assert(LIBRARY_METRICS_EVENT_COUNT == 8U,
               "Phase 4 trace protocol requires exactly eight event slots");

typedef struct {
    fake_library_fs_t fs;
    library_service_t *service;
    menu_t menu;
} closure_fixture_t;

typedef enum {
    CLOSURE_WRITER_REFUSE,
    CLOSURE_WRITER_NEGATIVE,
    CLOSURE_WRITER_SHORT,
    CLOSURE_WRITER_EXACT
} closure_writer_mode_t;

typedef struct {
    unsigned char before;
    char bytes[LIBRARY_METRICS_TERMINAL_RECORD_BYTES];
    unsigned char after;
    const char *pointer;
    size_t length;
    size_t calls;
    size_t usb_returns;
    bool called_after_usb_return;
    closure_writer_mode_t mode;
} closure_writer_t;

static library_dirent_t closure_rom_entry(void)
{
    library_dirent_t entry;
    memset(&entry, 0, sizeof(entry));
    (void)strncpy(entry.basename, "closure.z64",
                  sizeof(entry.basename) - 1U);
    entry.type = LIBRARY_FS_ENTRY_FILE;
    entry.size = ROM_HEADER_WITH_IPL3_BYTES;
    entry.modified_time = 17;
    return entry;
}

static void closure_fixture_init(closure_fixture_t *fixture, bool with_rom)
{
    library_service_config_t config;
    library_dirent_t entry;
    uint8_t rom[ROM_HEADER_WITH_IPL3_BYTES];

    memset(fixture, 0, sizeof(*fixture));
    fake_library_fs_init(&fixture->fs);
    if (with_rom) {
        entry = closure_rom_entry();
        TEST_ASSERT(fake_library_fs_add_directory(
            &fixture->fs, "/", &entry, 1U));
        rom_fixture_build_canonical(rom);
        TEST_ASSERT(fake_library_fs_add_file(
            &fixture->fs, "/closure.z64", rom, sizeof(rom), 17));
    } else {
        TEST_ASSERT(fake_library_fs_add_directory(
            &fixture->fs, "/", NULL, 0U));
    }

    memset(&config, 0, sizeof(config));
    config.fs = fake_library_fs_interface(&fixture->fs);
    config.roots = library_roots_default();
    config.budget.max_directory_entries = 1U;
    config.budget.max_read_bytes = 64U;
    config.budget.max_ticks = 10U;
    config.storage_prefix = "sd:/";
    TEST_ASSERT(library_service_init(&fixture->service, &config));

    memset(&fixture->menu, 0, sizeof(fixture->menu));
    fixture->menu.mode = MENU_MODE_HOME;
    fixture->menu.next_mode = MENU_MODE_HOME;
    fixture->menu.storage_prefix = "sd:/";
    fixture->menu.library_service = fixture->service;
    menu_library_transition_reset(&fixture->menu);
    usb_comm_transition_reset();
}

static void closure_drain(closure_fixture_t *fixture, menu_mode_t mode)
{
    size_t guard;
    if (fixture->service == NULL) return;
    library_service_request_cancel(fixture->service);
    for (guard = 0U; guard < CLOSURE_GUARD &&
         !library_service_is_quiesced(fixture->service); ++guard) {
        library_service_poll(fixture->service, mode);
    }
    TEST_CHECK(guard < CLOSURE_GUARD);
    TEST_CHECK(library_service_is_quiesced(fixture->service));
}

static void closure_fixture_destroy(closure_fixture_t *fixture)
{
    if (fixture->service == NULL) return;
    closure_drain(fixture, MENU_MODE_BOOT);
    library_service_free(fixture->service);
    fixture->service = NULL;
    fixture->menu.library_service = NULL;
}

static uint32_t closure_poll_until_published(closure_fixture_t *fixture,
                                             size_t expected_records)
{
    size_t guard;
    for (guard = 0U; guard < CLOSURE_GUARD; ++guard) {
        const library_snapshot_t *snapshot =
            library_service_snapshot_acquire(fixture->service);
        if (snapshot != NULL &&
            library_snapshot_status(snapshot) == LIBRARY_SNAPSHOT_FRESH &&
            library_snapshot_record_count(snapshot) == expected_records) {
            uint32_t generation = library_snapshot_generation(snapshot);
            library_service_snapshot_release(snapshot);
            return generation;
        }
        if (snapshot != NULL) library_service_snapshot_release(snapshot);
        library_service_poll(fixture->service, MENU_MODE_HOME);
    }
    TEST_ASSERT(false);
    return 0U;
}

static void closure_writer_reset(closure_writer_t *writer,
                                 closure_writer_mode_t mode)
{
    memset(writer, 0, sizeof(*writer));
    writer->before = 0xa5U;
    writer->after = 0x5aU;
    writer->mode = mode;
}

static int closure_writer_capture(void *context, const char *bytes,
                                  size_t length)
{
    closure_writer_t *writer = context;
    size_t copy_length = length;
    ++writer->calls;
    writer->pointer = bytes;
    writer->length = length;
    writer->called_after_usb_return = !layer1_view_host_usb_poll_active();
    writer->usb_returns = layer1_view_host_usb_return_count();
    if (copy_length >= sizeof(writer->bytes))
        copy_length = sizeof(writer->bytes) - 1U;
    if (bytes != NULL && copy_length != 0U)
        memcpy(writer->bytes, bytes, copy_length);
    writer->bytes[copy_length] = '\0';
    if (writer->mode == CLOSURE_WRITER_REFUSE) return 0;
    if (writer->mode == CLOSURE_WRITER_NEGATIVE) return -7;
    if (writer->mode == CLOSURE_WRITER_SHORT)
        return length == 0U ? 0 : (int)(length - 1U);
    return (int)length;
}

static uint8_t closure_event_bit(library_metrics_event_t event)
{
    return (uint8_t)(UINT8_C(1) << (unsigned int)event);
}

static void closure_finish_trace(uint32_t id, uint32_t generation,
                                 bool quiescence, bool nonempty)
{
    if (quiescence) {
        TEST_ASSERT(library_metrics_trace_record(
            LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_QUIESCED,
            id, generation));
    }
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER, id, generation));
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_EXIT, id, generation));
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_BEGIN,
        id, generation));
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED,
        id, generation));
    if (nonempty) {
        TEST_ASSERT(library_metrics_trace_record(
            LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_NONEMPTY_FRAME_SUBMITTED,
            id, generation));
    }
}

void test_phase4_trace_protocol_context_id_reuse(void)
{
    library_metrics_snapshot_t snapshot;
    uint32_t first;
    uint32_t second;
    uint32_t wrapped;
    uint32_t ambiguous;
    bool valid;

    library_metrics_reset();
    library_metrics_host_set_tick_step(1U);
    library_metrics_host_set_ticks(10U);
    first = library_metrics_trace_begin(41U);
    TEST_CHECK(first == 1U);
    TEST_CHECK(!library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_BEGIN,
        first, 41U));
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(snapshot.trace.valid_mask == closure_event_bit(
        LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_INPUT_RECEIVED));
    TEST_CHECK(snapshot.trace.protocol_invalid);

    second = library_metrics_trace_begin(42U);
    TEST_CHECK(second == first + 1U && second != 0U);
    TEST_CHECK(!library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER, first, 41U));
    TEST_CHECK(!library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER, second, 41U));
    TEST_CHECK(library_metrics_trace_accept_transition(second, 42U, false));
    TEST_CHECK(!library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_QUIESCED,
        second, 42U));
    TEST_CHECK(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER, second, 42U));
    TEST_CHECK(!library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER, second, 42U));
    TEST_CHECK(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_EXIT, second, 42U));
    TEST_CHECK(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_BEGIN,
        second, 42U));
    TEST_CHECK(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED,
        second, 42U));
    library_metrics_snapshot(&snapshot);
    TEST_CHECK((snapshot.trace.applicable_mask & closure_event_bit(
        LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_QUIESCED)) == 0U);
    TEST_CHECK((snapshot.trace.valid_mask & closure_event_bit(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_NONEMPTY_FRAME_SUBMITTED)) == 0U);

    library_metrics_host_set_ticks(UINT32_MAX - 4U);
    library_metrics_host_set_tick_step(0U);
    second = library_metrics_trace_begin(43U);
    library_metrics_host_set_ticks(3U);
    TEST_CHECK(library_metrics_trace_accept_transition(second, 43U, true));
    library_metrics_snapshot(&snapshot);
    wrapped = library_metrics_tick_delta(
        snapshot.trace.ticks[
            LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_INPUT_RECEIVED],
        snapshot.trace.ticks[
            LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_TRANSITION_REQUESTED],
        &valid);
    TEST_CHECK(valid && wrapped == 8U);
    TEST_CHECK(!library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER, second, 43U));
    TEST_CHECK(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_QUIESCED,
        second, 43U));
    closure_finish_trace(second, 43U, false, false);

    ambiguous = library_metrics_tick_delta(
        0U, (uint32_t)INT32_MAX + 1U, &valid);
    TEST_CHECK(!valid && ambiguous == 0U);
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(snapshot.trace.transition_id == second);
    TEST_CHECK(snapshot.trace.generation == 43U);
    TEST_CHECK(snapshot.trace.ticks[
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_NONEMPTY_FRAME_SUBMITTED] == 0U);
}

void test_phase4_transition_action_decisions_and_supersession(void)
{
    closure_fixture_t fixture;
    library_metrics_snapshot_t metrics;
    const library_snapshot_t *snapshot;
    uint32_t generation;
    uint32_t immediate_id;
    uint32_t superseded_id;
    size_t guard;

    closure_fixture_init(&fixture, true);
    generation = closure_poll_until_published(&fixture, 1U);
    library_metrics_reset();
    library_metrics_host_set_ticks(100U);
    library_metrics_host_set_tick_step(1U);
    layer1_view_host_reset();
    layer1_view_host_observe_menu(&fixture.menu);

    fixture.menu.home.selected = 0;
    fixture.menu.actions.enter = true;
    view_home_display(&fixture.menu, NULL);
    fixture.menu.actions.enter = false;
    library_metrics_snapshot(&metrics);
    TEST_CHECK(metrics.trace.transition_id == 0U);
    TEST_CHECK(fixture.menu.next_mode == MENU_MODE_HOME);
    TEST_CHECK(layer1_view_host_sound_calls() == 0U);

    fixture.menu.home.selected = 5;
    fixture.menu.actions.enter = true;
    view_home_display(&fixture.menu, NULL);
    fixture.menu.actions.enter = false;
    library_metrics_snapshot(&metrics);
    immediate_id = metrics.trace.transition_id;
    TEST_CHECK(immediate_id != 0U);
    TEST_CHECK(metrics.trace.generation == generation);
    TEST_CHECK(layer1_view_host_sound_calls() == 1U);
    TEST_CHECK(layer1_view_host_sound_saw_input_trace());
    TEST_CHECK(layer1_view_host_sound_next_mode() == MENU_MODE_HOME);
    TEST_CHECK(fixture.menu.next_mode == MENU_MODE_LIBRARY);

    menu_library_coordinate_frame(&fixture.menu);
    TEST_CHECK(fixture.menu.next_mode == MENU_MODE_HOME);
    TEST_CHECK(fixture.menu.mode == MENU_MODE_HOME);
    menu_library_coordinate_frame(&fixture.menu);
    TEST_CHECK(fixture.menu.next_mode == MENU_MODE_LIBRARY);
    library_metrics_snapshot(&metrics);
    TEST_CHECK((metrics.trace.valid_mask & closure_event_bit(
        LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_TRANSITION_REQUESTED)) != 0U);
    TEST_CHECK((metrics.trace.valid_mask & closure_event_bit(
        LIBRARY_METRICS_EVENT_HOME_ALL_GAMES_QUIESCED)) != 0U);

    snapshot = library_service_snapshot_acquire(fixture.service);
    TEST_ASSERT(snapshot != NULL);
    TEST_CHECK(library_snapshot_generation(snapshot) == generation);
    TEST_CHECK(library_snapshot_record_count(snapshot) == 1U);
    library_service_snapshot_release(snapshot);
    fixture.menu.mode = MENU_MODE_LIBRARY;
    library_service_poll(fixture.service, MENU_MODE_LIBRARY);
    TEST_CHECK(!library_service_is_quiesced(fixture.service));

    fixture.menu.mode = MENU_MODE_HOME;
    fixture.menu.next_mode = MENU_MODE_HOME;
    fixture.menu.actions.enter = true;
    view_home_display(&fixture.menu, NULL);
    fixture.menu.actions.enter = false;
    menu_library_coordinate_frame(&fixture.menu);
    TEST_CHECK(fixture.menu.next_mode == MENU_MODE_HOME);
    library_metrics_snapshot(&metrics);
    superseded_id = metrics.trace.transition_id;

    fixture.menu.actions.enter = true;
    view_home_display(&fixture.menu, NULL);
    fixture.menu.actions.enter = false;
    library_metrics_snapshot(&metrics);
    TEST_CHECK(metrics.trace.transition_id == superseded_id + 1U);
    TEST_CHECK(metrics.trace.generation == generation);
    menu_library_coordinate_frame(&fixture.menu);
    TEST_CHECK(fixture.menu.next_mode == MENU_MODE_HOME);
    for (guard = 0U; guard < CLOSURE_GUARD &&
         !library_service_is_quiesced(fixture.service); ++guard) {
        library_service_poll(fixture.service, MENU_MODE_HOME);
        menu_library_coordinate_frame(&fixture.menu);
    }
    TEST_CHECK(guard < CLOSURE_GUARD);
    menu_library_coordinate_frame(&fixture.menu);
    TEST_CHECK(fixture.menu.next_mode == MENU_MODE_LIBRARY);
    library_metrics_snapshot(&metrics);
    TEST_CHECK(metrics.trace.transition_id == superseded_id + 1U);
    TEST_CHECK(!metrics.publication_failed);

    closure_fixture_destroy(&fixture);
}

void test_phase4_output_critical_usb_retry_stable_record(void)
{
    menu_t menu;
    closure_writer_t writer;
    library_metrics_snapshot_t snapshot;
    library_metrics_overlay_t overlay;
    const char *first_pointer;
    char first_bytes[LIBRARY_METRICS_TERMINAL_RECORD_BYTES];
    size_t first_length;
    uint32_t id;

    memset(&menu, 0, sizeof(menu));
    menu.mode = MENU_MODE_HOME;
    menu.next_mode = MENU_MODE_HOME;
    layer1_view_host_reset();
    library_metrics_reset();
    library_metrics_host_set_ticks(10U);
    library_metrics_host_set_tick_step(1U);

    id = library_metrics_trace_begin(7U);
    TEST_ASSERT(library_metrics_trace_accept_transition(id, 7U, false));
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_ENTER, id, 7U));
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_INIT_EXIT, id, 7U));
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_BEGIN, id, 7U));
    TEST_CHECK(library_metrics_critical_interval_active());
    TEST_CHECK(!library_metrics_format_overlay(
        LIBRARY_METRICS_OVERLAY_ALL_GAMES, &overlay));

    closure_writer_reset(&writer, CLOSURE_WRITER_EXACT);
    menu_library_poll_usb_and_emit(&menu, closure_writer_capture, &writer);
    TEST_CHECK(writer.calls == 0U);
    TEST_ASSERT(library_metrics_trace_record(
        LIBRARY_METRICS_EVENT_ALL_GAMES_FIRST_FRAME_SUBMITTED, id, 7U));
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(snapshot.terminal_pending);
    TEST_CHECK(!snapshot.terminal_eligible);
    TEST_CHECK(snapshot.terminal_event_mask ==
               LIBRARY_METRICS_TERMINAL_TRANSITION);
    TEST_CHECK(!library_metrics_emit_pending(closure_writer_capture, &writer));
    TEST_CHECK(writer.calls == 0U);

    menu_library_poll_usb_and_emit(&menu, NULL, NULL);
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(snapshot.terminal_pending && snapshot.terminal_eligible);
    TEST_CHECK(!library_metrics_emit_pending(NULL, NULL));

    closure_writer_reset(&writer, CLOSURE_WRITER_REFUSE);
    TEST_CHECK(!library_metrics_emit_pending(closure_writer_capture, &writer));
    TEST_CHECK(writer.calls == 1U);
    first_pointer = writer.pointer;
    first_length = writer.length;
    memcpy(first_bytes, writer.bytes, first_length + 1U);
    TEST_CHECK(writer.before == 0xa5U && writer.after == 0x5aU);

    writer.mode = CLOSURE_WRITER_NEGATIVE;
    TEST_CHECK(!library_metrics_emit_pending(closure_writer_capture, &writer));
    TEST_CHECK(writer.pointer == first_pointer);
    TEST_CHECK(writer.length == first_length);
    TEST_CHECK(memcmp(writer.bytes, first_bytes, first_length + 1U) == 0);

    writer.mode = CLOSURE_WRITER_SHORT;
    TEST_CHECK(!library_metrics_emit_pending(closure_writer_capture, &writer));
    TEST_CHECK(writer.pointer == first_pointer);
    TEST_CHECK(memcmp(writer.bytes, first_bytes, first_length + 1U) == 0);

    writer.mode = CLOSURE_WRITER_EXACT;
    TEST_CHECK(library_metrics_emit_pending(closure_writer_capture, &writer));
    TEST_CHECK(writer.pointer == first_pointer);
    TEST_CHECK(writer.called_after_usb_return);
    TEST_CHECK(writer.usb_returns == layer1_view_host_usb_poll_calls());
    library_metrics_snapshot(&snapshot);
    TEST_CHECK(!snapshot.terminal_pending && !snapshot.terminal_eligible);
    TEST_CHECK(snapshot.terminal_event_mask == 0U);
    TEST_CHECK(!library_metrics_emit_pending(closure_writer_capture, &writer));
    TEST_CHECK(menu.next_mode == MENU_MODE_HOME);
}

static void closure_queue_unavailable_record(uint32_t generation,
                                             bool publication_valid,
                                             const library_snapshot_t *published)
{
    library_scanner_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    library_metrics_scan_attempt(generation, 10U);
    library_metrics_scan_terminal(&stats, LIBRARY_SCANNER_COMPLETE,
                                  true, 20U);
    library_metrics_publication_succeeded(
        generation, publication_valid ? published : NULL);
    if (publication_valid)
        library_metrics_publication_failed(generation);
    else
        library_metrics_invalidate_snapshot_detail(generation);
}

void test_phase4_seam_service_poll_detail_usb_emission(void)
{
    closure_fixture_t fixture;
    closure_writer_t writer;
    library_metrics_snapshot_t metrics;
    const library_snapshot_t *published;
    uint32_t generation;
    size_t usb_before;

    layer1_view_host_reset();
    library_metrics_reset();
    library_metrics_host_set_ticks(100U);
    library_metrics_host_set_tick_step(1U);
    closure_fixture_init(&fixture, true);
    generation = closure_poll_until_published(&fixture, 1U);

    closure_writer_reset(&writer, CLOSURE_WRITER_EXACT);
    menu_library_poll_usb_and_emit(
        &fixture.menu, closure_writer_capture, &writer);
    menu_library_poll_usb_and_emit(
        &fixture.menu, closure_writer_capture, &writer);
    TEST_CHECK(writer.calls == 0U);
    library_metrics_snapshot(&metrics);
    TEST_CHECK(metrics.retained_path_detail_pending);
    TEST_CHECK(!metrics.retained_path_detail_valid);

    usb_before = layer1_view_host_usb_return_count();
    library_service_poll(fixture.service, MENU_MODE_HOME);
    library_metrics_snapshot(&metrics);
    TEST_CHECK(!metrics.retained_path_detail_pending);
    TEST_CHECK(metrics.publication_facts_valid);
    TEST_CHECK(metrics.retained_path_detail_valid);
    TEST_CHECK(metrics.published_generation == generation);
    TEST_CHECK(metrics.record_count == 1U);
    TEST_CHECK(metrics.retained_path_count == 1U);
    TEST_CHECK(metrics.retained_path_bytes != 0U);

    menu_library_poll_usb_and_emit(
        &fixture.menu, closure_writer_capture, &writer);
    TEST_CHECK(writer.calls == 1U);
    TEST_CHECK(writer.called_after_usb_return);
    TEST_CHECK(writer.usb_returns == usb_before + 1U);
    TEST_CHECK(strstr(writer.bytes,
                      "publication_facts_valid=1 ") != NULL);
    TEST_CHECK(strstr(writer.bytes,
                      "retained_path_detail_valid=1 ") != NULL);
    TEST_CHECK(strstr(writer.bytes, "records=1 paths=1/") != NULL);

    library_metrics_reset();
    library_metrics_record_usb_opportunity(100U);
    library_metrics_snapshot(&metrics);
    TEST_CHECK(metrics.max_usb_gap_ticks == 0U);
    library_metrics_record_usb_opportunity(108U);
    library_metrics_snapshot(&metrics);
    TEST_CHECK(metrics.max_usb_gap_ticks == 8U);
    library_metrics_record_usb_opportunity(UINT32_MAX - 4U);
    library_metrics_record_usb_opportunity(3U);
    library_metrics_snapshot(&metrics);
    TEST_CHECK(metrics.max_usb_gap_ticks == 8U);
    library_metrics_record_usb_opportunity(
        3U + (uint32_t)INT32_MAX + 1U);
    library_metrics_snapshot(&metrics);
    TEST_CHECK(metrics.timing_invalid);
    TEST_CHECK(metrics.max_usb_gap_ticks == 8U);

    published = library_service_snapshot_acquire(fixture.service);
    TEST_ASSERT(published != NULL);

    library_metrics_reset();
    closure_queue_unavailable_record(generation, true, published);
    closure_writer_reset(&writer, CLOSURE_WRITER_EXACT);
    library_metrics_record_usb_opportunity(30U);
    TEST_CHECK(library_metrics_emit_pending(closure_writer_capture, &writer));
    TEST_CHECK(strstr(writer.bytes,
                      "publication_facts_valid=1 ") != NULL);
    TEST_CHECK(strstr(writer.bytes,
                      "retained_path_detail_valid=0 ") != NULL);
    TEST_CHECK(strstr(writer.bytes, "paths=1/n/a") != NULL);

    library_metrics_reset();
    closure_queue_unavailable_record(generation, false, NULL);
    closure_writer_reset(&writer, CLOSURE_WRITER_EXACT);
    library_metrics_record_usb_opportunity(31U);
    TEST_CHECK(library_metrics_emit_pending(closure_writer_capture, &writer));
    TEST_CHECK(strstr(writer.bytes,
                      "publication_facts_valid=0 ") != NULL);
    TEST_CHECK(strstr(writer.bytes,
                      "retained_path_detail_valid=0 ") != NULL);
    TEST_CHECK(strstr(writer.bytes,
                      "records=n/a paths=n/a/n/a warn=n/a err=n/a") != NULL);

    library_service_snapshot_release(published);
    closure_fixture_destroy(&fixture);
}

#endif
