#include "mpu6050.h"

#include <stddef.h>

#define PWR_MGMT_1_DEVICE_RESET ((uint8_t)0x80U)
#define PWR_MGMT_1_CLK_PLL_GX   ((uint8_t)0x01U)
#define RESET_DELAY_MS          100U
#define WAKE_DELAY_MS           10U

/* LSB per g: 16384 >> fs ; LSB per (10 * deg/s) scaled by 10: 1310 >> fs (= 131.0 * 10) */
static const int32_t k_accel_lsb_per_g[4] = { 16384, 8192, 4096, 2048 };
static const int32_t k_gyro_lsb_per_10dps[4] = { 1310, 655, 328, 164 };

mpu6050_cfg_t mpu6050_default_cfg(void)
{
    mpu6050_cfg_t cfg;

    cfg.accel_fs = MPU6050_ACCEL_4G;
    cfg.gyro_fs = MPU6050_GYRO_500;
    cfg.dlpf_cfg = 3U;     /* 44 Hz accel / 42 Hz gyro bandwidth */
    cfg.smplrt_div = 9U;   /* 1 kHz / 10 = 100 Hz */
    return cfg;
}

static i2c_status_t write_reg(const mpu6050_t *dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2];

    buf[0] = reg;
    buf[1] = val;
    return dev->bus->write(dev->bus->ctx, dev->addr, buf, sizeof(buf));
}

i2c_status_t mpu6050_init(mpu6050_t *dev, const i2c_bus_t *bus, uint8_t addr,
                          const mpu6050_cfg_t *cfg)
{
    i2c_status_t st;

    if ((dev == NULL) || (bus == NULL) || (cfg == NULL) || (bus->write == NULL) ||
        (bus->read_regs == NULL) || ((uint32_t)cfg->accel_fs > 3U) ||
        ((uint32_t)cfg->gyro_fs > 3U) || (cfg->dlpf_cfg > 6U)) {
        return I2C_ERR_PARAM;
    }
    dev->bus = bus;
    dev->addr = addr;
    dev->cfg = *cfg;
    dev->ready = false;

    st = write_reg(dev, MPU6050_REG_PWR_MGMT_1, PWR_MGMT_1_DEVICE_RESET);
    if (st != I2C_OK) {
        return st;
    }
    if (bus->delay_ms != NULL) {
        bus->delay_ms(bus->ctx, RESET_DELAY_MS);
    }
    return mpu6050_configure(dev);
}

i2c_status_t mpu6050_configure(mpu6050_t *dev)
{
    i2c_status_t st;
    uint8_t who = 0U;

    if ((dev == NULL) || (dev->bus == NULL)) {
        return I2C_ERR_PARAM;
    }
    dev->ready = false;
    st = dev->bus->read_regs(dev->bus->ctx, dev->addr, MPU6050_REG_WHO_AM_I, &who, 1U);
    if (st != I2C_OK) {
        return st;
    }
    if (who != MPU6050_WHO_AM_I_VAL) {
        return I2C_ERR_BUS;
    }

    st = write_reg(dev, MPU6050_REG_PWR_MGMT_1, PWR_MGMT_1_CLK_PLL_GX);
    if (st == I2C_OK) {
        if (dev->bus->delay_ms != NULL) {
            dev->bus->delay_ms(dev->bus->ctx, WAKE_DELAY_MS);
        }
        st = write_reg(dev, MPU6050_REG_SMPLRT_DIV, dev->cfg.smplrt_div);
    }
    if (st == I2C_OK) {
        st = write_reg(dev, MPU6050_REG_CONFIG, dev->cfg.dlpf_cfg);
    }
    if (st == I2C_OK) {
        st = write_reg(dev, MPU6050_REG_GYRO_CONFIG, (uint8_t)((uint8_t)dev->cfg.gyro_fs << 3U));
    }
    if (st == I2C_OK) {
        st = write_reg(dev, MPU6050_REG_ACCEL_CONFIG, (uint8_t)((uint8_t)dev->cfg.accel_fs << 3U));
    }
    dev->ready = (st == I2C_OK);
    return st;
}

static int16_t be16(const uint8_t *b)
{
    return (int16_t)(uint16_t)(((uint16_t)b[0] << 8U) | (uint16_t)b[1]);
}

i2c_status_t mpu6050_read_raw(mpu6050_t *dev, mpu6050_raw_t *out)
{
    uint8_t b[14];
    i2c_status_t st;

    if ((dev == NULL) || (out == NULL) || (dev->bus == NULL)) {
        return I2C_ERR_PARAM;
    }
    st = dev->bus->read_regs(dev->bus->ctx, dev->addr, MPU6050_REG_ACCEL_XOUT_H, b, sizeof(b));
    if (st != I2C_OK) {
        return st;
    }
    out->accel[0] = be16(&b[0]);
    out->accel[1] = be16(&b[2]);
    out->accel[2] = be16(&b[4]);
    out->temp = be16(&b[6]);
    out->gyro[0] = be16(&b[8]);
    out->gyro[1] = be16(&b[10]);
    out->gyro[2] = be16(&b[12]);
    return I2C_OK;
}

static int16_t sat16(int32_t v)
{
    if (v > INT16_MAX) {
        return INT16_MAX;
    }
    if (v < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)v;
}

/* Rounded division, symmetric around zero. */
static int32_t div_round(int32_t num, int32_t den)
{
    return (num >= 0) ? ((num + (den / 2)) / den) : ((num - (den / 2)) / den);
}

int16_t mpu6050_accel_to_mg(const mpu6050_t *dev, int16_t raw)
{
    if (dev == NULL) {
        return 0;
    }
    return sat16(div_round((int32_t)raw * 1000, k_accel_lsb_per_g[dev->cfg.accel_fs]));
}

int16_t mpu6050_gyro_to_ddps(const mpu6050_t *dev, int16_t raw)
{
    if (dev == NULL) {
        return 0;
    }
    /* deg/s*10 = raw / (LSB per deg/s) * 10 = raw * 100 / (LSB per deg/s * 10) */
    return sat16(div_round((int32_t)raw * 100, k_gyro_lsb_per_10dps[dev->cfg.gyro_fs]));
}

int16_t mpu6050_temp_to_cdeg(int16_t raw)
{
    /* T[0.01 C] = raw * 100 / 340 + 3653 */
    return sat16(div_round((int32_t)raw * 100, 340) + 3653);
}
