#define TEST_NO_MAIN
#include "acutest.h"
#include "menu/library/rom_header.h"
#include "support/rom_fixture_builder.h"

#include <stdint.h>
#include <string.h>

static rom_header_t parse_order(rom_byte_order_t order, uint8_t country)
{
    uint8_t canonical[ROM_HEADER_WITH_IPL3_BYTES];
    uint8_t encoded[ROM_HEADER_WITH_IPL3_BYTES];
    rom_header_t header;

    rom_fixture_build_canonical(canonical);
    canonical[0x3e] = country;
    rom_fixture_encode(order, canonical, sizeof(canonical), encoded);
    memset(&header, 0xa5, sizeof(header));
    TEST_ASSERT(rom_header_parse(encoded, sizeof(encoded), &header));
    return header;
}

void test_rom_header_orders_and_fields(void)
{
    rom_header_t expected = parse_order(ROM_BYTE_ORDER_Z64, (uint8_t)'E');
    rom_header_t v64 = parse_order(ROM_BYTE_ORDER_V64, (uint8_t)'E');
    rom_header_t n64 = parse_order(ROM_BYTE_ORDER_N64, (uint8_t)'E');

    TEST_CHECK(expected.byte_order == ROM_BYTE_ORDER_Z64);
    TEST_CHECK(v64.byte_order == ROM_BYTE_ORDER_V64);
    TEST_CHECK(n64.byte_order == ROM_BYTE_ORDER_N64);
    TEST_CHECK(strcmp(expected.title, "ABCDEFGHIJKLMNOPQRST") == 0);
    TEST_CHECK(strlen(expected.title) == 20U);
    TEST_CHECK(strcmp(expected.game_code, "NABE") == 0);
    TEST_CHECK(strcmp(expected.cartridge_id, "AB") == 0);
    TEST_CHECK(expected.country_code == (uint8_t)'E');
    TEST_CHECK(expected.region == ROM_REGION_NTSC_U);
    TEST_CHECK(expected.revision == 7U);
    TEST_CHECK(expected.clock_rate == UINT32_C(0x12345678));
    TEST_CHECK(expected.boot_address == UINT32_C(0x80401234));
    TEST_CHECK(expected.check_code == UINT64_C(0x89abcdef01234567));
    TEST_CHECK(strcmp(v64.title, expected.title) == 0);
    TEST_CHECK(strcmp(n64.title, expected.title) == 0);
    TEST_CHECK(strcmp(v64.game_code, expected.game_code) == 0);
    TEST_CHECK(strcmp(n64.game_code, expected.game_code) == 0);
    TEST_CHECK(strcmp(v64.cartridge_id, expected.cartridge_id) == 0);
    TEST_CHECK(strcmp(n64.cartridge_id, expected.cartridge_id) == 0);
    TEST_CHECK(v64.country_code == expected.country_code);
    TEST_CHECK(n64.country_code == expected.country_code);
    TEST_CHECK(v64.region == expected.region);
    TEST_CHECK(n64.region == expected.region);
    TEST_CHECK(v64.revision == expected.revision);
    TEST_CHECK(n64.revision == expected.revision);
    TEST_CHECK(v64.clock_rate == expected.clock_rate);
    TEST_CHECK(n64.clock_rate == expected.clock_rate);
    TEST_CHECK(v64.boot_address == expected.boot_address);
    TEST_CHECK(n64.boot_address == expected.boot_address);
    TEST_CHECK(v64.check_code == expected.check_code);
    TEST_CHECK(n64.check_code == expected.check_code);
}

