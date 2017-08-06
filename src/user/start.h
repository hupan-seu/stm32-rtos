
#ifndef __START_H
#define __START_H

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"


#ifdef __cplusplus
 extern "C" {
#endif 


//开始任务
#define START_TASK_PRIO		1				//优先级
#define START_STK_SIZE		128				//堆栈大小
void start_task(void *pvPara);				//任务函数
extern TaskHandle_t HTask_Start;			//句柄


//led任务
#define LED_TASK_PRIO		1				//优先级
#define LED_STK_SIZE		50				//堆栈大小
void led_task(void * pvPara);				//任务函数
extern TaskHandle_t HTask_Led;				//句柄


//debug任务
#define DEBUG_TASK_PRIO		3
#define DEBUG_STK_SIZE		128
void debug_task(void *pvPara);
extern TaskHandle_t HTask_Debug;			//句柄



#ifdef __cplusplus
}
#endif

#endif


