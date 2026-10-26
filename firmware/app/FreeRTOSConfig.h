/**
 * @file FreeRTOSConfig.h
 * @brief FreeRTOS kernel configuration: static allocation only, no heap.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

extern uint32_t bsp_time_us(void);
extern void bsp_fatal(const char *file, int line);

/* 16 MHz HSI, no PLL: identical on the Nucleo board and in Renode. */
#define configCPU_CLOCK_HZ                      16000000UL
#define configTICK_RATE_HZ                      1000U
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0
#define configMAX_PRIORITIES                    6
#define configMINIMAL_STACK_SIZE                128U
#define configMAX_TASK_NAME_LEN                 12
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_QUEUE_SETS                    0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configUSE_NEWLIB_REENTRANT              0

/* Memory: everything is statically allocated, there is no FreeRTOS heap. */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0
#define configKERNEL_PROVIDED_STATIC_MEMORY     0

/* Hooks */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            0

/* Run-time stats from TIM2 (1 MHz free-running) -> CPU load and per-task time. */
#define configGENERATE_RUN_TIME_STATS           1
#define configRUN_TIME_COUNTER_TYPE             uint32_t
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() do { } while (0) /* TIM2 started by bsp_init() */
#define portGET_RUN_TIME_COUNTER_VALUE()        bsp_time_us()
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configRECORD_STACK_HIGH_ADDRESS         1

#define configUSE_TIMERS                        0
#define configUSE_CO_ROUTINES                   0

/* Cortex-M4 interrupt priorities: STM32 implements 4 priority bits. */
#define configPRIO_BITS                              4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY      (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configASSERT(x) do { if ((x) == 0) { bsp_fatal(__FILE__, __LINE__); } } while (0)

/* API inclusion */
#define INCLUDE_vTaskDelay                 1
#define INCLUDE_xTaskDelayUntil            1
#define INCLUDE_vTaskSuspend               1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetIdleTaskHandle     1
#define INCLUDE_xTaskGetSchedulerState     1
#define INCLUDE_xTaskGetCurrentTaskHandle  1

/* Map the port's handlers onto the CMSIS vector names used by startup code. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
