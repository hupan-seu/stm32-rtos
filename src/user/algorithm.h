
#ifndef _ALGORITHM_H_
#define _ALGORITHM_H_

#include "types.h"




UINT8 Alg_GetSum(const UINT8 *dataBuf, UINT16 dataLen);
void Alg_Char2Hex(const UINT8 *srcData, UINT8 *disData, UINT16 srcLen);
int Alg_StrLookUp(const UINT8 *src, int maxSize, const UINT8 *demo, int len);








#endif

