#include "unity.h"
#include "fake_i2c.h"
#include "bmp280.h"

static fake_i2c_t bus;
static fake_i2c_dev_t *baro;
static bmp280_t dev;

/* Bosch datasheet (BST-BMP280-DS001) section 3.12 example trimming values. */
static const int32_t k_trim[12] = { 27504, 26435, -1000, 36477, -10685, 3024,
                                    2855, 140, -7, 15500, -14600, 6000 };

static void load_trim(uint8_t *regs)
{
    for (int i = 0; i < 12; i++) {
        uint16_t v = (uint16_t)k_trim[i];
        regs[0x88 + 2 * i] = (uint8_t)(v & 0xFF);
        regs[0x88 + 2 * i + 1] = (uint8_t)(v >> 8);
    }
}

static void set_adc(int32_t adc_p, int32_t adc_t)
{
    uint8_t *r = &baro->regs[BMP280_REG_PRESS_MSB];
    r[0] = (uint8_t)(adc_p >> 12);
    r[1] = (uint8_t)(adc_p >> 4);
    r[2] = (uint8_t)((adc_p & 0xF) << 4);
    r[3] = (uint8_t)(adc_t >> 12);
    r[4] = (uint8_t)(adc_t >> 4);
    r[5] = (uint8_t)((adc_t & 0xF) << 4);
}

void setUp(void)
{
    fake_i2c_init(&bus);
    baro = fake_i2c_add(&bus, BMP280_ADDR_DEFAULT);
    baro->regs[BMP280_REG_CHIP_ID] = BMP280_CHIP_ID_VAL;
    load_trim(baro->regs);
}
void tearDown(void) {}

static void test_parse_calib(void)
{
    bmp280_calib_t c;
    bmp280_parse_calib(&baro->regs[0x88], &c);
    TEST_ASSERT_EQUAL_UINT16(27504, c.dig_t1);
    TEST_ASSERT_EQUAL_INT16(-1000, c.dig_t3);
    TEST_ASSERT_EQUAL_UINT16(36477, c.dig_p1);
    TEST_ASSERT_EQUAL_INT16(-14600, c.dig_p8);
    TEST_ASSERT_EQUAL_INT16(6000, c.dig_p9);
}

static void test_datasheet_compensation_example(void)
{
    bmp280_calib_t c;
    int32_t t_fine;
    bmp280_parse_calib(&baro->regs[0x88], &c);
    TEST_ASSERT_EQUAL_INT32(2508, bmp280_compensate_temp(&c, 519888, &t_fine));
    TEST_ASSERT_EQUAL_INT32(128422, t_fine);
    /* Bit-exact result of Bosch's 64-bit reference code (re-computed with
     * Python big integers). The datasheet table prints 25767236; it was
     * produced by a slightly different calculation. Both are 100653.2x Pa. */
    TEST_ASSERT_EQUAL_UINT32(25767233u, bmp280_compensate_press_q24_8(&c, 415148, t_fine));
}

static void test_init_sequence(void)
{
    bmp280_cfg_t cfg = bmp280_default_cfg();
    TEST_ASSERT_EQUAL(I2C_OK, bmp280_init(&dev, &bus.bus, BMP280_ADDR_DEFAULT, &cfg));
    TEST_ASSERT_TRUE(dev.ready);
    int reset = fake_i2c_find_write(&bus, 0x76, BMP280_REG_RESET);
    int config = fake_i2c_find_write(&bus, 0x76, BMP280_REG_CONFIG);
    int meas = fake_i2c_find_write(&bus, 0x76, BMP280_REG_CTRL_MEAS);
    TEST_ASSERT_TRUE(reset >= 0 && config > reset && meas > config);
    TEST_ASSERT_EQUAL_HEX8(0xB6, bus.log[reset].val);
    TEST_ASSERT_EQUAL_HEX8(0x2F, baro->regs[BMP280_REG_CTRL_MEAS]);
}

static void test_init_errors(void)
{
    bmp280_cfg_t cfg = bmp280_default_cfg();
    baro->regs[BMP280_REG_CHIP_ID] = 0x60; /* BME280 */
    TEST_ASSERT_EQUAL(I2C_ERR_BUS, bmp280_init(&dev, &bus.bus, 0x76, &cfg));
    baro->regs[BMP280_REG_CHIP_ID] = BMP280_CHIP_ID_VAL;
    baro->regs[0x88] = 0; baro->regs[0x89] = 0; /* blank dig_T1 */
    TEST_ASSERT_EQUAL(I2C_ERR_BUS, bmp280_init(&dev, &bus.bus, 0x76, &cfg));
    baro->nack = true;
    TEST_ASSERT_EQUAL(I2C_ERR_NACK, bmp280_init(&dev, &bus.bus, 0x76, &cfg));
}

static void test_read_measurement(void)
{
    bmp280_cfg_t cfg = bmp280_default_cfg();
    TEST_ASSERT_EQUAL(I2C_OK, bmp280_init(&dev, &bus.bus, 0x76, &cfg));
    set_adc(415148, 519888);
    bmp280_meas_t m;
    TEST_ASSERT_EQUAL(I2C_OK, bmp280_read(&dev, &m));
    TEST_ASSERT_EQUAL_INT32(2508, m.temp_cdeg);
    TEST_ASSERT_EQUAL_UINT32(100653u, m.pressure_pa);
}

static void test_skipped_measurement_is_error(void)
{
    bmp280_cfg_t cfg = bmp280_default_cfg();
    TEST_ASSERT_EQUAL(I2C_OK, bmp280_init(&dev, &bus.bus, 0x76, &cfg));
    set_adc(0x80000, 519888); /* reset value: no conversion yet */
    bmp280_meas_t m;
    TEST_ASSERT_EQUAL(I2C_ERR_BUS, bmp280_read(&dev, &m));
}

static void test_disconnect_during_operation(void)
{
    bmp280_cfg_t cfg = bmp280_default_cfg();
    TEST_ASSERT_EQUAL(I2C_OK, bmp280_init(&dev, &bus.bus, 0x76, &cfg));
    baro->fail_after = baro->transfers; /* bus timeouts from now on */
    bmp280_meas_t m;
    TEST_ASSERT_EQUAL(I2C_ERR_TIMEOUT, bmp280_read(&dev, &m));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_calib);
    RUN_TEST(test_datasheet_compensation_example);
    RUN_TEST(test_init_sequence);
    RUN_TEST(test_init_errors);
    RUN_TEST(test_read_measurement);
    RUN_TEST(test_skipped_measurement_is_error);
    RUN_TEST(test_disconnect_during_operation);
    return UNITY_END();
}
