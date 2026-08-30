#ifndef OS_ERRMAN_H
#define OS_ERRMAN_H

#include "os_type.h"

#define RESET_REG_MAX        10
#define OS_MAX_DIAG          10

#define OS_ASSERT(EXPRESSION, retvalue)                       \
do {                                                          \
    if (!(EXPRESSION)) {                                      \
        OS_Reset(RESET_EVENT_ERR, __FILE__, __LINE__);        \
        return retvalue;                                      \
    }                                                         \
} while(0)

#define OS_RESET(EVENT) OS_Reset(EVENT, __FILE__, __LINE__)

/* 复位事件 */
typedef enum {
    RESET_EVENT_ERR,                   /* 程序异常复位 */
    RESET_EVENT_INITIATE,              /* 上电复位 */
    RESET_EVENT_DIRECT,                /* 直接复位，不回调复位通知函数 */
    RESET_EVENT_UPDATE,                /* 固件升级复位 */
    RESET_EVENT_MAX
} RESET_EVENT_E;

/* 复位回调优先级，0为最高优先级 */
typedef enum {
    RESET_PRI_0,
    RESET_PRI_1,
    RESET_PRI_MAX
} RESET_PRI_E;

/*******************************************************************
** 函数名	: OS_InitErrMan
** 函数描述	: 错误管理模块初始化。
** 参数		: 无
** 返回		: 无
********************************************************************/
void OS_InitErrMan(void);

/*******************************************************************
** 函数名	: OS_RegistResetInform
** 函数描述	: 注册复位前的通知回调。
** 参数		: [in] prior: 优先级，见RESET_PRI_E
** 参数		: [in] fp:    回调函数
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN OS_RegistResetInform(INT8U prior, void (*fp)(INT8U event, char *filename, INT32U line));

/*******************************************************************
** 函数名	: OS_Reset
** 函数描述	: 复位设备，复位前依次调用已注册的通知回调。
** 参数		: [in] event:    复位事件
** 参数		: [in] filename: 触发文件名
** 参数		: [in] line:     触发行号
** 返回		: 无
********************************************************************/
void OS_Reset(INT8U event, char *filename, INT32U line);

/*******************************************************************
** 函数名	: OS_ErrTskEntry
** 函数描述	: 错误管理调度入口，主循环中调用，轮询执行已注册的诊断函数。
** 参数		: 无
** 返回		: 无
********************************************************************/
void OS_ErrTskEntry(void);

/*******************************************************************
** 函数名	: OS_RegistDiagnoseProc
** 函数描述	: 注册一个诊断函数，由OS_ErrTskEntry轮询执行。
** 参数		: [in] diagproc: 诊断函数
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN OS_RegistDiagnoseProc(void (*diagproc)(void));

#endif /* OS_ERRMAN_H */
