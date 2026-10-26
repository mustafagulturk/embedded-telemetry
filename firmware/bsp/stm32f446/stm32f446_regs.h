/**
 * @file stm32f446_regs.h
 * @brief Minimal STM32F446 register definitions (RM0390) for the peripherals we use.
 *
 * Written by hand instead of pulling in the vendor CMSIS/HAL packages: the
 * firmware touches only a handful of registers and this keeps every hardware
 * access visible and reviewable.
 */
#ifndef STM32F446_REGS_H
#define STM32F446_REGS_H

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(addr))

/* ---- RCC ---- */
#define RCC_BASE        0x40023800UL
#define RCC_AHB1ENR     REG32(RCC_BASE + 0x30UL)
#define RCC_APB1ENR     REG32(RCC_BASE + 0x40UL)
#define RCC_APB1RSTR    REG32(RCC_BASE + 0x20UL)
#define RCC_CSR         REG32(RCC_BASE + 0x74UL)

#define RCC_AHB1ENR_GPIOAEN (1UL << 0)
#define RCC_AHB1ENR_GPIOBEN (1UL << 1)
#define RCC_APB1ENR_TIM2EN  (1UL << 0)
#define RCC_APB1ENR_USART2EN (1UL << 17)
#define RCC_APB1ENR_I2C1EN  (1UL << 21)
#define RCC_APB1RSTR_I2C1RST (1UL << 21)

#define RCC_CSR_RMVF     (1UL << 24)
#define RCC_CSR_PORRSTF  (1UL << 27)
#define RCC_CSR_SFTRSTF  (1UL << 28)
#define RCC_CSR_IWDGRSTF (1UL << 29)
#define RCC_CSR_WWDGRSTF (1UL << 30)

/* ---- GPIO ---- */
typedef struct {
    volatile uint32_t MODER;
    volatile uint32_t OTYPER;
    volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t LCKR;
    volatile uint32_t AFR[2];
} gpio_regs_t;

#define GPIOA ((gpio_regs_t *)0x40020000UL)
#define GPIOB ((gpio_regs_t *)0x40020400UL)

/* ---- I2C (v1, F4 family) ---- */
typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t OAR1;
    volatile uint32_t OAR2;
    volatile uint32_t DR;
    volatile uint32_t SR1;
    volatile uint32_t SR2;
    volatile uint32_t CCR;
    volatile uint32_t TRISE;
    volatile uint32_t FLTR;
} i2c_regs_t;

#define I2C1 ((i2c_regs_t *)0x40005400UL)

#define I2C_CR1_PE    (1UL << 0)
#define I2C_CR1_START (1UL << 8)
#define I2C_CR1_STOP  (1UL << 9)
#define I2C_CR1_ACK   (1UL << 10)
#define I2C_CR1_POS   (1UL << 11)
#define I2C_CR1_SWRST (1UL << 15)
#define I2C_SR1_SB    (1UL << 0)
#define I2C_SR1_ADDR  (1UL << 1)
#define I2C_SR1_BTF   (1UL << 2)
#define I2C_SR1_RXNE  (1UL << 6)
#define I2C_SR1_TXE   (1UL << 7)
#define I2C_SR1_BERR  (1UL << 8)
#define I2C_SR1_ARLO  (1UL << 9)
#define I2C_SR1_AF    (1UL << 10)
#define I2C_SR2_BUSY  (1UL << 1)

/* ---- USART ---- */
typedef struct {
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t BRR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t GTPR;
} usart_regs_t;

#define USART2 ((usart_regs_t *)0x40004400UL)

#define USART_SR_TC   (1UL << 6)
#define USART_SR_TXE  (1UL << 7)
#define USART_CR1_RE  (1UL << 2)
#define USART_CR1_TE  (1UL << 3)
#define USART_CR1_TCIE (1UL << 6)
#define USART_CR1_TXEIE (1UL << 7)
#define USART_CR1_UE  (1UL << 13)

/* ---- TIM2 (general purpose, 32-bit) ---- */
#define TIM2_BASE  0x40000000UL
#define TIM2_CR1   REG32(TIM2_BASE + 0x00UL)
#define TIM2_EGR   REG32(TIM2_BASE + 0x14UL)
#define TIM2_CNT   REG32(TIM2_BASE + 0x24UL)
#define TIM2_PSC   REG32(TIM2_BASE + 0x28UL)
#define TIM2_ARR   REG32(TIM2_BASE + 0x2CUL)
#define TIM_CR1_CEN (1UL << 0)
#define TIM_EGR_UG  (1UL << 0)

/* ---- IWDG ---- */
#define IWDG_BASE 0x40003000UL
#define IWDG_KR   REG32(IWDG_BASE + 0x00UL)
#define IWDG_PR   REG32(IWDG_BASE + 0x04UL)
#define IWDG_RLR  REG32(IWDG_BASE + 0x08UL)
#define IWDG_SR   REG32(IWDG_BASE + 0x0CUL)

/* ---- Cortex-M4 core ---- */
#define SCB_CPACR  REG32(0xE000ED88UL)
#define SCB_AIRCR  REG32(0xE000ED0CUL)
#define SCB_VTOR   REG32(0xE000ED08UL)
#define NVIC_ISER(n) REG32(0xE000E100UL + (4UL * (n)))
#define NVIC_IPR_BYTE(irq) (*(volatile uint8_t *)(0xE000E400UL + (irq)))

#define USART2_IRQn 38U

#endif /* STM32F446_REGS_H */
