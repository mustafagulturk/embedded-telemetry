/**
 * @file telemetry_types.h
 * @brief Shared data types passed between tasks (fixed-point physical units).
 */
#ifndef TELEMETRY_TYPES_H
#define TELEMETRY_TYPES_H

#include <stdint.h>

typedef enum {
    SYS_MODE_NORMAL = 0,
    SYS_MODE_DEGRADED = 1,
    SYS_MODE_FAILSAFE = 2
} sys_mode_t;

/* Fault flag bits (telemetry `faults` byte). */
#define FAULT_IMU            ((uint8_t)0x01U)
#define FAULT_BARO           ((uint8_t)0x02U)
#define FAULT_QUEUE_OVERFLOW ((uint8_t)0x04U)
#define FAULT_WDG_RESET      ((uint8_t)0x08U)

typedef enum {
    RESET_CAUSE_POWER_ON = 0,
    RESET_CAUSE_WATCHDOG = 1,
    RESET_CAUSE_SOFTWARE = 2,
    RESET_CAUSE_OTHER = 3
} reset_cause_t;

/** One 100 Hz sample, produced by the sensor task. */
typedef struct {
    uint32_t t_ms;
    int16_t accel_mg[3];
    int16_t gyro_ddps[3];   /* 0.1 deg/s */
    int16_t temp_cdeg;      /* 0.01 degC */
    uint32_t pressure_pa;
    int32_t alt_raw_cm;
    int32_t alt_filt_cm;
    uint8_t mode;           /* sys_mode_t */
    uint8_t faults;         /* FAULT_* bits */
} telemetry_sample_t;

/** 1 Hz system statistics. */
typedef struct {
    uint32_t uptime_ms;
    uint16_t cpu_load_permille;
    uint16_t sensor_max_us;
    uint16_t packer_max_us;
    uint16_t tx_max_us;
    uint16_t queue_drops;
    uint16_t i2c_errors;
    uint8_t reset_cause;    /* reset_cause_t */
    uint8_t mode;           /* sys_mode_t */
} telemetry_stats_t;

#endif /* TELEMETRY_TYPES_H */
