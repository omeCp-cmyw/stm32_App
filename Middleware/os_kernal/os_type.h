#ifndef OS_TYPE_H
#define OS_TYPE_H

typedef unsigned char       BOOLEAN;
typedef unsigned char       INT8U;
typedef signed   char       INT8S;
typedef unsigned short      INT16U;
typedef signed   short      INT16S;
typedef unsigned int        INT32U;
typedef signed   int        INT32S;
typedef unsigned long long  INT64U;
typedef float               FP32;
typedef double              FP64;

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

#endif /* OS_TYPE_H */
