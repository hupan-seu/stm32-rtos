
#include "stm32f1xx_hal.h"
#include "debug.h"
#include "gprs.h"
#include "start.h"


extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef hdma_usart2_tx;



TaskHandle_t HTask_Start;				//开始任务
TaskHandle_t HTask_Led;					//led闪烁任务
TaskHandle_t HTask_Debug;				//调试任务
TaskHandle_t HTask_Gprs;				//通讯模块数据上传任务


QueueHandle_t HQueue_DebugTx;			//串口调试发送队列
QueueHandle_t HQueue_GprsRx;			//串口调试发送队列





void Start_Task(void * pvParameters)
{
	//进入临界区
	taskENTER_CRITICAL();					

	//创建led任务
	xTaskCreate((TaskFunction_t		)Led_Task,
				(const char *		)"led_task",
				(uint16_t			)STK_SIZE_LED,
				(void *				)NULL,
				(UBaseType_t		)PRIO_TASK_LED,
				(TaskHandle_t		)&HTask_Led
	);

	//创建调试任务，负责打印log
	xTaskCreate((TaskFunction_t		)Debug_Task,
				(const char *		)"debug_task",
				(uint16_t			)STK_SIZE_DEBUG,
				(void *				)NULL,
				(UBaseType_t		)PRIO_TASK_DEBUG,
				(TaskHandle_t		)&HTask_Debug
	);

	//创建通讯模块任务
	xTaskCreate((TaskFunction_t		)Gprs_Task,
				(const char *		)"gprs_task",
				(uint16_t			)STK_SIZE_GPRS,
				(void *				)NULL,
				(UBaseType_t		)PRIO_TASK_GPRS,
				(TaskHandle_t		)&HTask_Gprs
	);

	//创建串口2发送队列
	HQueue_DebugTx = xQueueCreate(128, 1);

	//创建串口1接收队列 
	HQueue_GprsRx = xQueueCreate(256, 1);

	//退出临界区
	taskEXIT_CRITICAL();

	//删除开始任务
	vTaskDelete(HTask_Start);
}




//led任务
void Led_Task(void * p_arg)
{

	while(1)
	{
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
		vTaskDelay(1000);
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
		vTaskDelay(1000);

		//printf("test\r\n");
	}

}








