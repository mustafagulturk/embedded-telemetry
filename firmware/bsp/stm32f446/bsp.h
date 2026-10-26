/**
 * @file bsp.h
 * @brief Board support for the Nucleo-F446RE (also the Renode platform).
 *
 * Pin map: I2C1 SCL=PB8 SDA=PB9 (Arduino D15/D14), USART2 TX=PA2 RX=PA3
 * (ST-LINK virtual COM port), LD2 = PA5, fail-safe output = PA6.
 */
#ifndef BSP_H
#define BSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "i2c_bus.h"
#include "telemetry_types.h"

void bsp_init(void);

/** Microseconds from TIM2 (wraps after ~71 min; use unsigned differences). */
uint32_t bsp_time_us(void);

/** Busy-wait delay; usable before the scheduler starts. */
void bsp_delay_us(uint32_t us);

void bsp_led_set(bool on);
void bsp_led_toggle(void);

/** Drive the fail-safe output (e.g. arm/deploy inhibit line). */
void bsp_failsafe_output(bool active);

/** Reset cause captured from RCC_CSR at boot (flags are cleared). */
reset_cause_t bsp_reset_cause(void);

/* ---- I2C1 master (polled, with timeouts and bus recovery) ---- */
const i2c_bus_t *bsp_i2c1_bus(void);
uint32_t bsp_i2c_error_count(void);

/* ---- USART2 interrupt-driven transmitter ---- */
typedef void (*bsp_uart_done_cb_t)(void);

void bsp_uart_set_tx_done_callback(bsp_uart_done_cb_t cb);
/** Start sending @p len bytes; @p buf must stay valid until the callback. */
bool bsp_uart_tx_start(const uint8_t *buf, size_t len);

/* ---- Independent watchdog ---- */
void bsp_iwdg_start(uint32_t timeout_ms);
void bsp_iwdg_refresh(void);

/** Unrecoverable error: fail-safe output on, interrupts off, wait for IWDG. */
void bsp_fatal(const char *file, int line);

#endif /* BSP_H */