void test_rom_header_regions_and_fallback_boundary(void)
{
    struct region_case { uint8_t country; rom_region_t region; };
    static const struct region_case cases[] = {
        { (uint8_t)'A', ROM_REGION_NTSC_J },
        { (uint8_t)'J', ROM_REGION_NTSC_J },
        { (uint8_t)'E', ROM_REGION_NTSC_U },
        { (uint8_t)'G', ROM_REGION_NTSC_U },
        { (uint8_t)'D', ROM_REGION_PAL },
        { (uint8_t)'F', ROM_REGION_PAL },
        { (uint8_t)'H', ROM_REGION_PAL },
        { (uint8_t)'I', ROM_REGION_PAL },
        { (uint8_t)'L', ROM_REGION_PAL },
        { (uint8_t)'P', ROM_REGION_PAL },
        { (uint8_t)'S', ROM_REGION_PAL },
        { (uint8_t)'U', ROM_REGION_PAL },
        { (uint8_t)'W', ROM_REGION_PAL },
        { (uint8_t)'X', ROM_REGION_PAL },
        { (uint8_t)'Y', ROM_REGION_PAL },
        { (uint8_t)'B', ROM_REGION_OTHER },
        { (uint8_t)'C', ROM_REGION_OTHER },
        { (uint8_t)'K', ROM_REGION_OTHER },
        { (uint8_t)'N', ROM_REGION_OTHER },
        { (uint8_t)'Z', ROM_REGION_OTHER },
        { UINT8_C(0x00), ROM_REGION_UNKNOWN },
        { UINT8_C(0x01), ROM_REGION_UNKNOWN },
        { (uint8_t)'M', ROM_REGION_UNKNOWN },
        { (uint8_t)'Q', ROM_REGION_UNKNOWN },
        { UINT8_C(0xff), ROM_REGION_UNKNOWN }
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        rom_header_t header = parse_order(ROM_BYTE_ORDER_Z64, cases[index].country);
        TEST_CHECK_(header.region == cases[index].region, "country 0x%02x", cases[index].country);
    }

    {
        struct fallback_case {
            const char *title;
            uint8_t game_code[4];
            uint8_t country;
            rom_region_t region;
            uint8_t revision;
        };
        static const struct fallback_case fallback_cases[] = {
            { "SYNTH HOMEBREW", { 'H', 'B', 'R', 0xffU }, 0xffU,
              ROM_REGION_UNKNOWN, 0x42U },
            { "SYNTH HACK", { 'N', 'H', 'K', 'B' }, (uint8_t)'B',
              ROM_REGION_OTHER, 0x11U },
            { "SYNTH PROTOTYPE", { 'N', 'P', 'R', 0x01U }, 0x01U,
              ROM_REGION_UNKNOWN, 0x22U },
            { "MALFORMED TUPLE", { 0U, 0x7fU, 0xffU, 0U }, 0U,
              ROM_REGION_UNKNOWN, 0U }
        };
        size_t fallback_index;

        for (fallback_index = 0U;
             fallback_index < sizeof(fallback_cases) / sizeof(fallback_cases[0]);
             ++fallback_index) {
            uint8_t canonical[ROM_HEADER_METADATA_BYTES];
            rom_header_t header;
            const struct fallback_case *fallback = &fallback_cases[fallback_index];

            memset(canonical, 0, sizeof(canonical));
            canonical[0] = 0x80U; canonical[1] = 0x37U;
            canonical[2] = 0x12U; canonical[3] = 0x40U;
            memcpy(canonical + 0x20, fallback->title, strlen(fallback->title));
            memcpy(canonical + 0x3b, fallback->game_code, 4U);
            canonical[0x3e] = fallback->country;
            canonical[0x3f] = fallback->revision;
            TEST_ASSERT(rom_header_parse(canonical, sizeof(canonical), &header));
            TEST_CHECK(memcmp(header.game_code, fallback->game_code, 4U) == 0);
            TEST_CHECK(memcmp(header.cartridge_id, fallback->game_code + 1U, 2U) == 0);
            TEST_CHECK(header.country_code == fallback->country);
            TEST_CHECK(header.region == fallback->region);
            TEST_CHECK(header.revision == fallback->revision);
        }
    }
}

