/**
 * @file mpu6050.h
 * @brief InvenSense MPU-6050 6-axis IMU driver (register level, no vendor library).
 */
#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>
#include <stdint.h>

#include "i2c_bus.h"

#define MPU6050_ADDR_DEFAULT  ((uint8_t)0x68U)
#define MPU6050_WHO_AM_I_VAL  ((uint8_t)0x68U)

/* Register map (subset) */
#define MPU6050_REG_SMPLRT_DIV    ((uint8_t)0x19U)
#define MPU6050_REG_CONFIG        ((uint8_t)0x1AU)
#define MPU6050_REG_GYRO_CONFIG   ((uint8_t)0x1BU)
#define MPU6050_REG_ACCEL_CONFIG  ((uint8_t)0x1CU)
#define MPU6050_REG_ACCEL_XOUT_H  ((uint8_t)0x3BU)
#define MPU6050_REG_PWR_MGMT_1    ((uint8_t)0x6BU)
#define MPU6050_REG_WHO_AM_I      ((uint8_t)0x75U)

typedef enum { MPU6050_ACCEL_2G = 0, MPU6050_ACCEL_4G, MPU6050_ACCEL_8G, MPU6050_ACCEL_16G } mpu6050_accel_fs_t;
typedef enum { MPU6050_GYRO_250 = 0, MPU6050_GYRO_500, MPU6050_GYRO_1000, MPU6050_GYRO_2000 } mpu6050_gyro_fs_t;

typedef struct {
    mpu6050_accel_fs_t accel_fs;
    mpu6050_gyro_fs_t gyro_fs;
    uint8_t dlpf_cfg;      /* CONFIG.DLPF_CFG, 0..6 */
    uint8_t smplrt_div;    /* sample rate = 1 kHz / (1 + div) when DLPF enabled */
} mpu6050_cfg_t;

typedef struct {
    int16_t accel[3];
    int16_t temp;
    int16_t gyro[3];
} mpu6050_raw_t;

typedef struct {
    const i2c_bus_t *bus;
    uint8_t addr;
    mpu6050_cfg_t cfg;
    bool ready;
} mpu6050_t;

/** ±4 g, ±500 °/s, 44 Hz DLPF, 100 Hz output rate. */
mpu6050_cfg_t mpu6050_default_cfg(void);

/** Reset, verify WHO_AM_I, wake on gyro-X PLL, configure ranges. */
i2c_status_t mpu6050_init(mpu6050_t *dev, const i2c_bus_t *bus, uint8_t addr,
                          const mpu6050_cfg_t *cfg);

/**
 * Verify WHO_AM_I, wake and write the configuration without a device reset.
 * Used to recover a device that dropped off the bus: a power-cycled MPU-6050
 * is already in its reset state, so the 100 ms reset wait is not needed and
 * the 100 Hz sampling loop is not stalled. Requires a prior mpu6050_init().
 */
i2c_status_t mpu6050_configure(mpu6050_t *dev);

/** One 14-byte burst read of accel, temperature and gyro. */
i2c_status_t mpu6050_read_raw(mpu6050_t *dev, mpu6050_raw_t *out);

/** Raw accel count -> milli-g for the configured range. */
int16_t mpu6050_accel_to_mg(const mpu6050_t *dev, int16_t raw);

/** Raw gyro count -> 0.1 deg/s for the configured range. */
int16_t mpu6050_gyro_to_ddps(const mpu6050_t *dev, int16_t raw);

/** Raw temperature -> 0.01 degC (T = raw/340 + 36.53). */
int16_t mpu6050_temp_to_cdeg(int16_t raw);

#endif /* MPU6050_H */
