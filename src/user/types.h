#ifndef __TYPES_H
#define __TYPES_H

#include "stm32f1xx_hal.h"


//
typedef uint8_t    UBYTE;
typedef  int8_t	   SBYTE;
typedef uint8_t    UINT8;
typedef  int8_t    SINT8;
typedef uint16_t   UINT16;
typedef uint16_t   UWORD;
typedef  int16_t   SINT16;
typedef uint32_t   UINT32;
typedef  int32_t   SINT32;




#ifndef __cplusplus
typedef enum {false = 0, true = !false} bool;
typedef bool BOOLEAN;
#endif


#endif

