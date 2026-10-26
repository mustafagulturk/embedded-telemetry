#include "bmp280.h"

#include <stddef.h>

#define RESET_DELAY_MS 5U
#define ADC_SKIPPED    0x80000

bmp280_cfg_t bmp280_default_cfg(void)
{
    bmp280_cfg_t cfg;

    /* osrs_t = x1 (001), osrs_p = x4 (011), mode = normal (11) */
    cfg.ctrl_meas = (uint8_t)((1U << 5U) | (3U << 2U) | 3U);
    /* t_sb = 0.5 ms (000), filter off (000) - filtering is done in software */
    cfg.config = 0x00U;
    return cfg;
}

static uint16_t le_u16(const uint8_t *b)
{
    return (uint16_t)((uint16_t)b[0] | (uint16_t)((uint16_t)b[1] << 8U));
}

static int16_t le_s16(const uint8_t *b)
{
    return (int16_t)le_u16(b);
}

void bmp280_parse_calib(const uint8_t raw[BMP280_CALIB_LEN], bmp280_calib_t *out)
{
    if ((raw == NULL) || (out == NULL)) {
        return;
    }
    out->dig_t1 = le_u16(&raw[0]);
    out->dig_t2 = le_s16(&raw[2]);
    out->dig_t3 = le_s16(&raw[4]);
    out->dig_p1 = le_u16(&raw[6]);
    out->dig_p2 = le_s16(&raw[8]);
    out->dig_p3 = le_s16(&raw[10]);
    out->dig_p4 = le_s16(&raw[12]);
    out->dig_p5 = le_s16(&raw[14]);
    out->dig_p6 = le_s16(&raw[16]);
    out->dig_p7 = le_s16(&raw[18]);
    out->dig_p8 = le_s16(&raw[20]);
    out->dig_p9 = le_s16(&raw[22]);
}

/* Datasheet section 3.11.3 / 8.2. Bosch's reference code left-shifts signed
 * values, which is undefined behaviour in C when the value is negative; those
 * shifts are written as multiplications by powers of two instead (same result,
 * no UB). Right shifts of negative values are implementation-defined; GCC and
 * Clang (our only compilers) make them arithmetic, matching the reference. */
#define POW2(n) ((int64_t)1 << (n))
int32_t bmp280_compensate_temp(const bmp280_calib_t *c, int32_t adc_t, int32_t *t_fine)
{
    int32_t var1;
    int32_t var2;
    int32_t tf;

    if (c == NULL) {
        return 0;
    }
    var1 = (((adc_t >> 3) - ((int32_t)c->dig_t1 << 1)) * (int32_t)c->dig_t2) >> 11;
    var2 = (((((adc_t >> 4) - (int32_t)c->dig_t1) *
              ((adc_t >> 4) - (int32_t)c->dig_t1)) >> 12) *
            (int32_t)c->dig_t3) >> 14;
    tf = var1 + var2;
    if (t_fine != NULL) {
        *t_fine = tf;
    }
    return ((tf * 5) + 128) >> 8;
}

uint32_t bmp280_compensate_press_q24_8(const bmp280_calib_t *c, int32_t adc_p, int32_t t_fine)
{
    int64_t var1;
    int64_t var2;
    int64_t p;

    if (c == NULL) {
        return 0U;
    }
    var1 = (int64_t)t_fine - 128000;
    var2 = var1 * var1 * (int64_t)c->dig_p6;
    var2 = var2 + ((var1 * (int64_t)c->dig_p5) * POW2(17));
    var2 = var2 + ((int64_t)c->dig_p4 * POW2(35));
    var1 = ((var1 * var1 * (int64_t)c->dig_p3) >> 8) + ((var1 * (int64_t)c->dig_p2) * POW2(12));
    var1 = ((POW2(47) + var1) * (int64_t)c->dig_p1) >> 33;
    if (var1 == 0) {
        return 0U; /* avoid division by zero */
    }
    p = 1048576 - (int64_t)adc_p;
    p = (((p * POW2(31)) - var2) * 3125) / var1;
    var1 = ((int64_t)c->dig_p9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)c->dig_p8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)c->dig_p7 * POW2(4));
    return (uint32_t)p;
}

