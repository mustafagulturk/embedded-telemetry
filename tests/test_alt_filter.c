#include <math.h>
#include <stdlib.h>

#include "unity.h"
#include "alt_filter.h"

void setUp(void) {}
void tearDown(void) {}

/* Deterministic Gaussian noise (Box-Muller over a fixed LCG). */
static uint32_t lcg = 12345u;
static float urand(void)
{
    lcg = lcg * 1664525u + 1013904223u;
    return ((float)(lcg >> 8) + 0.5f) / 16777216.0f;
}
static float gauss(float sigma)
{
    float u1 = urand(), u2 = urand();
    return sigma * sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2);
}

static alt_filter_t make(void)
{
    alt_filter_t f;
    alt_filter_cfg_t cfg = alt_filter_default_cfg();
    TEST_ASSERT_TRUE(alt_filter_init(&f, &cfg));
    return f;
}

static void test_first_sample_seeds(void)
{
    alt_filter_t f = make();
    TEST_ASSERT_EQUAL_FLOAT(42.0f, alt_filter_update(&f, 42.0f, 0.01f));
}

static void test_rejects_bad_cfg(void)
{
    alt_filter_t f;
    alt_filter_cfg_t cfg = { 0.0f, 0.1f, 1.0f };
    TEST_ASSERT_FALSE(alt_filter_init(&f, &cfg));
    cfg.alpha = 1.5f;
    TEST_ASSERT_FALSE(alt_filter_init(&f, &cfg));
    TEST_ASSERT_FALSE(alt_filter_init(NULL, &cfg));
}

static void test_reduces_noise_on_static_altitude(void)
{
    alt_filter_t f = make();
    const float sigma = 0.5f;
    double raw_sq = 0.0, filt_sq = 0.0;
    int n = 0;
    for (int i = 0; i < 2000; i++) {
        float m = 100.0f + gauss(sigma);
        float e = alt_filter_update(&f, m, 0.01f);
        if (i >= 500) { /* after settling */
            raw_sq += (double)((m - 100.0f) * (m - 100.0f));
            filt_sq += (double)((e - 100.0f) * (e - 100.0f));
            n++;
        }
    }
    double raw_rms = sqrt(raw_sq / n), filt_rms = sqrt(filt_sq / n);
    /* Expect at least 3x noise reduction. */
    TEST_ASSERT_TRUE_MESSAGE(filt_rms * 3.0 < raw_rms, "filter did not reduce noise enough");
}

static void test_tracks_constant_climb_without_lag(void)
{
    alt_filter_t f = make();
    float e = 0.0f;
    for (int i = 0; i <= 1000; i++) { /* 10 s at 5 m/s */
        e = alt_filter_update(&f, 5.0f * (float)i * 0.01f, 0.01f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, e);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, f.vel_mps);
}

static void test_outlier_is_clamped(void)
{
    alt_filter_t f = make();
    for (int i = 0; i < 200; i++) (void)alt_filter_update(&f, 10.0f, 0.01f);
    float e = alt_filter_update(&f, 5000.0f, 0.01f); /* single glitch */
    /* Effect bounded by alpha * max_residual = 0.8 m */
    TEST_ASSERT_TRUE(e < 10.0f + 0.81f);
}

static void test_reset_reseeds(void)
{
    alt_filter_t f = make();
    (void)alt_filter_update(&f, 1.0f, 0.01f);
    alt_filter_reset(&f);
    TEST_ASSERT_EQUAL_FLOAT(77.0f, alt_filter_update(&f, 77.0f, 0.01f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_sample_seeds);
    RUN_TEST(test_rejects_bad_cfg);
    RUN_TEST(test_reduces_noise_on_static_altitude);
    RUN_TEST(test_tracks_constant_climb_without_lag);
    RUN_TEST(test_outlier_is_clamped);
    RUN_TEST(test_reset_reseeds);
    return UNITY_END();
}
