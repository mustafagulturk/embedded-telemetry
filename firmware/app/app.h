/**
 * @file app.h
 * @brief Task layout, inter-task queues and shared counters.
 *
 *   sensor (prio 4, 100 Hz) --sample_q--> packer (prio 3) --frame_q--> tx (prio 2) --> USART2
 *                                            ^
 *   monitor (prio 5, 10 Hz) --stats_q (mailbox, 1 Hz)
 *
 * The monitor task owns the IWDG and refreshes it only while every other task
 * keeps checking in with the supervisor.
 */
#ifndef APP_H
#define APP_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "packet.h"
#include "supervisor.h"
#include "telemetry_types.h"

#define PRIO_MONITOR (tskIDLE_PRIORITY + 5U)
#define PRIO_SENSOR  (tskIDLE_PRIORITY + 4U)
#define PRIO_PACKER  (tskIDLE_PRIORITY + 3U)
#define PRIO_TX      (tskIDLE_PRIORITY + 2U)

#define SENSOR_PERIOD_MS 10U
#define MONITOR_PERIOD_MS 100U
#define STATS_PERIOD_MS 1000U
#define IWDG_TIMEOUT_MS 500U

#define SAMPLE_QUEUE_LEN 8U
#define FRAME_QUEUE_LEN 8U

typedef struct {
    uint8_t len;
    uint8_t data[PACKET_MAX_FRAME];
} tx_frame_t;

typedef struct {
    QueueHandle_t sample_q;   /* telemetry_sample_t */
    QueueHandle_t frame_q;    /* tx_frame_t */
    QueueHandle_t stats_q;    /* telemetry_stats_t, length 1, overwritten */
    TaskHandle_t tx_task;
    supervisor_t supervisor;
    int32_t sup_sensor;
    int32_t sup_packer;
    int32_t sup_tx;
    /* Written by one task, read by the monitor: 32-bit accesses are atomic on Cortex-M4. */
    volatile uint32_t sensor_max_us;
    volatile uint32_t packer_max_us;
    volatile uint32_t tx_max_us;
    volatile uint32_t queue_drops;
    volatile uint32_t i2c_errors;
    volatile uint8_t mode;
    volatile uint8_t sticky_faults;
    reset_cause_t reset_cause;
} app_ctx_t;

extern app_ctx_t g_app;

uint32_t app_now_ms(void);
/** Keep the maximum seen in @p slot (single writer). */
void app_track_max(volatile uint32_t *slot, uint32_t value);

void sensor_task(void *arg);
void packer_task(void *arg);
void tx_task(void *arg);
void monitor_task(void *arg);

#endif /* APP_H */