void test_rom_header_bounds_invalid_and_zeroing(void)
{
    uint8_t bytes[ROM_HEADER_WITH_IPL3_BYTES];
    rom_header_t header;
    size_t lengths[] = { 0U, 3U, ROM_HEADER_METADATA_BYTES - 1U };
    size_t index;

    rom_fixture_build_canonical(bytes);
    for (index = 0U; index < sizeof(lengths) / sizeof(lengths[0]); ++index) {
        memset(&header, 0xa5, sizeof(header));
        TEST_CHECK(!rom_header_parse(bytes, lengths[index], &header));
        {
            rom_header_t zero;
            memset(&zero, 0, sizeof(zero));
            TEST_CHECK(memcmp(&header, &zero, sizeof(header)) == 0);
        }
    }
    TEST_CHECK(rom_header_parse(bytes, ROM_HEADER_METADATA_BYTES, &header));
    TEST_CHECK(rom_header_parse(bytes, ROM_HEADER_WITH_IPL3_BYTES - 1U, &header));
    bytes[0] = 0U;
    memset(&header, 0xa5, sizeof(header));
    TEST_CHECK(!rom_header_parse(bytes, sizeof(bytes), &header));
    {
        rom_header_t zero;
        memset(&zero, 0, sizeof(zero));
        TEST_CHECK(memcmp(&header, &zero, sizeof(header)) == 0);
    }
    TEST_CHECK(!rom_header_parse(NULL, sizeof(bytes), &header));
    TEST_CHECK(!rom_header_parse(bytes, sizeof(bytes), NULL));
}

void test_rom_header_64dd_ipl_compatibility(void)
{
    uint8_t bytes[ROM_HEADER_WITH_IPL3_BYTES];
    rom_header_t header;

    rom_fixture_build_canonical(bytes);
    bytes[0] = 0x80U;
    bytes[1] = 0x27U;
    bytes[2] = 0x07U;
    bytes[3] = 0x40U;
    bytes[0x38] = 0x01U;

    TEST_ASSERT(rom_header_parse(bytes, sizeof(bytes), &header));
    TEST_CHECK(header.byte_order == ROM_BYTE_ORDER_Z64);
    TEST_CHECK(memcmp(bytes, "\x80\x27\x07\x40", 4U) == 0);
    TEST_CHECK(rom_normalize_prefix(header.byte_order, bytes, sizeof(bytes),
                                    bytes, sizeof(bytes)));
    TEST_CHECK(memcmp(bytes, "\x80\x27\x07\x40", 4U) == 0);
    TEST_CHECK(bytes[0x38] == 0x01U);

    bytes[0] = 0x81U;
    memset(&header, 0xa5, sizeof(header));
    TEST_CHECK(!rom_header_parse(bytes, sizeof(bytes), &header));
    {
        rom_header_t zero;
        memset(&zero, 0, sizeof(zero));
        TEST_CHECK(memcmp(&header, &zero, sizeof(header)) == 0);
    }
}

void test_rom_header_prefix_normalization(void)
{
    uint8_t canonical[ROM_HEADER_WITH_IPL3_BYTES];
    uint8_t encoded[ROM_HEADER_WITH_IPL3_BYTES];
    uint8_t normalized[ROM_HEADER_WITH_IPL3_BYTES];
    uint8_t before[ROM_HEADER_WITH_IPL3_BYTES];
    rom_byte_order_t order;

    rom_fixture_build_canonical(canonical);
    canonical[0x38] = 0x01U;
    for (order = ROM_BYTE_ORDER_Z64; order <= ROM_BYTE_ORDER_N64; ++order) {
        rom_fixture_encode(order, canonical, sizeof(canonical), encoded);
        memcpy(before, encoded, sizeof(before));
        TEST_ASSERT(rom_normalize_prefix(order, encoded, sizeof(encoded), normalized, sizeof(normalized)));
        TEST_CHECK(memcmp(normalized, canonical, sizeof(canonical)) == 0);
        TEST_CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);

        memcpy(normalized, encoded, sizeof(normalized));
        TEST_ASSERT(rom_normalize_prefix(order, normalized, sizeof(normalized),
                                         normalized, sizeof(normalized)));
        TEST_CHECK_(memcmp(normalized, canonical, sizeof(canonical)) == 0,
                    "in-place order %d", (int)order);
        TEST_CHECK_(normalized[0x38] == 0x01U,
                    "canonical embedded flag order %d", (int)order);
    }
    TEST_CHECK(rom_normalize_prefix(ROM_BYTE_ORDER_Z64, NULL, 0U, NULL, 0U));
    TEST_CHECK(!rom_normalize_prefix((rom_byte_order_t)99, NULL, 0U, NULL, 0U));
    TEST_CHECK(!rom_normalize_prefix((rom_byte_order_t)99, canonical,
                                     sizeof(canonical), normalized, 4U));
    TEST_CHECK(!rom_normalize_prefix(ROM_BYTE_ORDER_Z64, NULL, 4U, normalized, 4U));
    TEST_CHECK(!rom_normalize_prefix(ROM_BYTE_ORDER_Z64, canonical, 4U, NULL, 4U));
    TEST_CHECK(!rom_normalize_prefix(ROM_BYTE_ORDER_Z64, canonical, 63U, normalized, 64U));
    TEST_CHECK(!rom_normalize_prefix(ROM_BYTE_ORDER_Z64, canonical,
                                     ROM_HEADER_WITH_IPL3_BYTES - 1U,
                                     normalized, ROM_HEADER_WITH_IPL3_BYTES));
    memcpy(normalized, canonical, sizeof(normalized));
    TEST_CHECK(!rom_normalize_prefix(ROM_BYTE_ORDER_Z64, normalized,
                                     sizeof(normalized), normalized + 1U, 4U));
}

