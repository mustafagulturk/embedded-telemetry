#include "unity.h"
#include "fake_i2c.h"
#include "mpu6050.h"

static fake_i2c_t bus;
static fake_i2c_dev_t *imu;
static mpu6050_t dev;

void setUp(void)
{
    fake_i2c_init(&bus);
    imu = fake_i2c_add(&bus, MPU6050_ADDR_DEFAULT);
    imu->regs[MPU6050_REG_WHO_AM_I] = MPU6050_WHO_AM_I_VAL;
}
void tearDown(void) {}

static void put_be16(uint8_t *r, int16_t v)
{
    r[0] = (uint8_t)((uint16_t)v >> 8);
    r[1] = (uint8_t)((uint16_t)v & 0xFF);
}

static void test_init_sequence(void)
{
    mpu6050_cfg_t cfg = mpu6050_default_cfg();
    TEST_ASSERT_EQUAL(I2C_OK, mpu6050_init(&dev, &bus.bus, MPU6050_ADDR_DEFAULT, &cfg));
    TEST_ASSERT_TRUE(dev.ready);
    /* Reset first, then wake with PLL clock, then config registers. */
    TEST_ASSERT_EQUAL_INT(0, fake_i2c_find_write(&bus, 0x68, MPU6050_REG_PWR_MGMT_1));
    TEST_ASSERT_EQUAL_HEX8(0x80, bus.log[0].val);
    TEST_ASSERT_EQUAL_HEX8(0x01, imu->regs[MPU6050_REG_PWR_MGMT_1]);
    TEST_ASSERT_EQUAL_HEX8(9, imu->regs[MPU6050_REG_SMPLRT_DIV]);
    TEST_ASSERT_EQUAL_HEX8(3, imu->regs[MPU6050_REG_CONFIG]);
    TEST_ASSERT_EQUAL_HEX8(0x08, imu->regs[MPU6050_REG_GYRO_CONFIG]);
    TEST_ASSERT_EQUAL_HEX8(0x08, imu->regs[MPU6050_REG_ACCEL_CONFIG]);
    TEST_ASSERT_TRUE(bus.delay_total_ms >= 100);
}

static void test_init_wrong_whoami(void)
{
    mpu6050_cfg_t cfg = mpu6050_default_cfg();
    imu->regs[MPU6050_REG_WHO_AM_I] = 0x72;
    TEST_ASSERT_EQUAL(I2C_ERR_BUS, mpu6050_init(&dev, &bus.bus, MPU6050_ADDR_DEFAULT, &cfg));
    TEST_ASSERT_FALSE(dev.ready);
}

static void test_init_missing_device(void)
{
    mpu6050_cfg_t cfg = mpu6050_default_cfg();
    imu->nack = true;
    TEST_ASSERT_EQUAL(I2C_ERR_NACK, mpu6050_init(&dev, &bus.bus, MPU6050_ADDR_DEFAULT, &cfg));
}

static void test_init_bad_params(void)
{
    mpu6050_cfg_t cfg = mpu6050_default_cfg();
    cfg.dlpf_cfg = 7;
    TEST_ASSERT_EQUAL(I2C_ERR_PARAM, mpu6050_init(&dev, &bus.bus, 0x68, &cfg));
    TEST_ASSERT_EQUAL(I2C_ERR_PARAM, mpu6050_init(&dev, NULL, 0x68, &cfg));
}

