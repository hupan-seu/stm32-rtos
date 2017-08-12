
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








