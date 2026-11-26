#pragma once
#include <stddef.h>
#include <stdint.h>

// Zephyr's crc32_ieee_update: reflected polynomial 0xEDB88320, inverted in and out.
static inline uint32_t crc32_ieee_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
