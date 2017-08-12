
#ifndef __GPRS_H
#define __GPRS_H

/* Scheduler includes. */
#include "types.h"


//通讯模块串口接收缓存
#define START_GPRS_RE_MAX	256

typedef struct
{
	UINT8 data[START_GPRS_RE_MAX];
	UINT16 len;
}Start_GprsRe_Struct;


#ifdef __cplusplus
 extern "C" {
#endif 


//任务函数
void Gprs_Task(void *pvPara);						



#ifdef __cplusplus
}
#endif




#endif


