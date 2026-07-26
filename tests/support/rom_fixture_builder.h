#ifndef ROM_FIXTURE_BUILDER_H__
#define ROM_FIXTURE_BUILDER_H__

#include <stddef.h>
#include <stdint.h>

#include "menu/library/rom_header.h"

void rom_fixture_build_canonical(uint8_t bytes[ROM_HEADER_WITH_IPL3_BYTES]);
/* length must be divisible by 2 for V64 and by 4 for N64. */
void rom_fixture_encode(rom_byte_order_t order, const uint8_t *canonical,
                        size_t length, uint8_t *encoded);

#endif
