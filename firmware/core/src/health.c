#include "health.h"

#include <stddef.h>
#include <string.h>

health_cfg_t health_default_cfg(void)
{
    health_cfg_t cfg;

    cfg.fail_threshold = 3U;      /* 30 ms at 100 Hz */
    cfg.recover_threshold = 50U;  /* 0.5 s of clean data */
    cfg.retry_period = 100U;      /* re-init once per second */
    return cfg;
}

bool health_init(health_t *h, const health_cfg_t *cfg)
{
    if ((h == NULL) || (cfg == NULL) || (cfg->fail_threshold == 0U) ||
        (cfg->recover_threshold == 0U) || (cfg->retry_period == 0U)) {
        return false;
    }
    (void)memset(h, 0, sizeof(*h));
    h->cfg = *cfg;
    return true;
}

static bool valid_sensor(health_sensor_t s)
{
    return ((uint32_t)s < (uint32_t)HEALTH_SENSOR_COUNT);
}

void health_report(health_t *h, health_sensor_t s, bool ok)
{
    health_sensor_state_t *st;

    if ((h == NULL) || !valid_sensor(s)) {
        return;
    }
    st = &h->sensor[s];
    if (ok) {
        st->consecutive_errors = 0U;
        if (st->consecutive_ok < UINT16_MAX) {
            st->consecutive_ok++;
        }
        if (st->faulted && (st->consecutive_ok >= h->cfg.recover_threshold)) {
            st->faulted = false;
        }
    } else {
        h->total_errors++;
        st->consecutive_ok = 0U;
        if (st->consecutive_errors < UINT16_MAX) {
            st->consecutive_errors++;
        }
        if (!st->faulted && (st->consecutive_errors >= h->cfg.fail_threshold)) {
            st->faulted = true;
            st->retry_countdown = h->cfg.retry_period;
        }
    }
}

void health_mark_faulted(health_t *h, health_sensor_t s)
{
    if ((h != NULL) && valid_sensor(s) && !h->sensor[s].faulted) {
        h->sensor[s].faulted = true;
        h->sensor[s].consecutive_ok = 0U;
        h->sensor[s].retry_countdown = h->cfg.retry_period;
    }
}

bool health_is_faulted(const health_t *h, health_sensor_t s)
{
    if ((h == NULL) || !valid_sensor(s)) {
        return true;
    }
    return h->sensor[s].faulted;
}

bool health_should_retry(health_t *h, health_sensor_t s)
{
    health_sensor_state_t *st;

    if ((h == NULL) || !valid_sensor(s)) {
        return false;
    }
    st = &h->sensor[s];
    if (!st->faulted) {
        return false;
    }
    if (st->retry_countdown > 0U) {
        st->retry_countdown--;
    }
    if (st->retry_countdown == 0U) {
        st->retry_countdown = h->cfg.retry_period;
        return true;
    }
    return false;
}

sys_mode_t health_mode(const health_t *h)
{
    if (h == NULL) {
        return SYS_MODE_FAILSAFE;
    }
    if (h->sensor[HEALTH_SENSOR_BARO].faulted) {
        return SYS_MODE_FAILSAFE;
    }
    if (h->sensor[HEALTH_SENSOR_IMU].faulted) {
        return SYS_MODE_DEGRADED;
    }
    return SYS_MODE_NORMAL;
}

uint8_t health_fault_flags(const health_t *h)
{
    uint8_t f = 0U;

    if (h != NULL) {
        if (h->sensor[HEALTH_SENSOR_IMU].faulted) {
            f |= FAULT_IMU;
        }
        if (h->sensor[HEALTH_SENSOR_BARO].faulted) {
            f |= FAULT_BARO;
        }
    }
    return f;
}
