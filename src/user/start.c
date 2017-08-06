

#include "stm32f1xx_hal.h"
#include "start.h"


extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef hdma_usart2_tx;



TaskHandle_t HTask_Start;				//开始任务
TaskHandle_t HTask_Led;					//led闪烁任务
TaskHandle_t HTask_Debug;				//调试任务

QueueHandle_t HQueue_DebugTx;			//串口调试发送队列


void start_task(void * pvParameters)
{
	//进入临界区
	taskENTER_CRITICAL();					

	//创建led任务
	xTaskCreate((TaskFunction_t		)led_task,
				(const char *		)"led_task",
				(uint16_t			)LED_STK_SIZE,
				(void *				)NULL,
				(UBaseType_t		)LED_TASK_PRIO,
				(TaskHandle_t		)&HTask_Led
	);

	//创建调试任务，负责打印log
	xTaskCreate((TaskFunction_t		)debug_task,
				(const char *		)"debug_task",
				(uint16_t			)DEBUG_STK_SIZE,
				(void *				)NULL,
				(UBaseType_t		)DEBUG_TASK_PRIO,
				(TaskHandle_t		)&HTask_Debug
	);

	//创建串口发送队列
	HQueue_DebugTx = xQueueCreate(128, 1);

	//退出临界区
	taskEXIT_CRITICAL();

	//删除开始任务
	vTaskDelete(HTask_Start);
}




//led任务
void led_task(void * p_arg)
{
	char ch = 'a';
	while(1)
	{
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
		vTaskDelay(1000);
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
		vTaskDelay(1000);

		xQueueSend(HQueue_DebugTx, &ch, 0);
	}

}



void debug_task(void *pvPara)
{
	uint8_t txData[128];
	uint8_t txLen;
    uint8_t state;
	

	while(1)
	{
		vTaskDelay(10);
		
		//prvLockQueue(HQueue_DebugTx);

		txLen = uxQueueMessagesWaiting(HQueue_DebugTx);
		if(txLen <= 0)
		{
			continue;
		}

		state = xQueueReceive(HQueue_DebugTx, txData, 0);
		if(state == pdFALSE){
			continue;
		}

		HAL_UART_Transmit_DMA(&huart2, txData, txLen);

		while(HAL_DMA_GetState(&hdma_usart2_tx) == HAL_DMA_STATE_BUSY)
		{
			vTaskDelay(2);
		}		
	}

	//
}