void test_rom_header_streaming_boundaries(void)
{
    static const size_t chunks[] = { 1U, 3U, 2U, 7U, 1U, 5U, 11U, 4U, 9U };
    uint8_t canonical[ROM_HEADER_WITH_IPL3_BYTES];
    uint8_t encoded[ROM_HEADER_WITH_IPL3_BYTES];
    uint8_t encoded_before[ROM_HEADER_WITH_IPL3_BYTES];
    uint8_t normalized[ROM_HEADER_WITH_IPL3_BYTES];
    uint8_t carry[4] = { 0U, 0U, 0U, 0U };
    size_t carry_len;
    rom_byte_order_t order;

    rom_fixture_build_canonical(canonical);
    for (order = ROM_BYTE_ORDER_Z64; order <= ROM_BYTE_ORDER_N64; ++order) {
        size_t input_offset = 0U, output_offset = 0U, chunk_index = 0U;
        carry_len = 0U;
        rom_fixture_encode(order, canonical, sizeof(canonical), encoded);
        memcpy(encoded_before, encoded, sizeof(encoded));
        while (input_offset < sizeof(encoded)) {
            size_t amount = chunks[chunk_index++ % (sizeof(chunks) / sizeof(chunks[0]))];
            size_t produced;
            if (amount > sizeof(encoded) - input_offset) amount = sizeof(encoded) - input_offset;
            produced = rom_normalize_bytes(order, encoded + input_offset, amount,
                                           carry, &carry_len, normalized + output_offset,
                                           sizeof(normalized) - output_offset,
                                           input_offset + amount == sizeof(encoded));
            TEST_ASSERT(produced != SIZE_MAX);
            input_offset += amount;
            output_offset += produced;
        }
        TEST_CHECK(carry_len == 0U);
        TEST_CHECK(output_offset == sizeof(normalized));
        TEST_CHECK(memcmp(normalized, canonical, sizeof(canonical)) == 0);
        TEST_CHECK(memcmp(encoded, encoded_before, sizeof(encoded)) == 0);
    }

    carry_len = 0U;
    TEST_CHECK(rom_normalize_bytes(ROM_BYTE_ORDER_N64, canonical, 1U, carry,
                                   &carry_len, normalized, sizeof(normalized), false) == 0U);
    TEST_CHECK(carry_len == 1U);
    TEST_CHECK(rom_normalize_bytes(ROM_BYTE_ORDER_N64, canonical + 1U, 2U, carry,
                                   &carry_len, normalized, sizeof(normalized), true) == SIZE_MAX);
    TEST_CHECK(rom_normalize_bytes(ROM_BYTE_ORDER_V64, canonical, 1U, carry,
                                   &(size_t){0U}, normalized, sizeof(normalized), true) == SIZE_MAX);
}

