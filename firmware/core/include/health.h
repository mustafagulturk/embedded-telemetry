/**
 * @file health.h
 * @brief Sensor health tracking and system mode (NORMAL / DEGRADED / FAILSAFE).
 *
 * A sensor is declared faulted after @c fail_threshold consecutive failed reads
 * and recovers after @c recover_threshold consecutive good reads. While faulted,
 * health_should_retry() paces re-initialisation attempts.
 *
 * Mode policy:
 *   - no sensor faulted                 -> NORMAL
 *   - IMU faulted only                  -> DEGRADED (altitude still valid)
 *   - barometer faulted (alone or both) -> FAILSAFE (altitude is the primary output)
 */
#ifndef HEALTH_H
#define HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "telemetry_types.h"

typedef enum {
    HEALTH_SENSOR_IMU = 0,
    HEALTH_SENSOR_BARO = 1,
    HEALTH_SENSOR_COUNT = 2
} health_sensor_t;

typedef struct {
    uint16_t fail_threshold;
    uint16_t recover_threshold;
    uint16_t retry_period;      /* samples between re-init attempts while faulted */
} health_cfg_t;

typedef struct {
    uint16_t consecutive_errors;
    uint16_t consecutive_ok;
    uint16_t retry_countdown;
    bool faulted;
} health_sensor_state_t;

typedef struct {
    health_cfg_t cfg;
    health_sensor_state_t sensor[HEALTH_SENSOR_COUNT];
    uint32_t total_errors;
} health_t;

health_cfg_t health_default_cfg(void);
bool health_init(health_t *h, const health_cfg_t *cfg);

/** Record the outcome of one read attempt. */
void health_report(health_t *h, health_sensor_t s, bool ok);

/** Declare a sensor faulted immediately (e.g. it failed to initialise at boot). */
void health_mark_faulted(health_t *h, health_sensor_t s);

bool health_is_faulted(const health_t *h, health_sensor_t s);

/** True when a faulted sensor should be re-initialised now (paced by retry_period). */
bool health_should_retry(health_t *h, health_sensor_t s);

sys_mode_t health_mode(const health_t *h);

/** FAULT_IMU / FAULT_BARO bits for the current state. */
uint8_t health_fault_flags(const health_t *h);

#endif /* HEALTH_H */
