/**
 * @file monitor_task.c
 * @brief Highest-priority supervisor: watchdog gating, status LED, fail-safe
 *        output and 1 Hz statistics (CPU load, task execution times).
 */
#include "app.h"

#include "bsp.h"

static uint16_t sat_u16(uint32_t v)
{
    return (v > 0xFFFFU) ? (uint16_t)0xFFFFU : (uint16_t)v;
}

/* Read-and-clear a per-window maximum. */
static uint16_t take_max(volatile uint32_t *slot)
{
    uint32_t v = *slot;

    *slot = 0U;
    return sat_u16(v);
}

static void update_led(uint32_t tick, uint8_t mode)
{
    /* NORMAL: 1 Hz blink, DEGRADED: 2.5 Hz, FAILSAFE: 5 Hz (toggle every cycle). */
    uint32_t half_period = 5U;

    if (mode == (uint8_t)SYS_MODE_DEGRADED) {
        half_period = 2U;
    } else if (mode == (uint8_t)SYS_MODE_FAILSAFE) {
        half_period = 1U;
    } else {
        /* NORMAL */
    }
    if ((tick % half_period) == 0U) {
        bsp_led_toggle();
    }
}

void monitor_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    uint32_t tick = 0U;
    uint32_t last_total_us = bsp_time_us();
    configRUN_TIME_COUNTER_TYPE last_idle = ulTaskGetIdleRunTimeCounter();
    telemetry_stats_t st;

    (void)arg;
    bsp_iwdg_start(IWDG_TIMEOUT_MS);

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(MONITOR_PERIOD_MS));
        tick++;

        uint32_t late = supervisor_evaluate(&g_app.supervisor, app_now_ms());
        uint8_t mode = g_app.mode;

        if (late == 0U) {
            bsp_iwdg_refresh();
        }
        /* else: stop refreshing; the IWDG resets the MCU within IWDG_TIMEOUT_MS. */

        bsp_failsafe_output((mode == (uint8_t)SYS_MODE_FAILSAFE) || (late != 0U));
        update_led(tick, mode);

        if ((tick % (STATS_PERIOD_MS / MONITOR_PERIOD_MS)) == 0U) {
            uint32_t now_us = bsp_time_us();
            configRUN_TIME_COUNTER_TYPE idle = ulTaskGetIdleRunTimeCounter();
            uint32_t total = now_us - last_total_us;
            uint32_t idle_d = idle - last_idle;
            uint32_t load = 0U;

            if ((total > 0U) && (idle_d <= total)) {
                load = 1000U - (uint32_t)(((uint64_t)idle_d * 1000U) / total);
            }
            last_total_us = now_us;
            last_idle = idle;

            st.uptime_ms = app_now_ms();
            st.cpu_load_permille = sat_u16(load);
            st.sensor_max_us = take_max(&g_app.sensor_max_us);
            st.packer_max_us = take_max(&g_app.packer_max_us);
            st.tx_max_us = take_max(&g_app.tx_max_us);
            st.queue_drops = sat_u16(g_app.queue_drops);
            st.i2c_errors = sat_u16(g_app.i2c_errors);
            st.reset_cause = (uint8_t)g_app.reset_cause;
            st.mode = mode;
            (void)xQueueOverwrite(g_app.stats_q, &st);
        }
    }
}
