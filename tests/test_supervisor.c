#include "unity.h"
#include "supervisor.h"

void setUp(void) {}
void tearDown(void) {}

static void test_all_alive_kicks(void)
{
    supervisor_t s;
    supervisor_init(&s, 0);
    int32_t a = supervisor_register(&s, 50, 0);
    int32_t b = supervisor_register(&s, 200, 0);
    TEST_ASSERT_EQUAL_INT32(0, a);
    TEST_ASSERT_EQUAL_INT32(1, b);
    for (uint32_t t = 10; t <= 1000; t += 10) {
        supervisor_checkin(&s, a, t);
        if (t % 100 == 0) supervisor_checkin(&s, b, t);
        TEST_ASSERT_EQUAL_HEX32(0, supervisor_evaluate(&s, t));
    }
}

static void test_stuck_task_detected(void)
{
    supervisor_t s;
    supervisor_init(&s, 0);
    int32_t a = supervisor_register(&s, 50, 0);
    int32_t b = supervisor_register(&s, 50, 0);
    supervisor_checkin(&s, a, 40);
    supervisor_checkin(&s, b, 40);
    supervisor_checkin(&s, a, 80);
    TEST_ASSERT_EQUAL_HEX32(0, supervisor_evaluate(&s, 90));
    TEST_ASSERT_EQUAL_HEX32(1u << 1, supervisor_evaluate(&s, 91));
}

static void test_tick_wraparound(void)
{
    supervisor_t s;
    uint32_t t0 = 0xFFFFFFF0u;
    supervisor_init(&s, t0);
    int32_t a = supervisor_register(&s, 100, t0);
    supervisor_checkin(&s, a, t0 + 0x20u); /* wrapped to 0x10 */
    TEST_ASSERT_EQUAL_HEX32(0, supervisor_evaluate(&s, 0x50u));
    TEST_ASSERT_EQUAL_HEX32(1, supervisor_evaluate(&s, 0x75u));
}

static void test_register_limits(void)
{
    supervisor_t s;
    supervisor_init(&s, 0);
    TEST_ASSERT_EQUAL_INT32(-1, supervisor_register(&s, 0, 0));
    for (uint32_t i = 0; i < SUPERVISOR_MAX_TASKS; i++) {
        TEST_ASSERT_EQUAL_INT32((int32_t)i, supervisor_register(&s, 10, 0));
    }
    TEST_ASSERT_EQUAL_INT32(-1, supervisor_register(&s, 10, 0));
    supervisor_checkin(&s, 99, 5); /* ignored */
    TEST_ASSERT_EQUAL_HEX32(UINT32_MAX, supervisor_evaluate(NULL, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_all_alive_kicks);
    RUN_TEST(test_stuck_task_detected);
    RUN_TEST(test_tick_wraparound);
    RUN_TEST(test_register_limits);
    return UNITY_END();
}