static void test_burst_read_and_scaling(void)
{
    mpu6050_cfg_t cfg = mpu6050_default_cfg();
    TEST_ASSERT_EQUAL(I2C_OK, mpu6050_init(&dev, &bus.bus, 0x68, &cfg));
    uint8_t *r = &imu->regs[MPU6050_REG_ACCEL_XOUT_H];
    put_be16(&r[0], 8192);   /* +1 g at ±4 g */
    put_be16(&r[2], -4096);  /* -0.5 g */
    put_be16(&r[4], 0);
    put_be16(&r[6], -521);   /* ~35.0 C */
    put_be16(&r[8], 655);    /* 10 dps at ±500 */
    put_be16(&r[10], -32768);
    put_be16(&r[12], 33);

    mpu6050_raw_t raw;
    TEST_ASSERT_EQUAL(I2C_OK, mpu6050_read_raw(&dev, &raw));
    TEST_ASSERT_EQUAL_INT16(8192, raw.accel[0]);
    TEST_ASSERT_EQUAL_INT16(-4096, raw.accel[1]);
    TEST_ASSERT_EQUAL_INT16(-32768, raw.gyro[1]);
    TEST_ASSERT_EQUAL_INT16(1000, mpu6050_accel_to_mg(&dev, raw.accel[0]));
    TEST_ASSERT_EQUAL_INT16(-500, mpu6050_accel_to_mg(&dev, raw.accel[1]));
    TEST_ASSERT_EQUAL_INT16(100, mpu6050_gyro_to_ddps(&dev, raw.gyro[0]));
    TEST_ASSERT_EQUAL_INT16(-5003, mpu6050_gyro_to_ddps(&dev, raw.gyro[1]));
    TEST_ASSERT_EQUAL_INT16(5, mpu6050_gyro_to_ddps(&dev, raw.gyro[2]));
    TEST_ASSERT_INT16_WITHIN(1, 3500, mpu6050_temp_to_cdeg(raw.temp));
}

static void test_scaling_other_ranges(void)
{
    mpu6050_t d = { 0 };
    d.cfg.accel_fs = MPU6050_ACCEL_16G;
    d.cfg.gyro_fs = MPU6050_GYRO_2000;
    TEST_ASSERT_EQUAL_INT16(16000, mpu6050_accel_to_mg(&d, 32767)); /* saturates within int16 */
    TEST_ASSERT_EQUAL_INT16(-16000, mpu6050_accel_to_mg(&d, -32768));
    TEST_ASSERT_EQUAL_INT16(19980, mpu6050_gyro_to_ddps(&d, 32767));
    d.cfg.accel_fs = MPU6050_ACCEL_2G;
    TEST_ASSERT_EQUAL_INT16(2000, mpu6050_accel_to_mg(&d, 32767));
}

static void test_configure_recovers_without_reset(void)
{
    mpu6050_cfg_t cfg = mpu6050_default_cfg();
    TEST_ASSERT_EQUAL(I2C_OK, mpu6050_init(&dev, &bus.bus, 0x68, &cfg));
    /* Device drops off and comes back power-cycled (sleep, default config). */
    imu->nack = true;
    TEST_ASSERT_EQUAL(I2C_ERR_NACK, mpu6050_configure(&dev));
    TEST_ASSERT_FALSE(dev.ready);
    imu->nack = false;
    imu->regs[MPU6050_REG_PWR_MGMT_1] = 0x40;
    imu->regs[MPU6050_REG_ACCEL_CONFIG] = 0;
    bus.nlog = 0;
    bus.delay_total_ms = 0;
    TEST_ASSERT_EQUAL(I2C_OK, mpu6050_configure(&dev));
    TEST_ASSERT_TRUE(dev.ready);
    TEST_ASSERT_EQUAL_HEX8(0x01, imu->regs[MPU6050_REG_PWR_MGMT_1]);
    TEST_ASSERT_EQUAL_HEX8(0x08, imu->regs[MPU6050_REG_ACCEL_CONFIG]);
    for (int i = 0; i < bus.nlog; i++) {
        TEST_ASSERT_NOT_EQUAL(0x80, bus.log[i].val); /* no DEVICE_RESET written */
    }
    TEST_ASSERT_TRUE(bus.delay_total_ms < 100);
    TEST_ASSERT_EQUAL(I2C_ERR_PARAM, mpu6050_configure(NULL));
}

static void test_read_error_propagates(void)
{
    mpu6050_cfg_t cfg = mpu6050_default_cfg();
    TEST_ASSERT_EQUAL(I2C_OK, mpu6050_init(&dev, &bus.bus, 0x68, &cfg));
    imu->nack = true;
    mpu6050_raw_t raw;
    TEST_ASSERT_EQUAL(I2C_ERR_NACK, mpu6050_read_raw(&dev, &raw));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_sequence);
    RUN_TEST(test_init_wrong_whoami);
    RUN_TEST(test_init_missing_device);
    RUN_TEST(test_init_bad_params);
    RUN_TEST(test_burst_read_and_scaling);
    RUN_TEST(test_scaling_other_ranges);
    RUN_TEST(test_configure_recovers_without_reset);
    RUN_TEST(test_read_error_propagates);
    return UNITY_END();
}
