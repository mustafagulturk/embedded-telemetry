/* Host-side I2C bus fake: register-mapped devices with auto-increment,
 * write logging and fault injection. */
#ifndef FAKE_I2C_H
#define FAKE_I2C_H

#include <stdbool.h>
#include <stdint.h>

#include "i2c_bus.h"

#define FAKE_I2C_MAX_DEVS   2
#define FAKE_I2C_LOG_LEN    64

typedef struct {
    uint8_t addr;
    uint8_t regs[256];
    bool nack;                 /* device "disconnected" */
    int fail_after;            /* >=0: fail every transfer once this many succeeded */
    int transfers;
} fake_i2c_dev_t;

typedef struct {
    uint8_t addr;
    uint8_t reg;
    uint8_t val;
} fake_i2c_write_t;

typedef struct {
    fake_i2c_dev_t devs[FAKE_I2C_MAX_DEVS];
    int ndevs;
    fake_i2c_write_t log[FAKE_I2C_LOG_LEN];
    int nlog;
    uint32_t delay_total_ms;
    i2c_bus_t bus;
} fake_i2c_t;

void fake_i2c_init(fake_i2c_t *f);
fake_i2c_dev_t *fake_i2c_add(fake_i2c_t *f, uint8_t addr);
/* Index of the first logged write of (addr, reg), or -1. */
int fake_i2c_find_write(const fake_i2c_t *f, uint8_t addr, uint8_t reg);

#endif
