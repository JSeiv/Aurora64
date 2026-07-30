#ifndef ROM_HEADER_H__
#define ROM_HEADER_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ROM_BYTE_ORDER_Z64,
    ROM_BYTE_ORDER_V64,
    ROM_BYTE_ORDER_N64
} rom_byte_order_t;

typedef enum {
    ROM_REGION_UNKNOWN,
    ROM_REGION_NTSC_J,
    ROM_REGION_NTSC_U,
    ROM_REGION_PAL,
    ROM_REGION_OTHER
} rom_region_t;

typedef struct {
    rom_byte_order_t byte_order;
    char title[21];
    char game_code[5];
    char cartridge_id[3];
    uint8_t country_code;
    rom_region_t region;
    uint8_t revision;
    uint32_t clock_rate;
    uint32_t boot_address;
    uint64_t check_code;
} rom_header_t;

#define ROM_HEADER_METADATA_BYTES 64
#define ROM_HEADER_WITH_IPL3_BYTES 0x1000

bool rom_header_parse(const uint8_t *bytes, size_t length, rom_header_t *out);

/*
 * Normalizes required_len bytes to canonical big-endian order. Exact in-place
 * normalization (input == output) is supported; other overlap is rejected.
 * A zero-length request succeeds for a valid order without dereferencing input
 * or output.
 */
bool rom_normalize_prefix(rom_byte_order_t order, const uint8_t *input,
                          size_t input_len, uint8_t *output,
                          size_t required_len);

/*
 * Returns SIZE_MAX on invalid arguments, insufficient output capacity, or a
 * malformed final tail. A zero return is valid when an incomplete unit is
 * buffered in carry. input and output must not overlap.
 */
size_t rom_normalize_bytes(rom_byte_order_t order, const uint8_t *input,
                           size_t input_len, uint8_t carry[4],
                           size_t *carry_len, uint8_t *output,
                           size_t output_capacity, bool final);

#endif
