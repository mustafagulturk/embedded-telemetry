/**
 * @file crc16.h
 * @brief CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, xorout 0).
 */
#ifndef CRC16_H
#define CRC16_H

#include <stddef.h>
#include <stdint.h>

#define CRC16_INIT ((uint16_t)0xFFFFU)

/** Continue a running CRC over @p len bytes. Start with CRC16_INIT. */
uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t len);

/** One-shot CRC over a buffer. */
uint16_t crc16_compute(const uint8_t *data, size_t len);

#endif /* CRC16_H */
