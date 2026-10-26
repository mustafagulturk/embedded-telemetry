/**
 * @file bsp_i2c.c
 * @brief Polled I2C1 master for STM32F4 (I2C v1 peripheral), RM0390 sec. 24.3.
 *
 * Every wait is bounded by a timeout; on any error a STOP is issued and, if
 * the bus stays busy, the peripheral is software-reset and re-initialised.
 * Reads follow the reference manual's 1-byte / 2-byte / N-byte sequences
 * (ACK/POS/STOP timing), which the hardware requires for correct NACKing of
 * the last byte.
 */
#include "bsp.h"

#include "stm32f446_regs.h"

#define I2C_PCLK_MHZ     16U
#define I2C_CCR_FAST     14U    /* Fm, duty 2:1: 16 MHz / (3 * 14) = 381 kHz */
#define I2C_TRISE_FAST   6U     /* 300 ns * 16 MHz + 1 */
#define I2C_TIMEOUT_US   2000U

static uint32_t s_errors;

static void i2c_hw_init(void)
{
    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0U;
    I2C1->CR2 = I2C_PCLK_MHZ;
    I2C1->CCR = (1UL << 15) | I2C_CCR_FAST;  /* F/S = fast mode */
    I2C1->TRISE = I2C_TRISE_FAST;
    I2C1->CR1 = I2C_CR1_PE;
}

static void i2c_pins_init(void)
{
    /* PB8 SCL, PB9 SDA: AF4, open-drain, high speed, pull-up (board has none). */
    for (uint32_t pin = 8U; pin <= 9U; pin++) {
        GPIOB->OTYPER |= (1UL << pin);
        GPIOB->OSPEEDR |= (3UL << (pin * 2U));
        GPIOB->PUPDR = (GPIOB->PUPDR & ~(3UL << (pin * 2U))) | (1UL << (pin * 2U));
        GPIOB->AFR[1] = (GPIOB->AFR[1] & ~(0xFUL << ((pin - 8U) * 4U))) | (4UL << ((pin - 8U) * 4U));
        GPIOB->MODER = (GPIOB->MODER & ~(3UL << (pin * 2U))) | (2UL << (pin * 2U));
    }
}

/* Wait until (SR1 & mask) != 0. AF/ARLO/BERR abort the wait early. */
static i2c_status_t wait_sr1(uint32_t mask)
{
    uint32_t start = bsp_time_us();

    for (;;) {
        uint32_t sr1 = I2C1->SR1;

        if ((sr1 & I2C_SR1_AF) != 0U) {
            I2C1->SR1 = ~I2C_SR1_AF;  /* rc_w0 */
            return I2C_ERR_NACK;
        }
        if ((sr1 & (I2C_SR1_ARLO | I2C_SR1_BERR)) != 0U) {
            I2C1->SR1 = ~(I2C_SR1_ARLO | I2C_SR1_BERR);
            return I2C_ERR_BUS;
        }
        if ((sr1 & mask) != 0U) {
            return I2C_OK;
        }
        if ((bsp_time_us() - start) > I2C_TIMEOUT_US) {
            return I2C_ERR_TIMEOUT;
        }
    }
}

static void clear_addr(void)
{
    (void)I2C1->SR1;
    (void)I2C1->SR2;
}

static i2c_status_t fail(i2c_status_t st)
{
    uint32_t start = bsp_time_us();

    s_errors++;
    I2C1->CR1 = (I2C1->CR1 | I2C_CR1_STOP) & ~(I2C_CR1_ACK | I2C_CR1_POS);
    while ((I2C1->SR2 & I2C_SR2_BUSY) != 0U) {
        if ((bsp_time_us() - start) > I2C_TIMEOUT_US) {
            i2c_hw_init();  /* bus stuck: software reset of the peripheral */
            break;
        }
    }
    return st;
}

static i2c_status_t start_addr(uint8_t addr8)
{
    i2c_status_t st;

    I2C1->CR1 |= I2C_CR1_START;
    st = wait_sr1(I2C_SR1_SB);
    if (st == I2C_OK) {
        I2C1->DR = addr8;
        st = wait_sr1(I2C_SR1_ADDR);
    }
    return st;
}