void test_rom_header_streaming_capacity(void)
{
    uint8_t canonical[16];
    uint8_t encoded[16];
    rom_byte_order_t order;
    size_t index;

    for (index = 0U; index < sizeof(canonical); ++index) {
        canonical[index] = (uint8_t)(0x10U + index);
    }

    for (order = ROM_BYTE_ORDER_Z64; order <= ROM_BYTE_ORDER_N64; ++order) {
        uint8_t output[17];
        uint8_t carry[4] = { 0xa1U, 0xa2U, 0xa3U, 0xa4U };
        size_t carry_len = 0U;
        size_t produced;

        rom_fixture_encode(order, canonical, sizeof(canonical), encoded);
        memset(output, 0xcc, sizeof(output));
        produced = rom_normalize_bytes(order, encoded, sizeof(encoded), carry,
                                       &carry_len, output, sizeof(canonical), true);
        TEST_CHECK_(produced == sizeof(canonical), "exact capacity order %d", (int)order);
        TEST_CHECK_(carry_len == 0U, "exact capacity carry order %d", (int)order);
        TEST_CHECK_(memcmp(output, canonical, sizeof(canonical)) == 0,
                    "exact capacity bytes order %d", (int)order);
        TEST_CHECK_(output[sizeof(canonical)] == 0xccU,
                    "exact capacity guard order %d", (int)order);
    }

    for (order = ROM_BYTE_ORDER_V64; order <= ROM_BYTE_ORDER_N64; ++order) {
        const size_t unit = order == ROM_BYTE_ORDER_V64 ? 2U : 4U;
        const size_t first_amount = unit - 1U;
        const size_t second_amount = unit + 1U;
        uint8_t output[9];
        uint8_t carry[4] = { 0xa1U, 0xa2U, 0xa3U, 0xa4U };
        size_t carry_len = 0U;
        size_t produced;

        rom_fixture_encode(order, canonical, sizeof(canonical), encoded);
        memset(output, 0xcc, sizeof(output));
        produced = rom_normalize_bytes(order, encoded, first_amount, carry,
                                       &carry_len, output, 0U, false);
        TEST_CHECK_(produced == 0U, "buffer-only order %d", (int)order);
        TEST_CHECK_(carry_len == first_amount, "buffer-only carry order %d", (int)order);
        TEST_CHECK_(memcmp(carry, encoded, first_amount) == 0,
                    "buffer-only bytes order %d", (int)order);
        TEST_CHECK_(output[0] == 0xccU, "zero capacity output order %d", (int)order);

        produced = rom_normalize_bytes(order, encoded + first_amount, second_amount,
                                       carry, &carry_len, output, unit * 2U, false);
        TEST_CHECK_(produced == unit * 2U, "carry boundary order %d", (int)order);
        TEST_CHECK_(carry_len == 0U, "carry boundary length order %d", (int)order);
        TEST_CHECK_(memcmp(output, canonical, unit * 2U) == 0,
                    "carry boundary bytes order %d", (int)order);
        TEST_CHECK_(output[unit * 2U] == 0xccU,
                    "carry boundary guard order %d", (int)order);
    }
}

