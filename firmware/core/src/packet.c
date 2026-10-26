#include "packet.h"

#include <string.h>

#include "crc16.h"

void put_u16le(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t)(v & 0xFFU);
    b[1] = (uint8_t)((v >> 8U) & 0xFFU);
}

void put_u32le(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v & 0xFFU);
    b[1] = (uint8_t)((v >> 8U) & 0xFFU);
    b[2] = (uint8_t)((v >> 16U) & 0xFFU);
    b[3] = (uint8_t)((v >> 24U) & 0xFFU);
}

uint16_t get_u16le(const uint8_t *b)
{
    return (uint16_t)((uint16_t)b[0] | (uint16_t)((uint16_t)b[1] << 8U));
}

uint32_t get_u32le(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U) | ((uint32_t)b[2] << 16U) |
           ((uint32_t)b[3] << 24U);
}

size_t packet_build(uint8_t type, uint16_t seq, const uint8_t *payload,
                    size_t payload_len, uint8_t *out, size_t out_cap)
{
    size_t total = PACKET_OVERHEAD + payload_len;

    if ((out == NULL) || (payload_len > PACKET_MAX_PAYLOAD) || (out_cap < total) ||
        ((payload == NULL) && (payload_len > 0U))) {
        return 0U;
    }

    out[0] = PACKET_SYNC0;
    out[1] = PACKET_SYNC1;
    out[2] = PACKET_VERSION;
    out[3] = type;
    put_u16le(&out[4], seq);
    out[6] = (uint8_t)payload_len;
    if (payload_len > 0U) {
        (void)memcpy(&out[PACKET_HEADER_LEN], payload, payload_len);
    }
    put_u16le(&out[PACKET_HEADER_LEN + payload_len],
              crc16_compute(&out[2], (PACKET_HEADER_LEN - 2U) + payload_len));
    return total;
}

size_t packet_encode_telemetry(const telemetry_sample_t *s, uint16_t seq,
                               uint8_t *out, size_t out_cap)
{
    uint8_t pl[PACKET_TELEMETRY_PAYLOAD_LEN];

    if (s == NULL) {
        return 0U;
    }
    put_u32le(&pl[0], s->t_ms);
    for (size_t i = 0U; i < 3U; i++) {
        put_u16le(&pl[4U + (2U * i)], (uint16_t)s->accel_mg[i]);
        put_u16le(&pl[10U + (2U * i)], (uint16_t)s->gyro_ddps[i]);
    }
    put_u16le(&pl[16], (uint16_t)s->temp_cdeg);
    put_u32le(&pl[18], s->pressure_pa);
    put_u32le(&pl[22], (uint32_t)s->alt_raw_cm);
    put_u32le(&pl[26], (uint32_t)s->alt_filt_cm);
    pl[30] = s->mode;
    pl[31] = s->faults;
    return packet_build(PACKET_TYPE_TELEMETRY, seq, pl, sizeof(pl), out, out_cap);
}

size_t packet_encode_stats(const telemetry_stats_t *s, uint16_t seq,
                           uint8_t *out, size_t out_cap)
{
    uint8_t pl[PACKET_STATS_PAYLOAD_LEN];

    if (s == NULL) {
        return 0U;
    }
    put_u32le(&pl[0], s->uptime_ms);
    put_u16le(&pl[4], s->cpu_load_permille);
    put_u16le(&pl[6], s->sensor_max_us);
    put_u16le(&pl[8], s->packer_max_us);
    put_u16le(&pl[10], s->tx_max_us);
    put_u16le(&pl[12], s->queue_drops);
    put_u16le(&pl[14], s->i2c_errors);
    pl[16] = s->reset_cause;
    pl[17] = s->mode;
    return packet_build(PACKET_TYPE_STATS, seq, pl, sizeof(pl), out, out_cap);
}

bool packet_decode_telemetry(const uint8_t *payload, size_t len,
                             telemetry_sample_t *out)
{
    if ((payload == NULL) || (out == NULL) || (len != PACKET_TELEMETRY_PAYLOAD_LEN)) {
        return false;
    }
    out->t_ms = get_u32le(&payload[0]);
    for (size_t i = 0U; i < 3U; i++) {
        out->accel_mg[i] = (int16_t)get_u16le(&payload[4U + (2U * i)]);
        out->gyro_ddps[i] = (int16_t)get_u16le(&payload[10U + (2U * i)]);
    }
    out->temp_cdeg = (int16_t)get_u16le(&payload[16]);
    out->pressure_pa = get_u32le(&payload[18]);
    out->alt_raw_cm = (int32_t)get_u32le(&payload[22]);
    out->alt_filt_cm = (int32_t)get_u32le(&payload[26]);
    out->mode = payload[30];
    out->faults = payload[31];
    return true;
}

