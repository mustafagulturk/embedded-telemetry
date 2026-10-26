/**
 * @file sensor_task.c
 * @brief 100 Hz acquisition: IMU + barometer over I2C, altitude, filtering,
 *        sensor health and mode decisions.
 */
#include "app.h"

#include "alt_filter.h"
#include "altitude.h"
#include "bmp280.h"
#include "bsp.h"
#include "health.h"
#include "mpu6050.h"

#define GROUND_CAL_SAMPLES   50U    /* 0.5 s of pressure averaged as ground reference */
#define OVERFLOW_FLAG_HOLD   100U   /* keep FAULT_QUEUE_OVERFLOW visible for 1 s */

static i2c_bus_t s_bus;
static mpu6050_t s_imu;
static bmp280_t s_baro;
static health_t s_health;
static alt_filter_t s_filter;

static uint32_t s_ground_sum;
static uint32_t s_ground_n;
static float s_p0_pa = ALTITUDE_P0_STD_PA;

/* While the scheduler runs, sensor reset delays block only this task. */
static void rtos_delay_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static bool imu_init(void)
{
    mpu6050_cfg_t cfg = mpu6050_default_cfg();

    return mpu6050_init(&s_imu, &s_bus, MPU6050_ADDR_DEFAULT, &cfg) == I2C_OK;
}

static bool baro_init(void)
{
    bmp280_cfg_t cfg = bmp280_default_cfg();

    return bmp280_init(&s_baro, &s_bus, BMP280_ADDR_DEFAULT, &cfg) == I2C_OK;
}

/* Read the IMU into @p s. While faulted, reads stop and re-init is attempted
 * once per retry period instead of hammering a missing device every cycle. */
static void sample_imu(telemetry_sample_t *s)
{
    mpu6050_raw_t raw;

    if (s_imu.ready) {
        bool ok = (mpu6050_read_raw(&s_imu, &raw) == I2C_OK);

        health_report(&s_health, HEALTH_SENSOR_IMU, ok);
        if (ok) {
            for (uint32_t i = 0U; i < 3U; i++) {
                s->accel_mg[i] = mpu6050_accel_to_mg(&s_imu, raw.accel[i]);
                s->gyro_ddps[i] = mpu6050_gyro_to_ddps(&s_imu, raw.gyro[i]);
            }
        } else if (health_is_faulted(&s_health, HEALTH_SENSOR_IMU)) {
            s_imu.ready = false;
        } else {
            /* transient error: keep trying next cycle */
        }
    } else if (health_should_retry(&s_health, HEALTH_SENSOR_IMU)) {
        (void)imu_init();
    } else {
        /* faulted, waiting for the next retry slot */
    }
}

static void update_altitude(telemetry_sample_t *s, uint32_t pressure_pa, float dt_s)
{
    float alt_m;

    if (s_ground_n < GROUND_CAL_SAMPLES) {
        s_ground_sum += pressure_pa;
        s_ground_n++;
        s_p0_pa = (float)s_ground_sum / (float)s_ground_n;
    }
    alt_m = altitude_from_pressure((float)pressure_pa, s_p0_pa);
    s->alt_raw_cm = altitude_m_to_cm(alt_m);
    s->alt_filt_cm = altitude_m_to_cm(alt_filter_update(&s_filter, alt_m, dt_s));
}

static void sample_baro(telemetry_sample_t *s, float dt_s)
{
    bmp280_meas_t m;

    if (s_baro.ready) {
        bool ok = (bmp280_read(&s_baro, &m) == I2C_OK);

        health_report(&s_health, HEALTH_SENSOR_BARO, ok);
        if (ok) {
            s->temp_cdeg = (int16_t)m.temp_cdeg;
            s->pressure_pa = m.pressure_pa;
            update_altitude(s, m.pressure_pa, dt_s);
        } else if (health_is_faulted(&s_health, HEALTH_SENSOR_BARO)) {
            s_baro.ready = false;
            alt_filter_reset(&s_filter);  /* do not bridge a gap with stale velocity */
        } else {
            /* transient error: last values are kept */
        }
    } else if (health_should_retry(&s_health, HEALTH_SENSOR_BARO)) {
        (void)baro_init();
    } else {
        /* faulted, waiting for the next retry slot */
    }
}

void sensor_task(void *arg)
{
    telemetry_sample_t s = { 0 };
    health_cfg_t hcfg = health_default_cfg();
    alt_filter_cfg_t fcfg = alt_filter_default_cfg();
    TickType_t wake = xTaskGetTickCount();
    uint32_t last_us = bsp_time_us();
    uint32_t overflow_hold = 0U;

    (void)arg;
    s_bus = *bsp_i2c1_bus();
    s_bus.delay_ms = rtos_delay_ms;
    configASSERT(health_init(&s_health, &hcfg));
    configASSERT(alt_filter_init(&s_filter, &fcfg));

    if (!imu_init()) {
        health_mark_faulted(&s_health, HEALTH_SENSOR_IMU);
    }
    if (!baro_init()) {
        health_mark_faulted(&s_health, HEALTH_SENSOR_BARO);
    }

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
        uint32_t t0 = bsp_time_us();
        float dt_s = (float)(t0 - last_us) * 1.0e-6f;
        last_us = t0;

        supervisor_checkin(&g_app.supervisor, g_app.sup_sensor, app_now_ms());
        s.t_ms = app_now_ms();
        sample_imu(&s);
        sample_baro(&s, dt_s);

        if (health_is_faulted(&s_health, HEALTH_SENSOR_IMU)) {
            for (uint32_t i = 0U; i < 3U; i++) {
                s.accel_mg[i] = 0;
                s.gyro_ddps[i] = 0;
            }
        }
        s.mode = (uint8_t)health_mode(&s_health);
        s.faults = (uint8_t)(health_fault_flags(&s_health) | g_app.sticky_faults);
        if (overflow_hold > 0U) {
            overflow_hold--;
            s.faults |= FAULT_QUEUE_OVERFLOW;
        }
        g_app.mode = s.mode;
        g_app.i2c_errors = s_health.total_errors;

        if (xQueueSend(g_app.sample_q, &s, 0U) != pdPASS) {
            g_app.queue_drops++;
            overflow_hold = OVERFLOW_FLAG_HOLD;
        }
        app_track_max(&g_app.sensor_max_us, bsp_time_us() - t0);
    }
}
