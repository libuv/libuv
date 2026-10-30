#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0 // Must be 0 for POSIX port
#define configUSE_TICKLESS_IDLE                 0
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#endif
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                ((unsigned short)128)
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

#if defined(__arm__)
/* MPS2 AN385 (Cortex-M3), see mps2/. */
#define configCPU_CLOCK_HZ                      ((unsigned long)25000000)
#define configPRIO_BITS                         3
#define configKERNEL_INTERRUPT_PRIORITY         255
/* Must not set bits below the implemented priority bits, so not 191. */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (5 << (8 - configPRIO_BITS))

#ifndef __ASSEMBLER__
void vAssertCalled(const char *file, int line);
#endif
#define configASSERT(x)                                                       \
    do { if (!(x)) vAssertCalled(__FILE__, __LINE__); } while (0)
#else
#define configCPU_CLOCK_HZ                      ((unsigned long)1000000)

/* Essential definitions */
#define configKERNEL_INTERRUPT_PRIORITY         255
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    191
#endif

/* For libuv */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#ifndef configSUPPORT_STATIC_ALLOCATION
#define configSUPPORT_STATIC_ALLOCATION         0
#endif

#define configTOTAL_HEAP_SIZE                   (2<<20)

#define configUSE_COUNTING_SEMAPHORES		    1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_MUTEXES                       1
#define configUSE_TIMERS                        1

#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES-1)
#define configTIMER_QUEUE_LENGTH                1
#define configTIMER_TASK_STACK_DEPTH            (2*configMINIMAL_STACK_SIZE)

#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetSchedulerState          1

#ifndef configNUM_THREAD_LOCAL_STORAGE_POINTERS
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 1
#endif

/* Required by the Cortex-M0 port. */
#define configENABLE_MPU                        0

#endif
