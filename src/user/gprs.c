
#include <string.h>
#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "debug.h"
#include "atcmd.h"
#include "gprs.h"


extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart2_tx;
extern QueueHandle_t HQueue_DebugTx;			//串口调试发送队列



static bool Gprs_ModuleInit(void);



void Gprs_Task(void * p_arg)
{

	vTaskDelay(1000);
	Gprs_ModuleInit();
	//printf("test1\r\n");
	//UINT8 mychar = 'a';

	while(1)
	{

		//printf("test\r\n");
		//test = uxQueueMessagesWaiting(HQueue_GprsRx);
		printf("num:\r\n");
		

		//xQueueSend(HQueue_DebugTx, &mychar, 0);

		vTaskDelay(1000);
		
	}

}


static bool Gprs_ModuleInit(void)
{
	UINT8 atStr[32];

	memcpy(atStr, AT_TEST, sizeof(AT_TEST));
	HAL_UART_Transmit_DMA(&huart1, atStr, sizeof(AT_TEST));

	return true;

}








