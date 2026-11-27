#include "ota_crc32.h"

/* crc32 of the 16 nibble values, reflected polynomial 0xEDB88320 */
static const uint32_t nibble_table[16] = {
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
    0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
    0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
    0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL,
};

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0U; i < len; i++) {
        crc ^= data[i];
        crc = (crc >> 4) ^ nibble_table[crc & 0x0FU];
        crc = (crc >> 4) ^ nibble_table[crc & 0x0FU];
    }
    return crc;
}

uint32_t ota_crc32_final(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFUL;
}

uint32_t ota_crc32(const uint8_t *data, size_t len)
{
    return ota_crc32_final(ota_crc32_update(OTA_CRC32_INIT, data, len));
}
