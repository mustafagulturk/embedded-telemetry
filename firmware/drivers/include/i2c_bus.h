/**
 * @file i2c_bus.h
 * @brief Hardware-independent I2C master interface used by the sensor drivers.
 *
 * The STM32 BSP and the host-side test fakes both implement this, so driver
 * logic (register sequences, scaling, compensation) is tested on the PC.
 */
#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    I2C_OK = 0,
    I2C_ERR_NACK,      /* address or data not acknowledged (sensor missing) */
    I2C_ERR_TIMEOUT,   /* bus stuck / flag never set */
    I2C_ERR_BUS,       /* arbitration lost, bus error */
    I2C_ERR_PARAM
} i2c_status_t;

typedef struct {
    /** Write @p len bytes starting with the register address in data[0]. */
    i2c_status_t (*write)(void *ctx, uint8_t addr7, const uint8_t *data, size_t len);
    /** Write register address @p reg, repeated start, read @p len bytes. */
    i2c_status_t (*read_regs)(void *ctx, uint8_t addr7, uint8_t reg, uint8_t *data,
                              size_t len);
    /** Blocking delay used during sensor reset sequences. */
    void (*delay_ms)(void *ctx, uint32_t ms);
    void *ctx;
} i2c_bus_t;

#endif /* I2C_BUS_H */
