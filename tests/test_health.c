#include "unity.h"
#include "health.h"

void setUp(void) {}
void tearDown(void) {}

static health_t h;

static void init_default(void)
{
    health_cfg_t cfg = health_default_cfg();
    TEST_ASSERT_TRUE(health_init(&h, &cfg));
}

static void fail(health_sensor_t s, int n) { for (int i = 0; i < n; i++) health_report(&h, s, false); }
static void ok(health_sensor_t s, int n) { for (int i = 0; i < n; i++) health_report(&h, s, true); }

static void test_starts_normal(void)
{
    init_default();
    TEST_ASSERT_EQUAL(SYS_MODE_NORMAL, health_mode(&h));
    TEST_ASSERT_EQUAL_HEX8(0, health_fault_flags(&h));
}

static void test_transient_error_does_not_fault(void)
{
    init_default();
    fail(HEALTH_SENSOR_IMU, 2);
    ok(HEALTH_SENSOR_IMU, 1);
    fail(HEALTH_SENSOR_IMU, 2);
    TEST_ASSERT_FALSE(health_is_faulted(&h, HEALTH_SENSOR_IMU));
    TEST_ASSERT_EQUAL_UINT32(4, h.total_errors);
}

static void test_imu_loss_is_degraded(void)
{
    init_default();
    fail(HEALTH_SENSOR_IMU, 3);
    TEST_ASSERT_TRUE(health_is_faulted(&h, HEALTH_SENSOR_IMU));
    TEST_ASSERT_EQUAL(SYS_MODE_DEGRADED, health_mode(&h));
    TEST_ASSERT_EQUAL_HEX8(FAULT_IMU, health_fault_flags(&h));
}

static void test_baro_loss_is_failsafe(void)
{
    init_default();
    fail(HEALTH_SENSOR_BARO, 3);
    TEST_ASSERT_EQUAL(SYS_MODE_FAILSAFE, health_mode(&h));
    fail(HEALTH_SENSOR_IMU, 3);
    TEST_ASSERT_EQUAL(SYS_MODE_FAILSAFE, health_mode(&h));
    TEST_ASSERT_EQUAL_HEX8(FAULT_IMU | FAULT_BARO, health_fault_flags(&h));
}

static void test_recovery_needs_sustained_good_reads(void)
{
    init_default();
    fail(HEALTH_SENSOR_BARO, 5);
    ok(HEALTH_SENSOR_BARO, 49);
    TEST_ASSERT_EQUAL(SYS_MODE_FAILSAFE, health_mode(&h));
    fail(HEALTH_SENSOR_BARO, 1); /* glitch restarts the count */
    ok(HEALTH_SENSOR_BARO, 49);
    TEST_ASSERT_TRUE(health_is_faulted(&h, HEALTH_SENSOR_BARO));
    ok(HEALTH_SENSOR_BARO, 1);
    TEST_ASSERT_EQUAL(SYS_MODE_NORMAL, health_mode(&h));
}

static void test_retry_pacing(void)
{
    health_cfg_t cfg = { 3, 10, 4 };
    TEST_ASSERT_TRUE(health_init(&h, &cfg));
    TEST_ASSERT_FALSE(health_should_retry(&h, HEALTH_SENSOR_IMU)); /* healthy: never */
    fail(HEALTH_SENSOR_IMU, 3);
    int retries = 0;
    for (int i = 0; i < 12; i++) {
        if (health_should_retry(&h, HEALTH_SENSOR_IMU)) retries++;
    }
    TEST_ASSERT_EQUAL_INT(3, retries);
}

static void test_mark_faulted_then_recover(void)
{
    init_default();
    health_mark_faulted(&h, HEALTH_SENSOR_IMU);
    TEST_ASSERT_EQUAL(SYS_MODE_DEGRADED, health_mode(&h));
    TEST_ASSERT_EQUAL_UINT32(0, h.total_errors);
    ok(HEALTH_SENSOR_IMU, 50);
    TEST_ASSERT_EQUAL(SYS_MODE_NORMAL, health_mode(&h));
}

static void test_bad_args(void)
{
    health_cfg_t cfg = { 0, 1, 1 };
    TEST_ASSERT_FALSE(health_init(&h, &cfg));
    TEST_ASSERT_EQUAL(SYS_MODE_FAILSAFE, health_mode(NULL));
    TEST_ASSERT_TRUE(health_is_faulted(NULL, HEALTH_SENSOR_IMU));
    init_default();
    health_report(&h, HEALTH_SENSOR_COUNT, false); /* ignored */
    TEST_ASSERT_EQUAL_UINT32(0, h.total_errors);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_normal);
    RUN_TEST(test_transient_error_does_not_fault);
    RUN_TEST(test_imu_loss_is_degraded);
    RUN_TEST(test_baro_loss_is_failsafe);
    RUN_TEST(test_recovery_needs_sustained_good_reads);
    RUN_TEST(test_retry_pacing);
    RUN_TEST(test_mark_faulted_then_recover);
    RUN_TEST(test_bad_args);
    return UNITY_END();
}
