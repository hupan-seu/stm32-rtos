
#include <string.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "debug.h"



extern UART_HandleTypeDef huart2;				//串口2
extern DMA_HandleTypeDef hdma_usart2_tx;		//串口dma
extern QueueHandle_t HQueue_DebugTx;			//串口发送队列


/*bool Debug_AddString(const UINT8 *pString, UINT8 len)
{
	UINT8 i;
	UINT8 ret;
	
	for(i=0; i<len; i++)
	{
		ret = xQueueSend(HQueue_DebugTx, &pString[i], 0);
		if(ret != pdPASS)
		{
			return false;
		}
	}

	return true;
}*/

int fputc(int ch, FILE *f)
{
	UINT8 mychar = (UINT8)ch;

	xQueueSend(HQueue_DebugTx, &mychar, 0);

	return ch;
}


void Debug_Task(void *pvPara)
{
	UINT8 txData[64];
	UINT8 txLen;
    UINT8 i;
	
	while(1)
	{
		vTaskDelay(10);

		txLen = uxQueueMessagesWaiting(HQueue_DebugTx);
		if(txLen <= 0){
			continue;
		}

		if(txLen > 64){
			txLen = 64;
		}

		for(i=0; i<txLen; i++){
			xQueueReceive(HQueue_DebugTx, &txData[i], 0);
		}

		HAL_UART_Transmit_DMA(&huart2, txData, txLen);

		while(HAL_DMA_GetState(&hdma_usart2_tx) == HAL_DMA_STATE_BUSY){
			vTaskDelay(2);
		}
		
		memset(txData, 0, txLen);
	}

	//
}



