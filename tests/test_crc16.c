#include "unity.h"
#include "crc16.h"

void setUp(void) {}
void tearDown(void) {}

static void test_check_value(void)
{
    const uint8_t s[] = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_compute(s, 9));
}

static void test_empty_is_init(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16_compute(NULL, 0));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16_compute((const uint8_t *)"x", 0));
}

static void test_incremental_equals_oneshot(void)
{
    const uint8_t s[] = "The quick brown fox";
    uint16_t c = CRC16_INIT;
    for (size_t i = 0; i < sizeof(s) - 1; i++) {
        c = crc16_update(c, &s[i], 1);
    }
    TEST_ASSERT_EQUAL_HEX16(crc16_compute(s, sizeof(s) - 1), c);
}

static void test_single_bit_flip_detected(void)
{
    uint8_t buf[32];
    for (int i = 0; i < 32; i++) buf[i] = (uint8_t)(i * 7);
    uint16_t ref = crc16_compute(buf, sizeof buf);
    for (int byte = 0; byte < 32; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            buf[byte] ^= (uint8_t)(1u << bit);
            TEST_ASSERT_NOT_EQUAL(ref, crc16_compute(buf, sizeof buf));
            buf[byte] ^= (uint8_t)(1u << bit);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_value);
    RUN_TEST(test_empty_is_init);
    RUN_TEST(test_incremental_equals_oneshot);
    RUN_TEST(test_single_bit_flip_detected);
    return UNITY_END();
}
