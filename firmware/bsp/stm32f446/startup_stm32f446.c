/**
 * @file startup_stm32f446.c
 * @brief Vector table and reset handler (C, no assembly).
 */
#include <stdint.h>

#include "stm32f446_regs.h"

extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;

int main(void);
void Reset_Handler(void);
void Default_Handler(void);

/* Weak aliases: any handler not provided by the application ends in Default_Handler. */
#define WEAK_ALIAS __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_ALIAS;
void HardFault_Handler(void) WEAK_ALIAS;
void MemManage_Handler(void) WEAK_ALIAS;
void BusFault_Handler(void) WEAK_ALIAS;
void UsageFault_Handler(void) WEAK_ALIAS;
void SVC_Handler(void) WEAK_ALIAS;
void DebugMon_Handler(void) WEAK_ALIAS;
void PendSV_Handler(void) WEAK_ALIAS;
void SysTick_Handler(void) WEAK_ALIAS;
void USART2_IRQHandler(void) WEAK_ALIAS;

typedef void (*vector_t)(void);

#define IRQ_COUNT 97U

/* The vector table is built by section placement; the cast from the stack
 * address to a function pointer is the standard Cortex-M idiom. */
__attribute__((section(".isr_vector"), used))
static const vector_t g_vectors[16U + IRQ_COUNT] = {
    [0] = (vector_t)(uintptr_t)&_estack,
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = HardFault_Handler,
    [4] = MemManage_Handler,
    [5] = BusFault_Handler,
    [6] = UsageFault_Handler,
    [11] = SVC_Handler,
    [12] = DebugMon_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [16U + USART2_IRQn] = USART2_IRQHandler,
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;

    while (dst < &_edata) {
        *dst = *src;
        dst++;
        src++;
    }
    for (dst = &_sbss; dst < &_ebss; dst++) {
        *dst = 0U;
    }

    /* Enable CP10/CP11 (FPU) full access before any float instruction. */
    SCB_CPACR |= (0xFUL << 20);
    __asm volatile ("dsb\n isb" ::: "memory");

    (void)main();
    for (;;) {
    }
}

void Default_Handler(void)
{
    /* Unexpected interrupt / fault: spin; the IWDG resets the MCU. */
    for (;;) {
    }
}
