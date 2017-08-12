
#include <string.h>
#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "algorithm.h"
#include "debug.h"
#include "atcmd.h"
#include "gprs.h"


extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart2_tx;
extern QueueHandle_t HQueue_DebugTx;			//串口调试发送队列



static bool Gprs_ModuleInit(void);
static void Gprs_Uart1Clear(void);
static bool Gprs_ModuleActPart(UINT8 *sendStr, UINT16 sendLen, UINT8 *reStr, UINT16 reLen, UINT16 waitTime);





UINT8 at_set_test[] = 	"AT\r\n";						//测试
UINT8 at_set_show[] = 	"ATE0\r\n";						//关闭回显
UINT8 at_set_baud[] = 	"AT+IPR=115200\r\n";			//设置串口波特率
UINT8 at_set_sim[] = 	"AT+CPIN?\r\n";					//读取sim卡状态
UINT8 at_set_dns[] = 	"AT+QIDNSIP=1\r\n";				//设置域名方式连接
UINT8 at_set_cg[] = 	"AT+CGATT=1\r\n";				//设置gprs附着
UINT8 at_set_apn[] = 	"AT+QIREGAPP=\"cmnet\"\r\n";	//设置接入点
UINT8 at_set_act[] = 	"AT+QIACT\r\n";					//激活移动场景


UINT8 at_gprs_sta[] = 	"AT+QISTATE\r\n";				//查询连接状态
UINT8 at_gprs_open[] = 	"AT+QIOPEN=\"TCP\",\"micro-code.com\",9090\r\n";
UINT8 at_gprs_send[] = 	"AT+QISEND=";					//发送数据
UINT8 at_gprs_close[] = "AT+QICLOSE\r\n";				//关闭连接


UINT8 at_re_ok[] = 		"OK";							//大部分命令的回复
UINT8 at_re_sim[] = 	"+CPIN: READY";					//sim卡状态
UINT8 at_re_con[] =		"CONNECT OK";					//连接成功
UINT8 at_re_data[] = 	">";							//输入要发送的数据
UINT8 at_re_send[] = 	"SEND OK";						//数据发送成功



Start_GprsRe_Struct Start_GprsRe;				//通讯模块接收缓存


void Gprs_Task(void * p_arg)
{

	vTaskDelay(12000);
	printf("gprs init\r\n");
	Gprs_ModuleInit();
	printf("init ok\r\n");

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
	bool reVal;

	//测试连接
	reVal = Gprs_ModuleActPart(at_set_test, sizeof(at_set_test)-1, at_re_ok, sizeof(at_re_ok)-1, 300);			
	if(!reVal)
	{
		return false;
	}
	
	//关闭回显
	reVal = Gprs_ModuleActPart(at_set_show, sizeof(at_set_show)-1, at_re_ok, sizeof(at_re_ok)-1, 300);
	if(!reVal)
	{	
		return false;
	}

	//设置串口波特率
	reVal = Gprs_ModuleActPart(at_set_baud, sizeof(at_set_baud)-1, at_re_ok, sizeof(at_re_ok)-1, 300);			
	if(!reVal)
	{
		return false;
	}

	//读取sim卡状态
	reVal = Gprs_ModuleActPart(at_set_sim, sizeof(at_set_sim)-1, at_re_sim, sizeof(at_re_sim)-1, 300);		
	if(!reVal)
	{
		return false;
	}
	
	//设置域名方式连接
	vTaskDelay(1000);
	reVal = Gprs_ModuleActPart(at_set_dns, sizeof(at_set_dns)-1, at_re_ok, sizeof(at_re_ok)-1, 300);			
	if(!reVal)
	{
		return false;
	}
	
	//设置gprs附着
	vTaskDelay(1000);
	reVal = Gprs_ModuleActPart(at_set_cg, sizeof(at_set_cg)-1, at_re_ok, sizeof(at_re_ok)-1, 8000);			
	if(!reVal)
	{
		return false;
	}
	
	//设置接入点
	vTaskDelay(2000);
	reVal = Gprs_ModuleActPart(at_set_apn, sizeof(at_set_apn)-1, at_re_ok, sizeof(at_re_ok)-1, 300);			
	if(!reVal)
	{
		return false;
	}
	
	//激活移动场景
	vTaskDelay(2000);
	reVal = Gprs_ModuleActPart(at_set_act, sizeof(at_set_act)-1, at_re_ok, sizeof(at_re_ok)-1, 18000);			
	if(!reVal)
	{
		return false;
	}
					
	printf("finish\r\n");
	return true;

}

static bool Gprs_ModuleActPart(UINT8 *sendStr, UINT16 sendLen, UINT8 *reStr, UINT16 reLen, UINT16 waitTime)
{
	int pos;
	UINT16 i,waitMax;

	Gprs_Uart1Clear();
	
	HAL_UART_Transmit_DMA(&huart1, sendStr, sendLen);

	waitMax = waitTime/10;
	i=0;
	while(i<waitMax)
	{
		vTaskDelay(10);
		i++;
		
		if(Start_GprsRe.len < 4)
		{
			continue;
		}
		pos = Alg_StrLookUp(Start_GprsRe.data, Start_GprsRe.len, reStr, reLen);
		if(pos >= 0)
		{
			return true;
		}
	}

	return false;
	/*for(i=0; i<waitMax; i++)
	{
		vTaskDelay(10);
		if(Start_GprsRe.len >= 4)
		{
			vTaskDelay(100);
			break;
		}
	}

	pos = Alg_StrLookUp(Start_GprsRe.data, Start_GprsRe.len, reStr, reLen);

	if(pos <= 0)
	{
		return false;
	}

	return true;*/
}


/*static void Gprs_WaitForRe(UINT16 waitTime)
{
	UINT16 i,max;

	max = waitTime/10;
	for(i=0; i<max; i++)
	{
		vTaskDelay(10);
		if(Start_GprsRe.len >= 4)
		{
			vTaskDelay(10);
			return;
		}
	}

	return;
}*/


static void Gprs_Uart1Clear(void)
{
	taskENTER_CRITICAL();			//进入临界区

	Start_GprsRe.len = 0;
	memset(Start_GprsRe.data, 0, START_GPRS_RE_MAX);

	taskEXIT_CRITICAL();			//退出临界区

	return;
}


void Gprs_Uart1RxDeal(uint8_t recData)
{
	//xQueueSendFromISR(HQueue_GprsRx, &recData, 0);
	Start_GprsRe.data[Start_GprsRe.len] = recData;
	Start_GprsRe.len++;

	return;
}