bool packet_decode_stats(const uint8_t *payload, size_t len,
                         telemetry_stats_t *out)
{
    if ((payload == NULL) || (out == NULL) || (len != PACKET_STATS_PAYLOAD_LEN)) {
        return false;
    }
    out->uptime_ms = get_u32le(&payload[0]);
    out->cpu_load_permille = get_u16le(&payload[4]);
    out->sensor_max_us = get_u16le(&payload[6]);
    out->packer_max_us = get_u16le(&payload[8]);
    out->tx_max_us = get_u16le(&payload[10]);
    out->queue_drops = get_u16le(&payload[12]);
    out->i2c_errors = get_u16le(&payload[14]);
    out->reset_cause = payload[16];
    out->mode = payload[17];
    return true;
}

void packet_parser_init(packet_parser_t *p)
{
    if (p != NULL) {
        (void)memset(p, 0, sizeof(*p));
        p->state = PARSER_WAIT_SYNC0;
    }
}

static packet_parse_result_t parser_finish(packet_parser_t *p)
{
    uint16_t crc = crc16_update(CRC16_INIT, p->hdr, sizeof(p->hdr));

    crc = crc16_update(crc, p->frame.payload, p->frame.len);
    p->state = PARSER_WAIT_SYNC0;
    if (crc != get_u16le(p->crc_bytes)) {
        p->crc_errors++;
        return PARSE_ERROR;
    }
    p->frame.type = p->hdr[1];
    p->frame.seq = get_u16le(&p->hdr[2]);
    return PARSE_FRAME;
}

packet_parse_result_t packet_parser_feed(packet_parser_t *p, uint8_t byte)
{
    packet_parse_result_t res = PARSE_NONE;

    if (p == NULL) {
        return PARSE_ERROR;
    }

    switch (p->state) {
    case PARSER_WAIT_SYNC0:
        if (byte == PACKET_SYNC0) {
            p->state = PARSER_WAIT_SYNC1;
        }
        break;
    case PARSER_WAIT_SYNC1:
        if (byte == PACKET_SYNC1) {
            p->state = PARSER_HEADER;
            p->idx = 0U;
        } else if (byte != PACKET_SYNC0) {
            p->state = PARSER_WAIT_SYNC0;
        } else {
            /* A5 A5 5A: stay, second A5 may start the frame */
        }
        break;
    case PARSER_HEADER:
        if ((p->idx == 0U) && (byte != PACKET_VERSION)) {
            /* Reject a false sync as early as possible so a real frame that
             * starts inside the bogus "header" is not swallowed. */
            p->format_errors++;
            p->state = (byte == PACKET_SYNC0) ? PARSER_WAIT_SYNC1 : PARSER_WAIT_SYNC0;
            res = PARSE_ERROR;
        } else {
            p->hdr[p->idx] = byte;
            p->idx++;
            if (p->idx == (uint8_t)sizeof(p->hdr)) {
                if (p->hdr[4] > PACKET_MAX_PAYLOAD) {
                    p->format_errors++;
                    p->state = PARSER_WAIT_SYNC0;
                    res = PARSE_ERROR;
                } else {
                    p->frame.len = p->hdr[4];
                    p->idx = 0U;
                    p->state = (p->frame.len > 0U) ? PARSER_PAYLOAD : PARSER_CRC;
                }
            }
        }
        break;
    case PARSER_PAYLOAD:
        p->frame.payload[p->idx] = byte;
        p->idx++;
        if (p->idx == p->frame.len) {
            p->idx = 0U;
            p->state = PARSER_CRC;
        }
        break;
    case PARSER_CRC:
        p->crc_bytes[p->idx] = byte;
        p->idx++;
        if (p->idx == 2U) {
            res = parser_finish(p);
        }
        break;
    default:
        p->state = PARSER_WAIT_SYNC0;
        break;
    }
    return res;
}
