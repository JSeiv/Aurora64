#include "rom_header.h"

#include <string.h>

static size_t order_unit(rom_byte_order_t order)
{
    switch (order) {
        case ROM_BYTE_ORDER_Z64: return 1U;
        case ROM_BYTE_ORDER_V64: return 2U;
        case ROM_BYTE_ORDER_N64: return 4U;
        default: return 0U;
    }
}

static uint8_t stream_byte(const uint8_t old_carry[4], size_t old_carry_len,
                           const uint8_t *input, size_t position)
{
    if (position < old_carry_len) {
        return old_carry[position];
    }
    return input[position - old_carry_len];
}

size_t rom_normalize_bytes(rom_byte_order_t order, const uint8_t *input,
                           size_t input_len, uint8_t carry[4],
                           size_t *carry_len, uint8_t *output,
                           size_t output_capacity, bool final)
{
    uint8_t old_carry[4];
    size_t unit = order_unit(order);
    size_t old_carry_len;
    size_t total;
    size_t produced;
    size_t position;
    size_t remainder;

    if (unit == 0U || carry == NULL || carry_len == NULL ||
        (input_len != 0U && input == NULL)) {
        return SIZE_MAX;
    }
    old_carry_len = *carry_len;
    if (old_carry_len >= unit || old_carry_len > 3U ||
        input_len > SIZE_MAX - old_carry_len) {
        return SIZE_MAX;
    }
    total = old_carry_len + input_len;
    remainder = total % unit;
    produced = total - remainder;
    if ((produced != 0U && output == NULL) || output_capacity < produced ||
        (final && remainder != 0U)) {
        return SIZE_MAX;
    }

    memcpy(old_carry, carry, sizeof(old_carry));
    for (position = 0U; position < produced; position += unit) {
        size_t index;
        for (index = 0U; index < unit; ++index) {
            size_t source_index = (order == ROM_BYTE_ORDER_Z64) ? index :
                                  (unit - 1U - index);
            output[position + index] = stream_byte(old_carry, old_carry_len,
                                                    input,
                                                    position + source_index);
        }
    }
    for (position = 0U; position < remainder; ++position) {
        carry[position] = stream_byte(old_carry, old_carry_len, input,
                                      produced + position);
    }
    for (position = remainder; position < 4U; ++position) {
        carry[position] = 0U;
    }
    *carry_len = remainder;
    return produced;
}

bool rom_normalize_prefix(rom_byte_order_t order, const uint8_t *input,
                          size_t input_len, uint8_t *output,
                          size_t required_len)
{
    uint8_t carry[4] = { 0U, 0U, 0U, 0U };
    size_t carry_len = 0U;
    size_t result;
    size_t unit = order_unit(order);
    size_t position;

    if (unit == 0U || required_len > input_len ||
        (required_len != 0U && (input == NULL || output == NULL))) {
        return false;
    }
    if (required_len == 0U) {
        return true;
    }
    if (input == output) {
        if (required_len % unit != 0U) {
            return false;
        }
        for (position = 0U; position < required_len; position += unit) {
            size_t index;
            for (index = 0U; index < unit / 2U; ++index) {
                uint8_t temporary = output[position + index];
                output[position + index] = output[position + unit - 1U - index];
                output[position + unit - 1U - index] = temporary;
            }
        }
        return true;
    }
    if (((uintptr_t)input < (uintptr_t)output &&
         (uintptr_t)output - (uintptr_t)input < required_len) ||
        ((uintptr_t)output < (uintptr_t)input &&
         (uintptr_t)input - (uintptr_t)output < required_len)) {
        return false;
    }
    result = rom_normalize_bytes(order, input, required_len, carry,
                                 &carry_len, output, required_len, true);
    return result == required_len && carry_len == 0U;
}

static uint32_t read_u32_be(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static uint64_t read_u64_be(const uint8_t *bytes)
{
    return ((uint64_t)read_u32_be(bytes) << 32) |
           (uint64_t)read_u32_be(bytes + 4U);
}

static rom_region_t normalize_country(uint8_t country)
{
    switch (country) {
        case 'A':
        case 'J': return ROM_REGION_NTSC_J;
        case 'E':
        case 'G': return ROM_REGION_NTSC_U;
        case 'D':
        case 'F':
        case 'H':
        case 'I':
        case 'L':
        case 'P':
        case 'S':
        case 'U':
        case 'W':
        case 'X':
        case 'Y': return ROM_REGION_PAL;
        case 'B':
        case 'C':
        case 'K':
        case 'N':
        case 'Z': return ROM_REGION_OTHER;
        default: return ROM_REGION_UNKNOWN;
    }
}

bool rom_header_parse(const uint8_t *bytes, size_t length, rom_header_t *out)
{
    uint8_t canonical[ROM_HEADER_METADATA_BYTES];
    rom_byte_order_t order;

    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (bytes == NULL || length < ROM_HEADER_METADATA_BYTES) {
        return false;
    }

    if (memcmp(bytes, "\x80\x37\x12\x40", 4U) == 0 ||
        memcmp(bytes, "\x80\x27\x07\x40", 4U) == 0) {
        order = ROM_BYTE_ORDER_Z64;
    } else if (memcmp(bytes, "\x37\x80\x40\x12", 4U) == 0) {
        order = ROM_BYTE_ORDER_V64;
    } else if (memcmp(bytes, "\x40\x12\x37\x80", 4U) == 0) {
        order = ROM_BYTE_ORDER_N64;
    } else {
        return false;
    }
    if (!rom_normalize_prefix(order, bytes, length, canonical,
                              sizeof(canonical))) {
        return false;
    }

    out->byte_order = order;
    memcpy(out->title, canonical + 0x20U, 20U);
    out->title[20] = '\0';
    memcpy(out->game_code, canonical + 0x3bU, 4U);
    out->game_code[4] = '\0';
    memcpy(out->cartridge_id, canonical + 0x3cU, 2U);
    out->cartridge_id[2] = '\0';
    out->country_code = canonical[0x3eU];
    out->region = normalize_country(out->country_code);
    out->revision = canonical[0x3fU];
    out->clock_rate = read_u32_be(canonical + 0x04U);
    out->boot_address = read_u32_be(canonical + 0x08U);
    out->check_code = read_u64_be(canonical + 0x10U);
    return true;
}
