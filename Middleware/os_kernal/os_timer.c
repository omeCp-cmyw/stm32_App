#include "os_include.h"
#include "drv_systick.h"

/* 定时器控制块表 */
static OS_TMR_T s_tcb[OS_MAX_TIMER];

/* 上一次调度时的tick值 */
static INT32U s_preticks;

/*******************************************************************
** 函数名	: OS_InitTimer
** 函数描述	: 定时器模块初始化，须在SYSTICK_Init之后调用。
** 参数		: 无
** 返回		: 无
********************************************************************/
void OS_InitTimer(void)
{
    INT8U i;

    for (i = 0; i < OS_MAX_TIMER; i++) {
        s_tcb[i].ready   = false;
        s_tcb[i].tmrproc = 0;
        s_tcb[i].bakproc = 0;
    }
    s_preticks = SYSTICK_GetTick();
}

/*******************************************************************
** 函数名	: OS_CreateTmr
** 函数描述	: 创建一个软件定时器。
** 参数		: [in] index:   回调函数的参数指针
** 参数		: [in] tmrproc: 到期回调函数
** 返回		: 定时器ID，安装失败返回0xff
********************************************************************/
INT8U OS_CreateTmr(void *index, void (*tmrproc)(void *index))
{
    INT8U id;

    OS_ASSERT((tmrproc != 0), 0xff);

    for (id = 0; id < OS_MAX_TIMER; id++) {                                    /* 查找空闲定时器 */
        if (s_tcb[id].tmrproc == 0) {
            break;
        }
    }
    OS_ASSERT((id < OS_MAX_TIMER), 0xff);                                      /* 无空闲定时器 */

    s_tcb[id].ready     = FALSE;
    s_tcb[id].index     = index;
    s_tcb[id].totaltime = 0;
    s_tcb[id].lefttime  = 0;
    s_tcb[id].tmrproc   = tmrproc;
    s_tcb[id].bakproc   = tmrproc;

    return id;
}

/*******************************************************************
** 函数名	: OS_RemoveTmr
** 函数描述	: 删除一个已创建的定时器。
** 参数		: [in] id: 定时器ID
** 返回		: 无
********************************************************************/
void OS_RemoveTmr(INT8U id)
{
    OS_ASSERT((id < OS_MAX_TIMER), RETURN_VOID);

    s_tcb[id].index     = 0;
    s_tcb[id].totaltime = 0;
    s_tcb[id].lefttime  = 0;
    s_tcb[id].ready     = FALSE;
    s_tcb[id].tmrproc   = 0;
    s_tcb[id].bakproc   = 0;
}

/*******************************************************************
** 函数名	: OS_StartTmr
** 函数描述	: 启动定时器，到期自动重装，周期执行。
** 参数		: [in] id:     定时器ID
** 参数		: [in] attrib: 定时时间单位，_TICK/_SECOND/_MINUTE
** 参数		: [in] time:   定时时长
** 返回		: 无
********************************************************************/
void OS_StartTmr(INT8U id, INT32U attrib, INT32U time)
{
    OS_ASSERT((id < OS_MAX_TIMER && s_tcb[id].tmrproc != 0), RETURN_VOID);

    s_tcb[id].totaltime = (attrib * time);
    s_tcb[id].lefttime  = s_tcb[id].totaltime;
    s_tcb[id].ready     = FALSE;
}

/*******************************************************************
** 函数名	: OS_StopTmr
** 函数描述	: 停止定时器。
** 参数		: [in] id: 定时器ID
** 返回		: 无
********************************************************************/
void OS_StopTmr(INT8U id)
{
    OS_ASSERT((id < OS_MAX_TIMER), RETURN_VOID);

    s_tcb[id].totaltime = 0;
    s_tcb[id].lefttime  = 0;
    s_tcb[id].ready     = FALSE;
}

/*******************************************************************
** 函数名	: OS_GetLeftTime
** 函数描述	: 获取定时器剩余等待时间。
** 参数		: [in] id: 定时器ID
** 返回		: 剩余时间，单位一个tick
********************************************************************/
INT32U OS_GetLeftTime(INT8U id)
{
    OS_ASSERT((id < OS_MAX_TIMER), 0);

    if (s_tcb[id].tmrproc != 0 && s_tcb[id].totaltime > 0) {
        return (s_tcb[id].lefttime);
    } else {
        return 0;
    }
}

/*******************************************************************
** 函数名	: OS_TmrIsRun
** 函数描述	: 判断定时器是否处于运行状态。
** 参数		: [in] id: 定时器ID
** 返回		: true运行，false停止
********************************************************************/
INT8U OS_TmrIsRun(INT8U id)
{
    OS_ASSERT((id < OS_MAX_TIMER), RETURN_FALSE);

    if (s_tcb[id].tmrproc != 0 && s_tcb[id].totaltime > 0) {
        return true;
    } else {
        return false;
    }
}

/*******************************************************************
** 函数名	: OS_TmrTskEntry
** 函数描述	: 软件定时器调度入口，按tick差值倒计时，到期直接执行回调。
** 参数		: 无
** 返回		: 无
********************************************************************/
void OS_TmrTskEntry(void)
{
    INT8U i;
    INT32U time, curticks;

    curticks = SYSTICK_GetTick();
    if (s_preticks == curticks) {
        return;
    }

    ClearWatchdog();
    if (curticks >= s_preticks) {
        time = curticks - s_preticks;
    } else {
        time = (0xFFFFFFFF - s_preticks) + curticks + 1;                       /* tick回绕 */
    }
    s_preticks = curticks;                                                     /* 保存当前tick基准 */

    for (i = 0; i < OS_MAX_TIMER; i++) {
        if (s_tcb[i].tmrproc != 0 && s_tcb[i].totaltime > 0) {
            if (s_tcb[i].lefttime >= time) {
                s_tcb[i].lefttime -= time;
            } else {
                s_tcb[i].lefttime = 0;
            }

            if (s_tcb[i].lefttime == 0) {
                OS_ASSERT((s_tcb[i].tmrproc == s_tcb[i].bakproc), RETURN_VOID);

                s_tcb[i].lefttime = s_tcb[i].totaltime;                        /* 重装，周期执行 */
                s_tcb[i].tmrproc(s_tcb[i].index);
            }
        }
    }
}
