
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
static UINT8 Gprs_PackData(UINT8 *dataBuf);





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

UINT8 test_head[] = {0xFF,0xFF,0xFF};
UINT8 test_service[] = "20101";
UINT8 test_user[] = "18551607206";
UINT8 test_lng[13] = "E125394763";
UINT8 test_lat[13] = "N42499362";

Start_GprsRe_Struct Start_GprsRe;				//通讯模块接收缓存


void Gprs_Task(void * p_arg)
{
	bool reVal;
	UINT8 sendBuf[100] = {0};
	UINT8 packBuf[100] = {0};
	UINT8 sendNum[10] = {0};
	UINT8 packNum;
	UINT8 numLen;
	UINT8 len;
	
	printf("wait...\r\n");

	vTaskDelay(12000);

	printf("gprs init\r\n");

	reVal = Gprs_ModuleInit();
	if(!reVal)
	{
		printf("init fail stop\r\n");
		while(1);
	}

	printf("init ok\r\n");
	vTaskDelay(1000);
	
	while(1)
	{
		//建立连接
		reVal = Gprs_ModuleActPart(at_gprs_open, sizeof(at_gprs_open)-1, at_re_con, sizeof(at_re_con)-1, 18000);		
		if(!reVal)
		{
			printf("error0\r\n");
			break;
		}

		//发送数据
		packNum = Gprs_PackData(packBuf);
		numLen = Alg_Num2String(packNum, sendNum);

		len = 0;
		memcpy(&sendBuf[len], at_gprs_send, sizeof(at_gprs_send)-1);
		len += (sizeof(at_gprs_send)-1);
		memcpy(&sendBuf[len], sendNum, numLen);
		len += numLen;
		memcpy(&sendBuf[len], "\r\n", 2);
		len += 2;
		reVal = Gprs_ModuleActPart(sendBuf, len, at_re_data, sizeof(at_re_data)-1, 1000);		
		if(!reVal)
		{
			printf("error1\r\n");
			break;
		}

		reVal = Gprs_ModuleActPart(packBuf, packNum, at_re_send, sizeof(at_re_send)-1, 3000);	
		if(!reVal)
		{
			printf("error2\r\n");
			break;
		}

		reVal = Gprs_ModuleActPart(at_gprs_close, sizeof(at_gprs_close), at_re_ok, sizeof(at_re_ok)-1, 1000);	
		if(!reVal)
		{
			printf("error3\r\n");
			break;
		}
		
		printf("send ok\r\n");

		vTaskDelay(30000);
		
		

		//xQueueSend(HQueue_DebugTx, &mychar, 0);

		//vTaskDelay(1000);
		
	}
	
	printf("fail stop\r\n");
	while(1);
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
	reVal = Gprs_ModuleActPart(at_set_cg, sizeof(at_set_cg)-1, at_re_ok, sizeof(at_re_ok)-1, 12000);			
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
		
		if(Start_GprsRe.len < reLen)
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
}

static UINT8 Gprs_PackData(UINT8 *dataBuf)
{
	UINT8 pos = 0;
	UINT8 packLen = 0;

	//帧头
	memcpy(&dataBuf[pos], test_head, 3);
	pos += 3;

	//长度
	pos += 2;

	//服务号及版本
	memcpy(&dataBuf[pos], test_service, 5);
	pos += 5;

	//用户名
	memcpy(&dataBuf[pos], test_user, 11);
	pos += 11;

	//经纬度
	memcpy(&dataBuf[pos], test_lng, 13);
	pos += 13;
	memcpy(&dataBuf[pos], test_lat, 13);
	pos += 13;

	//校验和
	dataBuf[pos++] = Alg_GetSum(&dataBuf[5], pos-5);

	//
	packLen = pos - 5;
	dataBuf[3] = (packLen>>8)&0xFF;
	dataBuf[4] = packLen&0xFF;

	return pos;
}

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
	Start_GprsRe.data[Start_GprsRe.len] = recData;
	Start_GprsRe.len++;

	return;
}






