#include <string.h>

#include "unity.h"
#include "crc16.h"
#include "packet.h"

void setUp(void) {}
void tearDown(void) {}

static telemetry_sample_t sample(void)
{
    telemetry_sample_t s = {
        .t_ms = 123456u,
        .accel_mg = { -12, 34, 1001 },
        .gyro_ddps = { -5000, 0, 32767 },
        .temp_cdeg = -1234,
        .pressure_pa = 100653u,
        .alt_raw_cm = -4567,
        .alt_filt_cm = 1234567,
        .mode = SYS_MODE_DEGRADED,
        .faults = FAULT_IMU | FAULT_WDG_RESET,
    };
    return s;
}

static void test_telemetry_frame_layout(void)
{
    telemetry_sample_t s = sample();
    uint8_t f[PACKET_MAX_FRAME];
    size_t n = packet_encode_telemetry(&s, 0xBEEF, f, sizeof f);

    TEST_ASSERT_EQUAL_size_t(41, n);
    TEST_ASSERT_EQUAL_HEX8(0xA5, f[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, f[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, f[2]);
    TEST_ASSERT_EQUAL_HEX8(PACKET_TYPE_TELEMETRY, f[3]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, f[4]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, f[5]);
    TEST_ASSERT_EQUAL_UINT8(32, f[6]);
    /* t_ms little-endian at payload offset 0 */
    TEST_ASSERT_EQUAL_HEX8(0x40, f[7]);
    TEST_ASSERT_EQUAL_HEX8(0xE2, f[8]);
    TEST_ASSERT_EQUAL_HEX8(0x01, f[9]);
    TEST_ASSERT_EQUAL_HEX8(0x00, f[10]);
    uint16_t crc = crc16_compute(&f[2], 5 + 32);
    TEST_ASSERT_EQUAL_HEX16(crc, get_u16le(&f[39]));
}

static void test_telemetry_roundtrip(void)
{
    telemetry_sample_t s = sample(), d;
    uint8_t f[PACKET_MAX_FRAME];
    size_t n = packet_encode_telemetry(&s, 7, f, sizeof f);
    TEST_ASSERT_TRUE(packet_decode_telemetry(&f[7], n - 9, &d));
    TEST_ASSERT_EQUAL_MEMORY(&s.t_ms, &d.t_ms, sizeof s.t_ms);
    TEST_ASSERT_EQUAL_INT16_ARRAY(s.accel_mg, d.accel_mg, 3);
    TEST_ASSERT_EQUAL_INT16_ARRAY(s.gyro_ddps, d.gyro_ddps, 3);
    TEST_ASSERT_EQUAL_INT16(s.temp_cdeg, d.temp_cdeg);
    TEST_ASSERT_EQUAL_UINT32(s.pressure_pa, d.pressure_pa);
    TEST_ASSERT_EQUAL_INT32(s.alt_raw_cm, d.alt_raw_cm);
    TEST_ASSERT_EQUAL_INT32(s.alt_filt_cm, d.alt_filt_cm);
    TEST_ASSERT_EQUAL_UINT8(s.mode, d.mode);
    TEST_ASSERT_EQUAL_UINT8(s.faults, d.faults);
}

static void test_stats_roundtrip(void)
{
    telemetry_stats_t s = { 99000u, 437, 812, 95, 120, 3, 17, RESET_CAUSE_WATCHDOG, SYS_MODE_FAILSAFE }, d;
    uint8_t f[PACKET_MAX_FRAME];
    size_t n = packet_encode_stats(&s, 1, f, sizeof f);
    TEST_ASSERT_EQUAL_size_t(PACKET_STATS_FRAME_LEN, n);
    TEST_ASSERT_TRUE(packet_decode_stats(&f[7], f[6], &d));
    TEST_ASSERT_EQUAL_MEMORY(&s, &d, sizeof s);
}

static void test_build_rejects_small_buffer_and_bad_args(void)
{
    telemetry_sample_t s = sample();
    uint8_t f[40];
    TEST_ASSERT_EQUAL_size_t(0, packet_encode_telemetry(&s, 0, f, sizeof f));
    TEST_ASSERT_EQUAL_size_t(0, packet_encode_telemetry(NULL, 0, f, 64));
    uint8_t big[PACKET_MAX_PAYLOAD + 1] = { 0 };
    uint8_t out[128];
    TEST_ASSERT_EQUAL_size_t(0, packet_build(1, 0, big, sizeof big, out, sizeof out));
    TEST_ASSERT_EQUAL_size_t(PACKET_OVERHEAD, packet_build(9, 0, NULL, 0, out, sizeof out));
    TEST_ASSERT_FALSE(packet_decode_telemetry(out, 31, &s));
}

static int feed_all(packet_parser_t *p, const uint8_t *b, size_t n, packet_frame_t *last)
{
    int frames = 0;
    for (size_t i = 0; i < n; i++) {
        if (packet_parser_feed(p, b[i]) == PARSE_FRAME) {
            frames++;
            *last = p->frame;
        }
    }
    return frames;
}

static void test_parser_with_garbage_and_resync(void)
{
    telemetry_sample_t s = sample();
    uint8_t stream[200];
    size_t n = 0;
    const uint8_t junk[] = { 0x00, 0xA5, 0x11, 0xA5, 0xA5, 0x5A, 0x07 }; /* last 3: fake sync + bad version */
    memcpy(&stream[n], junk, sizeof junk); n += sizeof junk;
    n += packet_encode_telemetry(&s, 10, &stream[n], sizeof stream - n);
    stream[n++] = 0xA5; /* stray byte between frames */
    n += packet_encode_telemetry(&s, 11, &stream[n], sizeof stream - n);

    packet_parser_t p;
    packet_frame_t last;
    packet_parser_init(&p);
    /* The false sync "A5 5A 07" must be rejected on the version byte so the
     * real frame right after it is not swallowed. */
    int frames = feed_all(&p, stream, n, &last);
    TEST_ASSERT_EQUAL_INT(2, frames);
    TEST_ASSERT_EQUAL_UINT16(11, last.seq);
    TEST_ASSERT_EQUAL_UINT8(PACKET_TYPE_TELEMETRY, last.type);
    TEST_ASSERT_EQUAL_UINT32(0, p.crc_errors);
    TEST_ASSERT_EQUAL_UINT32(1, p.format_errors);
}

static void test_parser_counts_crc_error(void)
{
    telemetry_sample_t s = sample();
    uint8_t stream[100];
    size_t n = packet_encode_telemetry(&s, 1, stream, sizeof stream);
    stream[20] ^= 0x01;
    n += packet_encode_telemetry(&s, 2, &stream[n], sizeof stream - n);
    packet_parser_t p;
    packet_frame_t last;
    packet_parser_init(&p);
    TEST_ASSERT_EQUAL_INT(1, feed_all(&p, stream, n, &last));
    TEST_ASSERT_EQUAL_UINT16(2, last.seq);
    TEST_ASSERT_EQUAL_UINT32(1, p.crc_errors);
}

static void test_parser_rejects_oversize_len(void)
{
    const uint8_t hdr[] = { 0xA5, 0x5A, 0x01, 0x01, 0x00, 0x00, PACKET_MAX_PAYLOAD + 1 };
    packet_parser_t p;
    packet_parser_init(&p);
    packet_parse_result_t r = PARSE_NONE;
    for (size_t i = 0; i < sizeof hdr; i++) r = packet_parser_feed(&p, hdr[i]);
    TEST_ASSERT_EQUAL(PARSE_ERROR, r);
    TEST_ASSERT_EQUAL_UINT32(1, p.format_errors);
    TEST_ASSERT_EQUAL(PARSER_WAIT_SYNC0, p.state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_telemetry_frame_layout);
    RUN_TEST(test_telemetry_roundtrip);
    RUN_TEST(test_stats_roundtrip);
    RUN_TEST(test_build_rejects_small_buffer_and_bad_args);
    RUN_TEST(test_parser_with_garbage_and_resync);
    RUN_TEST(test_parser_counts_crc_error);
    RUN_TEST(test_parser_rejects_oversize_len);
    return UNITY_END();
}
