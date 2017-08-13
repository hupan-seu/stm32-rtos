
#define _ALGORITHM_C_


#include <string.h>

#include "algorithm.h"



UINT8 Alg_GetSum(const UINT8 *dataBuf, UINT16 dataLen)
{
    UINT16 i;
    UINT8 sum = 0;

	if(dataLen == 0)
	{
		return 0;
	}

	for(i=0; i<dataLen; i++)
	{
	  sum += dataBuf[i];
	}

	return sum;
}



void Alg_Char2Hex(const UINT8 *srcData, UINT8 *disData, UINT16 srcLen)
{
	UINT16 i,j;
	UINT8 low,high;

	i = 0;
	j = 0;
	while(i < srcLen)
	{
		if((srcData[i] >= 'A')&&(srcData[i] <= 'F'))
		{
			low = srcData[i] - 'A' + 10; 
		}
		else if((srcData[i] >= 'a')&&(srcData[i] <= 'f'))
		{
			low = srcData[i] - 'a' + 10; 
		}
		else if((srcData[i] >= '0')&&(srcData[i] <= '9'))
		{
			low = srcData[i] - '0'; 
		}
		else
		{
			low = 0;
		}

		i++;
		if((srcData[i] >= 'A')&&(srcData[i] <= 'F'))
		{
			high = srcData[i] - 'A' + 10; 
		}
		else if((srcData[i] >= 'a')&&(srcData[i] <= 'f'))
		{
			high = srcData[i] - 'a' + 10; 
		}
		else if((srcData[i] >= '0')&&(srcData[i] <= '9'))
		{
			high = srcData[i] - '0'; 
		}
		else
		{
			high = 0;
		}

		disData[j] = low | (high<<4);
		i++;
		j++;
	}
	
	return;
}



int Alg_StrLookUp(const UINT8 *src, int maxSize, const UINT8 *demo, int len)
{
	int index,pos;

	if(maxSize > 8192)
	{
		return -1;
	}
	if(len > maxSize)
	{
		return -1;
	}

	pos = -1;
	index = 0;
	while((index + len) <= maxSize)
	{
		if(strncmp((void *)&src[index], (void *)demo, len)!= 0)
		{
			index++;
		}
		else
		{
			pos = index;
			break;
		}
	}
		
	return pos;
}


UINT8 Alg_Num2String(UINT16 myNum, UINT8 *myStr)
{
	UINT16 data_temp;
	UINT8 data_buf[5];
	UINT8 i;
	UINT8 data_pos;
	UINT16 pos_count;
	UINT8 ret_val;

	if(myNum == 0)
	{
		myStr[0] = '0';
		return 1;
	}

	data_temp = myNum;
	pos_count = 10000;
	for(i=0; i<5; i++)							//将整数的各位拆开分别存储
	{
		data_buf[i] = data_temp/pos_count;
		data_temp = data_temp%pos_count;
		pos_count = pos_count/10;
	}
	
	data_pos = 0;								//找到第一个不为0的数
	while(data_pos < 5)
	{
		if(data_buf[data_pos] != 0)
		{
			break;
		}
		data_pos++;
	}
	
	ret_val = 0;										//转换
	for(i=data_pos; i<5; i++)
	{
		myStr[ret_val] = data_buf[i] + '0';
		ret_val++;
	}

	return ret_val;	
}






