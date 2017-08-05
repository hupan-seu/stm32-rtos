

#include "stm32f1xx_hal.h"
#include "start.h"


extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;



TaskHandle_t StartTask_Handler;				//开始任务
TaskHandle_t LEDTask_Handler;				//led闪烁任务
TaskHandle_t DebugTask_Handler;				//调试任务



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
				(TaskHandle_t		)&LEDTask_Handler
	);

	//创建任务
	xTaskCreate((TaskFunction_t		)debug_task,
				(const char *		)"debug_task",
				(uint16_t			)DEBUG_STK_SIZE,
				(void *				)NULL,
				(UBaseType_t		)DEBUG_TASK_PRIO,
				(TaskHandle_t		)&DebugTask_Handler
	);

	//退出临界区
	taskEXIT_CRITICAL();

	//删除开始任务
	vTaskDelete(StartTask_Handler);
}

void led_task(void * p_arg)
{
	while(1)
	{
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
		vTaskDelay(1000);
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
		vTaskDelay(1000);
	}

}



void debug_task(void *pvPara)
{
	uint8_t txData[] = {"HelloWorld\r\n"};
    
	vTaskDelay(500);

	while(1)
	{
		HAL_UART_Transmit_DMA(&huart1, txData, sizeof(txData));
		vTaskDelay(1000);
	}
}



