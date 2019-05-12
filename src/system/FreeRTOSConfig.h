/*
    FreeRTOS V9.0.0 - Copyright (C) 2016 Real Time Engineers Ltd.
*/

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H


#include <stdint.h>
#include "main.h" 

extern uint32_t SystemCoreClock;

//config 开头的配置
#define configUSE_PREEMPTION                     1
#define configSUPPORT_STATIC_ALLOCATION          0		// 创建内核对象时，需要用户指定内存
#define configSUPPORT_DYNAMIC_ALLOCATION         1		// 创建内核对象时，自动分配内存，先按这个配置恒为1分析
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCPU_CLOCK_HZ                       ( SystemCoreClock )
#define configTICK_RATE_HZ                       ((TickType_t)1000)
#define configMAX_PRIORITIES                     ( 7 )
#define configMINIMAL_STACK_SIZE                 ((uint16_t)128)
#define configTOTAL_HEAP_SIZE                    ((size_t)10 *1024)
#define configMAX_TASK_NAME_LEN                  ( 16 )	// 创建任务时，任务名称最长长度
#define configUSE_16_BIT_TICKS                   0
#define configUSE_MUTEXES                        1		// 使用互斥信号量
#define configQUEUE_REGISTRY_SIZE                8
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1

/* Co-routine definitions. */
#define configUSE_CO_ROUTINES                    0
#define configMAX_CO_ROUTINE_PRIORITIES          ( 2 )

// 设置为1包含相应的api功能
#define INCLUDE_vTaskPrioritySet            1
#define INCLUDE_uxTaskPriorityGet           1
#define INCLUDE_vTaskDelete                 1		// 先按1分析
#define INCLUDE_vTaskCleanUpResources       0
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_vTaskDelayUntil             0
#define INCLUDE_vTaskDelay                  1
#define INCLUDE_xTaskGetSchedulerState      1

/* Cortex-M 中断向量表配置 */
#ifdef __NVIC_PRIO_BITS
	#define configPRIO_BITS         __NVIC_PRIO_BITS
#else
	#define configPRIO_BITS         4
#endif

/* 最低的可用中断优先级 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY   15

/* 最高 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

/* 内核中断优先级 */
#define configKERNEL_INTERRUPT_PRIORITY 		( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
/* 不能为0！*/
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 	( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* assert() */
#define configASSERT( x ) if ((x) == 0) {taskDISABLE_INTERRUPTS(); for( ;; );} 

/* 中断处理函数映射 */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler


#endif /* FREERTOS_CONFIG_H */