static i2c_status_t i2c1_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
    i2c_status_t st;

    (void)ctx;
    if ((data == NULL) || (len == 0U)) {
        return I2C_ERR_PARAM;
    }
    st = start_addr((uint8_t)(addr7 << 1U));
    if (st != I2C_OK) {
        return fail(st);
    }
    clear_addr();
    for (size_t i = 0U; i < len; i++) {
        st = wait_sr1(I2C_SR1_TXE);
        if (st != I2C_OK) {
            return fail(st);
        }
        I2C1->DR = data[i];
    }
    st = wait_sr1(I2C_SR1_BTF);
    if (st != I2C_OK) {
        return fail(st);
    }
    I2C1->CR1 |= I2C_CR1_STOP;
    return I2C_OK;
}

static i2c_status_t read_bytes(uint8_t *data, size_t len)
{
    i2c_status_t st = I2C_OK;
    size_t i = 0U;

    if (len == 1U) {
        I2C1->CR1 &= ~I2C_CR1_ACK;
        clear_addr();
        I2C1->CR1 |= I2C_CR1_STOP;
        st = wait_sr1(I2C_SR1_RXNE);
        if (st == I2C_OK) {
            data[0] = (uint8_t)I2C1->DR;
        }
    } else if (len == 2U) {
        I2C1->CR1 = (I2C1->CR1 & ~I2C_CR1_ACK) | I2C_CR1_POS;
        clear_addr();
        st = wait_sr1(I2C_SR1_BTF);
        if (st == I2C_OK) {
            I2C1->CR1 |= I2C_CR1_STOP;
            data[0] = (uint8_t)I2C1->DR;
            data[1] = (uint8_t)I2C1->DR;
        }
        I2C1->CR1 &= ~I2C_CR1_POS;
    } else {
        I2C1->CR1 |= I2C_CR1_ACK;
        clear_addr();
        while ((st == I2C_OK) && ((len - i) > 3U)) {
            st = wait_sr1(I2C_SR1_RXNE);
            if (st == I2C_OK) {
                data[i] = (uint8_t)I2C1->DR;
                i++;
            }
        }
        if (st == I2C_OK) {
            /* Three bytes left: data N-2 in DR, N-1 in shift register. */
            st = wait_sr1(I2C_SR1_BTF);
        }
        if (st == I2C_OK) {
            I2C1->CR1 &= ~I2C_CR1_ACK;
            data[i] = (uint8_t)I2C1->DR;
            i++;
            st = wait_sr1(I2C_SR1_BTF);
        }
        if (st == I2C_OK) {
            I2C1->CR1 |= I2C_CR1_STOP;
            data[i] = (uint8_t)I2C1->DR;
            i++;
            st = wait_sr1(I2C_SR1_RXNE);
        }
        if (st == I2C_OK) {
            data[i] = (uint8_t)I2C1->DR;
        }
    }
    return st;
}

static i2c_status_t i2c1_read_regs(void *ctx, uint8_t addr7, uint8_t reg, uint8_t *data,
                                   size_t len)
{
    i2c_status_t st;

    (void)ctx;
    if ((data == NULL) || (len == 0U)) {
        return I2C_ERR_PARAM;
    }
    st = start_addr((uint8_t)(addr7 << 1U));
    if (st == I2C_OK) {
        clear_addr();
        st = wait_sr1(I2C_SR1_TXE);
    }
    if (st == I2C_OK) {
        I2C1->DR = reg;
        st = wait_sr1(I2C_SR1_BTF);
    }
    if (st == I2C_OK) {
        st = start_addr((uint8_t)((uint8_t)(addr7 << 1U) | 1U));  /* repeated start */
    }
    if (st == I2C_OK) {
        st = read_bytes(data, len);
    }
    if (st != I2C_OK) {
        return fail(st);
    }
    return I2C_OK;
}

static void i2c1_delay_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    bsp_delay_us(ms * 1000U);
}

static bool s_inited;
static const i2c_bus_t s_bus = {
    .write = i2c1_write,
    .read_regs = i2c1_read_regs,
    .delay_ms = i2c1_delay_ms,
    .ctx = NULL,
};

const i2c_bus_t *bsp_i2c1_bus(void)
{
    if (!s_inited) {
        i2c_pins_init();
        i2c_hw_init();
        s_inited = true;
    }
    return &s_bus;
}

uint32_t bsp_i2c_error_count(void)
{
    return s_errors;
}