void test_rom_header_streaming_fail_closed(void)
{
    static const size_t units[] = { 1U, 2U, 4U };
    uint8_t input[8] = { 0x10U, 0x11U, 0x12U, 0x13U,
                         0x14U, 0x15U, 0x16U, 0x17U };
    rom_byte_order_t order;

    for (order = ROM_BYTE_ORDER_Z64; order <= ROM_BYTE_ORDER_N64; ++order) {
        const size_t unit = units[(size_t)order];
        uint8_t output[8];
        uint8_t output_before[8];
        uint8_t carry[4] = { 0xa1U, 0xa2U, 0xa3U, 0xa4U };
        uint8_t carry_before[4];
        size_t carry_len = 0U;
        size_t carry_len_before;

        memset(output, 0xcc, sizeof(output));
        memcpy(output_before, output, sizeof(output));
        memcpy(carry_before, carry, sizeof(carry));
        carry_len_before = carry_len;
        TEST_CHECK_(rom_normalize_bytes(order, input, unit, carry, &carry_len,
                                        output, unit - 1U, true) == SIZE_MAX,
                    "insufficient capacity order %d", (int)order);
        TEST_CHECK_(memcmp(output, output_before, sizeof(output)) == 0,
                    "insufficient output state order %d", (int)order);
        TEST_CHECK_(memcmp(carry, carry_before, sizeof(carry)) == 0,
                    "insufficient carry state order %d", (int)order);
        TEST_CHECK_(carry_len == carry_len_before,
                    "insufficient carry length order %d", (int)order);
    }

    for (order = ROM_BYTE_ORDER_V64; order <= ROM_BYTE_ORDER_N64; ++order) {
        uint8_t output[8];
        uint8_t output_before[8];
        uint8_t carry[4] = { 0xa1U, 0xa2U, 0xa3U, 0xa4U };
        uint8_t carry_before[4];
        size_t carry_len = order == ROM_BYTE_ORDER_V64 ? 0U : 1U;
        size_t carry_len_before = carry_len;

        memset(output, 0xcc, sizeof(output));
        memcpy(output_before, output, sizeof(output));
        memcpy(carry_before, carry, sizeof(carry));
        TEST_CHECK_(rom_normalize_bytes(order, input, 1U, carry, &carry_len,
                                        output, sizeof(output), true) == SIZE_MAX,
                    "malformed final tail order %d", (int)order);
        TEST_CHECK_(memcmp(output, output_before, sizeof(output)) == 0,
                    "malformed output state order %d", (int)order);
        TEST_CHECK_(memcmp(carry, carry_before, sizeof(carry)) == 0,
                    "malformed carry state order %d", (int)order);
        TEST_CHECK_(carry_len == carry_len_before,
                    "malformed carry length order %d", (int)order);
    }

    {
        uint8_t output[8];
        uint8_t output_before[8];
        uint8_t carry[4] = { 0xa1U, 0xa2U, 0xa3U, 0xa4U };
        uint8_t carry_before[4];
        size_t carry_len;
        size_t carry_len_before;

#define CHECK_INVALID(call, initial_len, label) do { \
            memset(output, 0xcc, sizeof(output)); \
            memcpy(output_before, output, sizeof(output)); \
            memcpy(carry_before, carry, sizeof(carry)); \
            carry_len = (initial_len); \
            carry_len_before = carry_len; \
            TEST_CHECK_((call) == SIZE_MAX, "%s return", (label)); \
            TEST_CHECK_(memcmp(output, output_before, sizeof(output)) == 0, \
                        "%s output state", (label)); \
            TEST_CHECK_(memcmp(carry, carry_before, sizeof(carry)) == 0, \
                        "%s carry state", (label)); \
            TEST_CHECK_(carry_len == carry_len_before, "%s carry length", (label)); \
        } while (0)

        CHECK_INVALID(rom_normalize_bytes((rom_byte_order_t)99, input, 1U,
                                          carry, &carry_len, output,
                                          sizeof(output), false),
                      0U, "invalid order");
        CHECK_INVALID(rom_normalize_bytes(ROM_BYTE_ORDER_V64, NULL, 1U,
                                          carry, &carry_len, output,
                                          sizeof(output), false),
                      0U, "null input");
        CHECK_INVALID(rom_normalize_bytes(ROM_BYTE_ORDER_V64, input, 2U,
                                          NULL, &carry_len, output,
                                          sizeof(output), true),
                      0U, "null carry");
        CHECK_INVALID(rom_normalize_bytes(ROM_BYTE_ORDER_V64, input, 2U,
                                          carry, NULL, output,
                                          sizeof(output), true),
                      0U, "null carry length");
        CHECK_INVALID(rom_normalize_bytes(ROM_BYTE_ORDER_V64, input, 2U,
                                          carry, &carry_len, NULL, 2U, true),
                      0U, "null output");
        CHECK_INVALID(rom_normalize_bytes(ROM_BYTE_ORDER_V64, input, 0U,
                                          carry, &carry_len, output,
                                          sizeof(output), false),
                      2U, "invalid carry length");
        CHECK_INVALID(rom_normalize_bytes(ROM_BYTE_ORDER_V64,
                                          (const uint8_t *)(uintptr_t)1U,
                                          SIZE_MAX, carry, &carry_len,
                                          output, sizeof(output), false),
                      1U, "input length overflow");
#undef CHECK_INVALID
    }
}
