/**
 * @file packet.h
 * @brief Binary telemetry framing (see docs/packet_format.md).
 *
 * Frame: A5 5A | ver | type | seq(u16 LE) | len | payload[len] | crc16(LE)
 * CRC-16/CCITT-FALSE is computed over ver..payload.
 */
#ifndef PACKET_H
#define PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "telemetry_types.h"

#define PACKET_SYNC0           ((uint8_t)0xA5U)
#define PACKET_SYNC1           ((uint8_t)0x5AU)
#define PACKET_VERSION         ((uint8_t)0x01U)
#define PACKET_TYPE_TELEMETRY  ((uint8_t)0x01U)
#define PACKET_TYPE_STATS      ((uint8_t)0x02U)

#define PACKET_HEADER_LEN      7U
#define PACKET_CRC_LEN         2U
#define PACKET_OVERHEAD        (PACKET_HEADER_LEN + PACKET_CRC_LEN)
#define PACKET_MAX_PAYLOAD     64U
#define PACKET_MAX_FRAME       (PACKET_OVERHEAD + PACKET_MAX_PAYLOAD)

#define PACKET_TELEMETRY_PAYLOAD_LEN 32U
#define PACKET_STATS_PAYLOAD_LEN     18U
#define PACKET_TELEMETRY_FRAME_LEN   (PACKET_OVERHEAD + PACKET_TELEMETRY_PAYLOAD_LEN)
#define PACKET_STATS_FRAME_LEN       (PACKET_OVERHEAD + PACKET_STATS_PAYLOAD_LEN)

/**
 * Build a frame around an already serialized payload.
 * @return frame length, or 0 if @p out_cap is too small / payload too long.
 */
size_t packet_build(uint8_t type, uint16_t seq, const uint8_t *payload,
                    size_t payload_len, uint8_t *out, size_t out_cap);

size_t packet_encode_telemetry(const telemetry_sample_t *s, uint16_t seq,
                               uint8_t *out, size_t out_cap);

size_t packet_encode_stats(const telemetry_stats_t *s, uint16_t seq,
                           uint8_t *out, size_t out_cap);

bool packet_decode_telemetry(const uint8_t *payload, size_t len,
                             telemetry_sample_t *out);

bool packet_decode_stats(const uint8_t *payload, size_t len,
                         telemetry_stats_t *out);

/* ---- Streaming parser (byte at a time, no allocation) ---- */

typedef enum {
    PARSER_WAIT_SYNC0 = 0,
    PARSER_WAIT_SYNC1,
    PARSER_HEADER,
    PARSER_PAYLOAD,
    PARSER_CRC
} packet_parser_state_t;

typedef struct {
    uint8_t type;
    uint16_t seq;
    uint8_t len;
    uint8_t payload[PACKET_MAX_PAYLOAD];
} packet_frame_t;

typedef struct {
    packet_parser_state_t state;
    uint8_t hdr[5];          /* ver, type, seq lo, seq hi, len */
    uint8_t idx;
    uint8_t crc_bytes[2];
    packet_frame_t frame;
    uint32_t crc_errors;
    uint32_t format_errors;
} packet_parser_t;

typedef enum {
    PARSE_NONE = 0,     /* need more bytes */
    PARSE_FRAME,        /* parser->frame holds a valid frame */
    PARSE_ERROR         /* CRC or header error, parser resynchronised */
} packet_parse_result_t;

void packet_parser_init(packet_parser_t *p);
packet_parse_result_t packet_parser_feed(packet_parser_t *p, uint8_t byte);

/* ---- Little-endian helpers (exposed for drivers/tests) ---- */
void put_u16le(uint8_t *b, uint16_t v);
void put_u32le(uint8_t *b, uint32_t v);
uint16_t get_u16le(const uint8_t *b);
uint32_t get_u32le(const uint8_t *b);

#endif /* PACKET_H */
