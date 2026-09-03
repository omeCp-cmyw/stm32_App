#ifndef OSAL_TYPES_H
#define OSAL_TYPES_H

#include <stdint.h>

/*
 * OSAL统一基础类型：
 * 新代码建议直接使用stdint.h类型（uint8_t等），
 * 以下旧类型别名保留兼容存量代码，逐步替换。
 */
typedef uint8_t  BOOLEAN;
typedef uint8_t  INT8U;
typedef int8_t   INT8S;
typedef uint16_t INT16U;
typedef int16_t  INT16S;
typedef uint32_t INT32U;
typedef int32_t  INT32S;
typedef uint64_t INT64U;
typedef float    FP32;
typedef double   FP64;

#ifndef false
#define false               0
#endif

#ifndef true
#define true                1
#endif

#ifndef TRUE
#define TRUE                1
#endif

#ifndef FALSE
#define FALSE               0
#endif

#ifndef NULL
#define NULL                ((void *)0)
#endif

#ifndef RETURN_VOID
#define RETURN_VOID
#endif

#ifndef RETURN_TRUE
#define RETURN_TRUE         TRUE
#endif

#ifndef RETURN_FALSE
#define RETURN_FALSE        FALSE
#endif

#ifndef _SUCCESS
#define _SUCCESS            0
#endif

#ifndef _FAILURE
#define _FAILURE            1
#endif

#ifndef _OVERTIME
#define _OVERTIME           2
#endif

#endif /* OSAL_TYPES_H */
