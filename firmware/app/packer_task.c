/**
 * @file packer_task.c
 * @brief Turns samples and statistics into CRC-protected frames.
 *
 * Owns the single frame sequence counter shared by all packet types, so the
 * ground station can measure loss from gaps in `seq`.
 */
#include "app.h"

#include "bsp.h"

static uint16_t s_seq;

static void push_frame(const tx_frame_t *f)
{
    if (xQueueSend(g_app.frame_q, f, 0U) != pdPASS) {
        g_app.queue_drops++;
    }
}

void packer_task(void *arg)
{
    telemetry_sample_t sample;
    telemetry_stats_t stats;
    tx_frame_t frame;

    (void)arg;
    for (;;) {
        BaseType_t got = xQueueReceive(g_app.sample_q, &sample, pdMS_TO_TICKS(50U));
        uint32_t t0 = bsp_time_us();

        supervisor_checkin(&g_app.supervisor, g_app.sup_packer, app_now_ms());
        if (got == pdPASS) {
            frame.len = (uint8_t)packet_encode_telemetry(&sample, s_seq, frame.data,
                                                         sizeof(frame.data));
            s_seq++;
            push_frame(&frame);
        }
        if (xQueueReceive(g_app.stats_q, &stats, 0U) == pdPASS) {
            frame.len = (uint8_t)packet_encode_stats(&stats, s_seq, frame.data,
                                                     sizeof(frame.data));
            s_seq++;
            push_frame(&frame);
        }
        app_track_max(&g_app.packer_max_us, bsp_time_us() - t0);
    }
}