static i2c_status_t write_reg(const bmp280_t *dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2];

    buf[0] = reg;
    buf[1] = val;
    return dev->bus->write(dev->bus->ctx, dev->addr, buf, sizeof(buf));
}

i2c_status_t bmp280_init(bmp280_t *dev, const i2c_bus_t *bus, uint8_t addr,
                         const bmp280_cfg_t *cfg)
{
    i2c_status_t st;
    uint8_t id = 0U;
    uint8_t calib[BMP280_CALIB_LEN];

    if ((dev == NULL) || (bus == NULL) || (cfg == NULL) || (bus->write == NULL) ||
        (bus->read_regs == NULL)) {
        return I2C_ERR_PARAM;
    }
    dev->bus = bus;
    dev->addr = addr;
    dev->cfg = *cfg;
    dev->ready = false;

    st = bus->read_regs(bus->ctx, addr, BMP280_REG_CHIP_ID, &id, 1U);
    if (st != I2C_OK) {
        return st;
    }
    if (id != BMP280_CHIP_ID_VAL) {
        return I2C_ERR_BUS;
    }
    st = write_reg(dev, BMP280_REG_RESET, BMP280_RESET_WORD);
    if (st != I2C_OK) {
        return st;
    }
    if (bus->delay_ms != NULL) {
        bus->delay_ms(bus->ctx, RESET_DELAY_MS);
    }
    st = bus->read_regs(bus->ctx, addr, BMP280_REG_CALIB00, calib, sizeof(calib));
    if (st != I2C_OK) {
        return st;
    }
    bmp280_parse_calib(calib, &dev->calib);
    if ((dev->calib.dig_t1 == 0U) || (dev->calib.dig_p1 == 0U)) {
        return I2C_ERR_BUS; /* blank NVM: not a real/working device */
    }
    /* config must be written while in sleep mode, i.e. before ctrl_meas. */
    st = write_reg(dev, BMP280_REG_CONFIG, cfg->config);
    if (st == I2C_OK) {
        st = write_reg(dev, BMP280_REG_CTRL_MEAS, cfg->ctrl_meas);
    }
    dev->ready = (st == I2C_OK);
    return st;
}

i2c_status_t bmp280_read(bmp280_t *dev, bmp280_meas_t *out)
{
    uint8_t b[6];
    i2c_status_t st;
    int32_t adc_p;
    int32_t adc_t;
    int32_t t_fine = 0;
    uint32_t p_q8;

    if ((dev == NULL) || (out == NULL) || (dev->bus == NULL)) {
        return I2C_ERR_PARAM;
    }
    st = dev->bus->read_regs(dev->bus->ctx, dev->addr, BMP280_REG_PRESS_MSB, b, sizeof(b));
    if (st != I2C_OK) {
        return st;
    }
    adc_p = (int32_t)(((uint32_t)b[0] << 12U) | ((uint32_t)b[1] << 4U) | ((uint32_t)b[2] >> 4U));
    adc_t = (int32_t)(((uint32_t)b[3] << 12U) | ((uint32_t)b[4] << 4U) | ((uint32_t)b[5] >> 4U));
    if ((adc_p == ADC_SKIPPED) || (adc_t == ADC_SKIPPED)) {
        return I2C_ERR_BUS; /* measurement not yet available / skipped */
    }
    out->temp_cdeg = bmp280_compensate_temp(&dev->calib, adc_t, &t_fine);
    p_q8 = bmp280_compensate_press_q24_8(&dev->calib, adc_p, t_fine);
    if (p_q8 == 0U) {
        return I2C_ERR_BUS;
    }
    out->pressure_pa = (p_q8 + 128U) / 256U;
    return I2C_OK;
}
