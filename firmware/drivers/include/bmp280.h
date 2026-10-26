/**
 * @file bmp280.h
 * @brief Bosch BMP280 pressure/temperature driver (register level, no vendor library).
 *
 * Compensation uses the datasheet's integer formulas (32-bit temperature,
 * 64-bit pressure), so results are bit-exact with the reference implementation.
 */
#ifndef BMP280_H
#define BMP280_H

#include <stdbool.h>
#include <stdint.h>

#include "i2c_bus.h"

#define BMP280_ADDR_DEFAULT  ((uint8_t)0x76U)
#define BMP280_CHIP_ID_VAL   ((uint8_t)0x58U)

#define BMP280_REG_CALIB00   ((uint8_t)0x88U)
#define BMP280_REG_CHIP_ID   ((uint8_t)0xD0U)
#define BMP280_REG_RESET     ((uint8_t)0xE0U)
#define BMP280_REG_STATUS    ((uint8_t)0xF3U)
#define BMP280_REG_CTRL_MEAS ((uint8_t)0xF4U)
#define BMP280_REG_CONFIG    ((uint8_t)0xF5U)
#define BMP280_REG_PRESS_MSB ((uint8_t)0xF7U)

#define BMP280_RESET_WORD    ((uint8_t)0xB6U)
#define BMP280_CALIB_LEN     24U

typedef struct {
    uint16_t dig_t1;
    int16_t dig_t2;
    int16_t dig_t3;
    uint16_t dig_p1;
    int16_t dig_p2;
    int16_t dig_p3;
    int16_t dig_p4;
    int16_t dig_p5;
    int16_t dig_p6;
    int16_t dig_p7;
    int16_t dig_p8;
    int16_t dig_p9;
} bmp280_calib_t;

typedef struct {
    uint8_t ctrl_meas;   /* osrs_t | osrs_p | mode */
    uint8_t config;      /* t_sb | filter | spi3w_en */
} bmp280_cfg_t;

typedef struct {
    const i2c_bus_t *bus;
    uint8_t addr;
    bmp280_cfg_t cfg;
    bmp280_calib_t calib;
    bool ready;
} bmp280_t;

typedef struct {
    int32_t temp_cdeg;    /* 0.01 degC */
    uint32_t pressure_pa; /* Pa (integer, Q24.8 rounded) */
} bmp280_meas_t;

/** Normal mode, osrs_t x1, osrs_p x4, t_standby 0.5 ms, IIR off (~ 100 Hz). */
bmp280_cfg_t bmp280_default_cfg(void);

i2c_status_t bmp280_init(bmp280_t *dev, const i2c_bus_t *bus, uint8_t addr,
                         const bmp280_cfg_t *cfg);

/** Burst-read 0xF7..0xFC and compensate. */
i2c_status_t bmp280_read(bmp280_t *dev, bmp280_meas_t *out);

/* Pure helpers, exposed for unit tests. */
void bmp280_parse_calib(const uint8_t raw[BMP280_CALIB_LEN], bmp280_calib_t *out);
int32_t bmp280_compensate_temp(const bmp280_calib_t *c, int32_t adc_t, int32_t *t_fine);
/** Returns pressure in Pa as Q24.8 (divide by 256), 0 on invalid input. */
uint32_t bmp280_compensate_press_q24_8(const bmp280_calib_t *c, int32_t adc_p, int32_t t_fine);

#endif /* BMP280_H */
