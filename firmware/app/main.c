/**
 * @file main.c
 * @brief Static creation of all RTOS objects and scheduler start.
 */
#include "app.h"
#include "bsp.h"

app_ctx_t g_app;

#define STACK_WORDS_SENSOR  512U
#define STACK_WORDS_PACKER  384U
#define STACK_WORDS_TX      256U
#define STACK_WORDS_MONITOR 384U

static StackType_t s_stack_sensor[STACK_WORDS_SENSOR];
static StackType_t s_stack_packer[STACK_WORDS_PACKER];
static StackType_t s_stack_tx[STACK_WORDS_TX];
static StackType_t s_stack_monitor[STACK_WORDS_MONITOR];
static StaticTask_t s_tcb_sensor;
static StaticTask_t s_tcb_packer;
static StaticTask_t s_tcb_tx;
static StaticTask_t s_tcb_monitor;

static uint8_t s_sample_q_storage[SAMPLE_QUEUE_LEN * sizeof(telemetry_sample_t)];
static uint8_t s_frame_q_storage[FRAME_QUEUE_LEN * sizeof(tx_frame_t)];
static uint8_t s_stats_q_storage[sizeof(telemetry_stats_t)];
static StaticQueue_t s_sample_q;
static StaticQueue_t s_frame_q;
static StaticQueue_t s_stats_q;

uint32_t app_now_ms(void)
{
    return (uint32_t)xTaskGetTickCount() * (1000U / configTICK_RATE_HZ);
}

void app_track_max(volatile uint32_t *slot, uint32_t value)
{
    if (value > *slot) {
        *slot = value;
    }
}

int main(void)
{
    bsp_init();

    g_app.reset_cause = bsp_reset_cause();
    if (g_app.reset_cause == RESET_CAUSE_WATCHDOG) {
        g_app.sticky_faults |= FAULT_WDG_RESET;
    }

    g_app.sample_q = xQueueCreateStatic(SAMPLE_QUEUE_LEN, sizeof(telemetry_sample_t),
                                        s_sample_q_storage, &s_sample_q);
    g_app.frame_q = xQueueCreateStatic(FRAME_QUEUE_LEN, sizeof(tx_frame_t),
                                       s_frame_q_storage, &s_frame_q);
    g_app.stats_q = xQueueCreateStatic(1U, sizeof(telemetry_stats_t),
                                       s_stats_q_storage, &s_stats_q);

    /* Deadlines are generous multiples of each task's period. The sensor task
     * may block up to ~150 ms while re-initialising a sensor. */
    supervisor_init(&g_app.supervisor, 0U);
    g_app.sup_sensor = supervisor_register(&g_app.supervisor, 250U, 0U);
    g_app.sup_packer = supervisor_register(&g_app.supervisor, 250U, 0U);
    g_app.sup_tx = supervisor_register(&g_app.supervisor, 250U, 0U);

    (void)xTaskCreateStatic(monitor_task, "monitor", STACK_WORDS_MONITOR, NULL,
                            PRIO_MONITOR, s_stack_monitor, &s_tcb_monitor);
    (void)xTaskCreateStatic(sensor_task, "sensor", STACK_WORDS_SENSOR, NULL,
                            PRIO_SENSOR, s_stack_sensor, &s_tcb_sensor);
    (void)xTaskCreateStatic(packer_task, "packer", STACK_WORDS_PACKER, NULL,
                            PRIO_PACKER, s_stack_packer, &s_tcb_packer);
    g_app.tx_task = xTaskCreateStatic(tx_task, "tx", STACK_WORDS_TX, NULL,
                                      PRIO_TX, s_stack_tx, &s_tcb_tx);

    configASSERT((g_app.sample_q != NULL) && (g_app.frame_q != NULL) &&
                 (g_app.stats_q != NULL) && (g_app.tx_task != NULL) &&
                 (g_app.sup_tx >= 0));

    vTaskStartScheduler();
    bsp_fatal(__FILE__, __LINE__); /* only reached if the scheduler could not start */
    return 0;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name);
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    bsp_fatal(__FILE__, __LINE__);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, configSTACK_DEPTH_TYPE *words);
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, configSTACK_DEPTH_TYPE *words)
{
    static StaticTask_t s_idle_tcb;
    static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE];

    *tcb = &s_idle_tcb;
    *stack = s_idle_stack;
    *words = configMINIMAL_STACK_SIZE;
}
