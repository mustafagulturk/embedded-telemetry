#include "crc16.h"

/* Bitwise implementation: 256 B of flash is not worth saving 8 shifts per byte
 * at 4 kB/s, and it keeps the code trivially reviewable. */
uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t len)
{
    uint16_t c = crc;

    if (data != NULL) {
        for (size_t i = 0U; i < len; i++) {
            c ^= (uint16_t)((uint16_t)data[i] << 8U);
            for (uint8_t bit = 0U; bit < 8U; bit++) {
                if ((c & 0x8000U) != 0U) {
                    c = (uint16_t)((uint16_t)(c << 1U) ^ 0x1021U);
                } else {
                    c = (uint16_t)(c << 1U);
                }
            }
        }
    }
    return c;
}

uint16_t crc16_compute(const uint8_t *data, size_t len)
{
    return crc16_update(CRC16_INIT, data, len);
}
