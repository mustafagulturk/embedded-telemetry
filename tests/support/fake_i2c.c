#include "fake_i2c.h"

#include <string.h>

static fake_i2c_dev_t *find(fake_i2c_t *f, uint8_t addr)
{
    for (int i = 0; i < f->ndevs; i++) {
        if (f->devs[i].addr == addr) {
            return &f->devs[i];
        }
    }
    return NULL;
}

static i2c_status_t check(fake_i2c_dev_t *d)
{
    if (d == NULL || d->nack) {
        return I2C_ERR_NACK;
    }
    if (d->fail_after >= 0 && d->transfers >= d->fail_after) {
        return I2C_ERR_TIMEOUT;
    }
    d->transfers++;
    return I2C_OK;
}

static i2c_status_t fwrite_(void *ctx, uint8_t addr, const uint8_t *data, size_t len)
{
    fake_i2c_t *f = ctx;
    fake_i2c_dev_t *d = find(f, addr);
    i2c_status_t st = check(d);
    if (st != I2C_OK) {
        return st;
    }
    if (len == 0) {
        return I2C_ERR_PARAM;
    }
    uint8_t reg = data[0];
    for (size_t i = 1; i < len; i++) {
        d->regs[(uint8_t)(reg + i - 1)] = data[i];
        if (f->nlog < FAKE_I2C_LOG_LEN) {
            f->log[f->nlog++] = (fake_i2c_write_t){ addr, (uint8_t)(reg + i - 1), data[i] };
        }
    }
    return I2C_OK;
}

static i2c_status_t fread_(void *ctx, uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    fake_i2c_t *f = ctx;
    fake_i2c_dev_t *d = find(f, addr);
    i2c_status_t st = check(d);
    if (st != I2C_OK) {
        return st;
    }
    for (size_t i = 0; i < len; i++) {
        data[i] = d->regs[(uint8_t)(reg + i)];
    }
    return I2C_OK;
}

static void fdelay(void *ctx, uint32_t ms)
{
    ((fake_i2c_t *)ctx)->delay_total_ms += ms;
}

void fake_i2c_init(fake_i2c_t *f)
{
    memset(f, 0, sizeof(*f));
    f->bus.write = fwrite_;
    f->bus.read_regs = fread_;
    f->bus.delay_ms = fdelay;
    f->bus.ctx = f;
}

fake_i2c_dev_t *fake_i2c_add(fake_i2c_t *f, uint8_t addr)
{
    fake_i2c_dev_t *d = &f->devs[f->ndevs++];
    memset(d, 0, sizeof(*d));
    d->addr = addr;
    d->fail_after = -1;
    return d;
}

int fake_i2c_find_write(const fake_i2c_t *f, uint8_t addr, uint8_t reg)
{
    for (int i = 0; i < f->nlog; i++) {
        if (f->log[i].addr == addr && f->log[i].reg == reg) {
            return i;
        }
    }
    return -1;
}
