/*
 * FreeRTOS-Kernel V11.1.0 configuration for the STM32F411RE (Cortex-M4F, 100 MHz).
 * See https://www.freertos.org/a00110.html for every option.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifndef __ASSEMBLER__
#include <stdint.h>
extern uint32_t SystemCoreClock;
void vAssertCalled(const char *file, int line);
#endif

/* ---- scheduler ------------------------------------------------------------ */
#define configCPU_CLOCK_HZ                        (SystemCoreClock)
#define configTICK_RATE_HZ                        1000U
#define configUSE_PREEMPTION                      1
#define configUSE_TIME_SLICING                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   1
#define configUSE_TICKLESS_IDLE                   0
#define configMAX_PRIORITIES                      7
#define configMINIMAL_STACK_SIZE                  128U      /* words */
#define configMAX_TASK_NAME_LEN                   12
#define configTICK_TYPE_WIDTH_IN_BITS             TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                   1
#define configSTACK_DEPTH_TYPE                    uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE          size_t
#define configUSE_NEWLIB_REENTRANT                0
/* 1: the lwIP 2.2.0 FreeRTOS port (contrib sys_arch.c) still uses portTICK_RATE_MS */
#define configENABLE_BACKWARD_COMPATIBILITY       1

/* ---- features --------------------------------------------------------------- */
#define configUSE_TASK_NOTIFICATIONS              1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES     1
#define configUSE_MUTEXES                         1
#define configUSE_RECURSIVE_MUTEXES               1
#define configUSE_COUNTING_SEMAPHORES             1
#define configUSE_QUEUE_SETS                      0
#define configQUEUE_REGISTRY_SIZE                 8
#define configUSE_CO_ROUTINES                     0
#define configUSE_TRACE_FACILITY                  1
#define configUSE_STATS_FORMATTING_FUNCTIONS      0
#define configGENERATE_RUN_TIME_STATS             0

/* ---- memory: tasks and queues of our own are static; heap_4 serves the
 *      kernel objects that lwIP creates at run time (see docs/memory-budget.md) */
#define configSUPPORT_STATIC_ALLOCATION           1
#define configSUPPORT_DYNAMIC_ALLOCATION          1
#define configKERNEL_PROVIDED_STATIC_MEMORY       1         /* idle + timer task memory */
#define configTOTAL_HEAP_SIZE                     ((size_t)(16U * 1024U))
#define configAPPLICATION_ALLOCATED_HEAP          0

/* ---- hooks -------------------------------------------------------------------- */
#define configUSE_IDLE_HOOK                       0
#define configUSE_TICK_HOOK                       0
#define configCHECK_FOR_STACK_OVERFLOW            2
#define configUSE_MALLOC_FAILED_HOOK              1
#define configRECORD_STACK_HIGH_ADDRESS           1

/* ---- software timers ---------------------------------------------------------- */
#define configUSE_TIMERS                          1
#define configTIMER_TASK_PRIORITY                 (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                  10
#define configTIMER_TASK_STACK_DEPTH              256U

/* ---- interrupt priorities (STM32F4: 4 priority bits, 0 = highest) ------------- */
#define configPRIO_BITS                           4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY   15
/* ISRs that call FreeRTOS "FromISR" APIs must have priority 5..15 (numerically >= 5). */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY           (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY      (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ---- assert -------------------------------------------------------------------- */
#define configASSERT(x)  do { if ((x) == 0) { vAssertCalled(__FILE__, __LINE__); } } while (0)

/* ---- optional API ------------------------------------------------------------- */
#define INCLUDE_vTaskPrioritySet                  1
#define INCLUDE_uxTaskPriorityGet                 1
#define INCLUDE_vTaskDelete                       1
#define INCLUDE_vTaskSuspend                      1
#define INCLUDE_xTaskDelayUntil                   1
#define INCLUDE_vTaskDelay                        1
#define INCLUDE_xTaskGetSchedulerState            1
#define INCLUDE_xTaskGetCurrentTaskHandle         1
#define INCLUDE_uxTaskGetStackHighWaterMark       1
#define INCLUDE_xTaskGetIdleTaskHandle            0
#define INCLUDE_eTaskGetState                     1
#define INCLUDE_xTimerPendFunctionCall            1

/* ---- map the port's handlers onto the CMSIS vector names ---------------------- */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
