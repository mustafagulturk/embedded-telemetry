#include "unity.h"
#include "altitude.h"

void setUp(void) {}
void tearDown(void) {}

static void test_sea_level_is_zero(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, altitude_from_pressure(101325.0f, ALTITUDE_P0_STD_PA));
}

static void test_standard_atmosphere_points(void)
{
    /* ISA: 1000 m -> 89874.6 Pa, 100 m -> 100129.4 Pa */
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 1000.0f, altitude_from_pressure(89874.6f, ALTITUDE_P0_STD_PA));
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 100.0f, altitude_from_pressure(100129.4f, ALTITUDE_P0_STD_PA));
}

static void test_relative_to_ground_reference(void)
{
    /* With p0 = ground pressure the result is height above ground. */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, altitude_from_pressure(95000.0f, 95000.0f));
    TEST_ASSERT_TRUE(altitude_from_pressure(94000.0f, 95000.0f) > 80.0f);
}

static void test_invalid_inputs(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, altitude_from_pressure(0.0f, 101325.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, altitude_from_pressure(-5.0f, 101325.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, altitude_from_pressure(100000.0f, 0.0f));
}

static void test_m_to_cm(void)
{
    TEST_ASSERT_EQUAL_INT32(12346, altitude_m_to_cm(123.456f));
    TEST_ASSERT_EQUAL_INT32(-50, altitude_m_to_cm(-0.5f));
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, altitude_m_to_cm(1.0e9f));
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, altitude_m_to_cm(-1.0e9f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sea_level_is_zero);
    RUN_TEST(test_standard_atmosphere_points);
    RUN_TEST(test_relative_to_ground_reference);
    RUN_TEST(test_invalid_inputs);
    RUN_TEST(test_m_to_cm);
    return UNITY_END();
}
