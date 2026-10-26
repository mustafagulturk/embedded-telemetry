#include "bsp.h"

#include "stm32f446_regs.h"

#define PCLK1_HZ   16000000UL
#define UART_BAUD  115200UL
#define LED_PIN    5U   /* PA5, LD2 */
#define SAFE_PIN   6U   /* PA6 */

static reset_cause_t s_reset_cause = RESET_CAUSE_OTHER;
static bool s_iwdg_running = false;

static void gpio_mode(gpio_regs_t *port, uint32_t pin, uint32_t mode)
{
    port->MODER = (port->MODER & ~(3UL << (pin * 2U))) | (mode << (pin * 2U));
}

static void gpio_af(gpio_regs_t *port, uint32_t pin, uint32_t af)
{
    uint32_t idx = pin / 8U;
    uint32_t shift = (pin % 8U) * 4U;

    port->AFR[idx] = (port->AFR[idx] & ~(0xFUL << shift)) | (af << shift);
    gpio_mode(port, pin, 2U);
}

static void capture_reset_cause(void)
{
    uint32_t csr = RCC_CSR;

    if ((csr & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) != 0U) {
        s_reset_cause = RESET_CAUSE_WATCHDOG;
    } else if ((csr & RCC_CSR_SFTRSTF) != 0U) {
        s_reset_cause = RESET_CAUSE_SOFTWARE;
    } else if ((csr & RCC_CSR_PORRSTF) != 0U) {
        s_reset_cause = RESET_CAUSE_POWER_ON;
    } else {
        s_reset_cause = RESET_CAUSE_OTHER;
    }
    RCC_CSR = csr | RCC_CSR_RMVF;
}

static void timer_init(void)
{
    TIM2_PSC = (PCLK1_HZ / 1000000UL) - 1UL;  /* 1 MHz */
    TIM2_ARR = 0xFFFFFFFFUL;
    TIM2_EGR = TIM_EGR_UG;                     /* load prescaler */
    TIM2_CNT = 0U;
    TIM2_CR1 = TIM_CR1_CEN;
}

static void uart_init(void)
{
    gpio_af(GPIOA, 2U, 7U);  /* USART2_TX */
    gpio_af(GPIOA, 3U, 7U);  /* USART2_RX */
    /* Oversampling by 16: BRR = fck / baud, rounded (= mantissa.fraction) */
    USART2->BRR = (PCLK1_HZ + (UART_BAUD / 2UL)) / UART_BAUD;
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    /* Priority 6: below configMAX_SYSCALL_INTERRUPT_PRIORITY (5) so the ISR
     * may use FromISR APIs. */
    NVIC_IPR_BYTE(USART2_IRQn) = (uint8_t)(6U << 4U);
    NVIC_ISER(USART2_IRQn / 32U) = 1UL << (USART2_IRQn % 32U);
}

void bsp_init(void)
{
    capture_reset_cause();

    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    RCC_APB1ENR |= RCC_APB1ENR_TIM2EN | RCC_APB1ENR_USART2EN | RCC_APB1ENR_I2C1EN;
    (void)RCC_APB1ENR; /* read back: clock enable takes effect before first access */

    gpio_mode(GPIOA, LED_PIN, 1U);
    gpio_mode(GPIOA, SAFE_PIN, 1U);
    bsp_failsafe_output(false);

    timer_init();
    uart_init();
}

uint32_t bsp_time_us(void)
{
    return TIM2_CNT;
}

void bsp_delay_us(uint32_t us)
{
    uint32_t start = bsp_time_us();

    while ((bsp_time_us() - start) < us) {
        /* busy wait */
    }
}

void bsp_led_set(bool on)
{
    GPIOA->BSRR = on ? (1UL << LED_PIN) : (1UL << (LED_PIN + 16U));
}

void bsp_led_toggle(void)
{
    bsp_led_set((GPIOA->ODR & (1UL << LED_PIN)) == 0U);
}

void bsp_failsafe_output(bool active)
{
    GPIOA->BSRR = active ? (1UL << SAFE_PIN) : (1UL << (SAFE_PIN + 16U));
}

reset_cause_t bsp_reset_cause(void)
{
    return s_reset_cause;
}

/* ---- USART2 TX ---- */

static const uint8_t *s_tx_buf;
static volatile size_t s_tx_len;
static volatile size_t s_tx_pos;
static bsp_uart_done_cb_t s_tx_done;

void bsp_uart_set_tx_done_callback(bsp_uart_done_cb_t cb)
{
    s_tx_done = cb;
}

bool bsp_uart_tx_start(const uint8_t *buf, size_t len)
{
    if ((buf == NULL) || (len == 0U) || ((USART2->CR1 & (USART_CR1_TXEIE | USART_CR1_TCIE)) != 0U)) {
        return false;
    }
    s_tx_buf = buf;
    s_tx_len = len;
    s_tx_pos = 0U;
    USART2->CR1 |= USART_CR1_TXEIE;
    return true;
}

void USART2_IRQHandler(void);
void USART2_IRQHandler(void)
{
    uint32_t sr = USART2->SR;
    uint32_t cr1 = USART2->CR1;

    if (((cr1 & USART_CR1_TXEIE) != 0U) && ((sr & USART_SR_TXE) != 0U)) {
        if (s_tx_pos < s_tx_len) {
            USART2->DR = s_tx_buf[s_tx_pos];
            s_tx_pos++;
        }
        if (s_tx_pos >= s_tx_len) {
            /* Last byte queued: wait for the shift register to drain. */
            USART2->CR1 = (cr1 & ~USART_CR1_TXEIE) | USART_CR1_TCIE;
        }
    } else if (((cr1 & USART_CR1_TCIE) != 0U) && ((sr & USART_SR_TC) != 0U)) {
        USART2->CR1 = cr1 & ~USART_CR1_TCIE;
        if (s_tx_done != NULL) {
            s_tx_done();
        }
    } else {
        /* spurious */
    }
}

/* ---- IWDG ---- */

void bsp_iwdg_start(uint32_t timeout_ms)
{
    /* LSI ~32 kHz, prescaler /32 -> 1 kHz -> 1 count per ms (max 4095 ms) */
    uint32_t reload = (timeout_ms > 4095U) ? 4095U : timeout_ms;

    IWDG_KR = 0x5555U;  /* unlock PR/RLR */
    IWDG_PR = 3U;       /* /32 */
    IWDG_RLR = reload;
    IWDG_KR = 0xAAAAU;
    IWDG_KR = 0xCCCCU;  /* start; cannot be stopped until reset */
    s_iwdg_running = true;
}

void bsp_iwdg_refresh(void)
{
    IWDG_KR = 0xAAAAU;
}

void bsp_fatal(const char *file, int line)
{
    (void)file;
    (void)line;
    __asm volatile ("cpsid i" ::: "memory");
    bsp_failsafe_output(true);
    bsp_led_set(true);
    if (!s_iwdg_running) {
        bsp_iwdg_start(100U);
    }
    for (;;) {
        /* no refresh: the IWDG resets the MCU */
    }
}
