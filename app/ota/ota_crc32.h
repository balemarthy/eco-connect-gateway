/*
 * CRC-32 (IEEE 802.3 / zlib / PNG): polynomial 0x04C11DB7 reflected (0xEDB88320),
 * init 0xFFFFFFFF, final XOR 0xFFFFFFFF. Check value: "123456789" -> 0xCBF43926,
 * the same number `crc32` (zlib), Python zlib.crc32() and 7-Zip print for a file.
 *
 * Nibble table (16 entries, 64 B) instead of the 1 KB byte table: OTA verification
 * runs once per image, so size matters more than speed. Pure C, host tested.
 */
#ifndef OTA_CRC32_H
#define OTA_CRC32_H

#include <stddef.h>
#include <stdint.h>

#define OTA_CRC32_INIT  0xFFFFFFFFUL

/* Incremental: crc = OTA_CRC32_INIT; crc = ota_crc32_update(crc, ...) ...; ota_crc32_final(crc) */
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t len);
uint32_t ota_crc32_final(uint32_t crc);

/* One shot. */
uint32_t ota_crc32(const uint8_t *data, size_t len);

#endif /* OTA_CRC32_H */
