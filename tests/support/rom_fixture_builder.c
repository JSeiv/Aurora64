#include "support/rom_fixture_builder.h"

#include <assert.h>
#include <string.h>

static void put_u32_be(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

void rom_fixture_build_canonical(uint8_t bytes[ROM_HEADER_WITH_IPL3_BYTES])
{
    static const char title[] = "ABCDEFGHIJKLMNOPQRST";
    size_t index;

    memset(bytes, 0, ROM_HEADER_WITH_IPL3_BYTES);
    put_u32_be(bytes + 0x00, UINT32_C(0x80371240));
    put_u32_be(bytes + 0x04, UINT32_C(0x12345678));
    put_u32_be(bytes + 0x08, UINT32_C(0x80401234));
    put_u32_be(bytes + 0x10, UINT32_C(0x89abcdef));
    put_u32_be(bytes + 0x14, UINT32_C(0x01234567));
    memcpy(bytes + 0x20, title, 20U);
    memcpy(bytes + 0x3b, "NABE", 4U);
    bytes[0x3f] = 7U;
    for (index = ROM_HEADER_METADATA_BYTES;
         index < ROM_HEADER_WITH_IPL3_BYTES; ++index) {
        bytes[index] = (uint8_t)((index * 37U + 11U) & 0xffU);
    }
}

void rom_fixture_encode(rom_byte_order_t order, const uint8_t *canonical,
                        size_t length, uint8_t *encoded)
{
    size_t index;

    /* V64 and N64 encoders operate on complete 2-byte and 4-byte units. */
    assert(order == ROM_BYTE_ORDER_Z64 ||
           (order == ROM_BYTE_ORDER_V64 && length % 2U == 0U) ||
           (order == ROM_BYTE_ORDER_N64 && length % 4U == 0U));
    if (order == ROM_BYTE_ORDER_Z64) {
        memcpy(encoded, canonical, length);
    } else if (order == ROM_BYTE_ORDER_V64) {
        for (index = 0U; index < length; index += 2U) {
            encoded[index] = canonical[index + 1U];
            encoded[index + 1U] = canonical[index];
        }
    } else {
        for (index = 0U; index < length; index += 4U) {
            encoded[index] = canonical[index + 3U];
            encoded[index + 1U] = canonical[index + 2U];
            encoded[index + 2U] = canonical[index + 1U];
            encoded[index + 3U] = canonical[index];
        }
    }
}
