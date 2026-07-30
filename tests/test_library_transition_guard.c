#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
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
