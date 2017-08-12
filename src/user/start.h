
#ifndef __START_H
#define __START_H

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"




//任务优先级
#define PRIO_TASK_START		1				
#define PRIO_TASK_LED		1
#define PRIO_TASK_DEBUG		1
#define PRIO_TASK_GPRS		1



//任务堆栈大小
#define STK_SIZE_START		128				
#define STK_SIZE_LED		32				
#define STK_SIZE_DEBUG		128
#define STK_SIZE_GPRS		512


//任务句柄
extern TaskHandle_t HTask_Start;	
extern TaskHandle_t HTask_Led;		
extern TaskHandle_t HTask_Debug;	
extern TaskHandle_t HTask_Gprs;





#ifdef __cplusplus
 extern "C" {
#endif 


//任务函数
void Start_Task(void *pvPara);				
void Led_Task(void * pvPara);			



#ifdef __cplusplus
}
#endif




#endif


