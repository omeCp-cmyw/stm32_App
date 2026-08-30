#ifndef OS_TIMER_H
#define OS_TIMER_H

#include "os_type.h"

/* 软件定时器数量上限 */
#define OS_MAX_TIMER                   80

/* 定时器控制块 */
typedef struct {
    INT8U   ready;                       /* 定时器是否到期 */
    void   *index;                       /* 回调函数的参数指针 */
    INT32U  totaltime;                   /* 定时总时长，单位tick */
    INT32U  lefttime;                    /* 当前剩余时间，单位tick */

    void  (*tmrproc)(void *index);       /* 到期回调函数指针 */
    void  (*bakproc)(void *index);       /* 回调备份，用于完整性校验 */
} OS_TMR_T;

/* 时基换算，本工程1个tick=10us（bsp_systick的g_systicks） */
#define PERTICK                        10                     /* 1 tick = 10us */
#define TICK_MILSECOND                 (1000L / PERTICK)
#define TICK_SECOND                    (1000L * 1000 / PERTICK)
#define TICK_MINUTE                    (60 * 1000L * 1000 / PERTICK)

#define _HIGHTICK                      1
#define _TICK                          TICK_MILSECOND
#define _MILTICK                       TICK_MILSECOND
#define _SECOND                        TICK_SECOND
#define _MINUTE                        TICK_MINUTE

/*******************************************************************
** 函数名	: OS_InitTimer
** 函数描述	: 定时器模块初始化，须在SysTick_Init之后调用。
** 参数		: 无
** 返回		: 无
********************************************************************/
void OS_InitTimer(void);

/*******************************************************************
** 函数名	: OS_CreateTmr
** 函数描述	: 创建一个软件定时器。
** 参数		: [in] index:   回调函数的参数指针
** 参数		: [in] tmrproc: 到期回调函数
** 返回		: 定时器ID，安装失败返回0xff
********************************************************************/
INT8U OS_CreateTmr(void *index, void (*tmrproc)(void *index));

/*******************************************************************
** 函数名	: OS_RemoveTmr
** 函数描述	: 删除一个已创建的定时器。
** 参数		: [in] id: 定时器ID
** 返回		: 无
********************************************************************/
void OS_RemoveTmr(INT8U id);

/*******************************************************************
** 函数名	: OS_StartTmr
** 函数描述	: 启动定时器，到期自动重装，周期执行。
** 参数		: [in] id:     定时器ID
** 参数		: [in] attrib: 定时时间单位，_TICK/_SECOND/_MINUTE
** 参数		: [in] time:   定时时长
** 返回		: 无
********************************************************************/
void OS_StartTmr(INT8U id, INT32U attrib, INT32U time);

/*******************************************************************
** 函数名	: OS_StopTmr
** 函数描述	: 停止定时器。
** 参数		: [in] id: 定时器ID
** 返回		: 无
********************************************************************/
void OS_StopTmr(INT8U id);

/*******************************************************************
** 函数名	: OS_GetLeftTime
** 函数描述	: 获取定时器剩余等待时间。
** 参数		: [in] id: 定时器ID
** 返回		: 剩余时间，单位一个tick
********************************************************************/
INT32U OS_GetLeftTime(INT8U id);

/*******************************************************************
** 函数名	: OS_TmrIsRun
** 函数描述	: 判断定时器是否处于运行状态。
** 参数		: [in] id: 定时器ID
** 返回		: true运行，false停止
********************************************************************/
INT8U OS_TmrIsRun(INT8U id);

/*******************************************************************
** 函数名	: OS_TmrTskEntry
** 函数描述	: 软件定时器调度入口，主循环中周期调用，到期直接执行回调。
** 参数		: 无
** 返回		: 无
********************************************************************/
void OS_TmrTskEntry(void);

#endif /* OS_TIMER_H */
