/**
 * @file tx_task.c
 * @brief Sends frames over USART2 using the interrupt-driven transmitter.
 *
 * The task sleeps on a task notification while the UART ISR shifts out the
 * frame, so transmission costs almost no CPU time.
 */
#include "app.h"

#include "bsp.h"

#define TX_TIMEOUT_MS 20U   /* 41 B at 115200 baud is ~3.6 ms */

static void tx_done_from_isr(void)
{
    BaseType_t woken = pdFALSE;

    vTaskNotifyGiveFromISR(g_app.tx_task, &woken);
    portYIELD_FROM_ISR(woken);
}

void tx_task(void *arg)
{
    tx_frame_t frame;

    (void)arg;
    bsp_uart_set_tx_done_callback(tx_done_from_isr);

    for (;;) {
        BaseType_t got = xQueueReceive(g_app.frame_q, &frame, pdMS_TO_TICKS(50U));

        supervisor_checkin(&g_app.supervisor, g_app.sup_tx, app_now_ms());
        if ((got == pdPASS) && (frame.len > 0U)) {
            uint32_t t0 = bsp_time_us();
            bool started = bsp_uart_tx_start(frame.data, frame.len);
            uint32_t busy_us = bsp_time_us() - t0;

            /* A missing completion leaves the task without check-ins for
             * longer than its supervisor deadline only if it repeats, which
             * then ends in a watchdog reset - the intended outcome. */
            if (started) {
                (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TX_TIMEOUT_MS));
            }
            app_track_max(&g_app.tx_max_us, busy_us);
        }
    }
}
