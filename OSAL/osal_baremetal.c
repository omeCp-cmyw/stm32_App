#include <stdio.h>
#include <string.h>
#include "osal.h"
#include "port_base.h"
#include "drv_systick.h"

/*
 * OSAL裸机实现：
 * 软件定时器池+错误管理，时基来自drv_systick的10us tick。
 * 移植FreeRTOS时新增osal_freertos.c实现osal.h同一接口即可。
 */

/* 定时器控制块 */
typedef struct {
    void    *index;                     /* 回调参数指针 */
    uint32_t totaltime;                 /* 定时总时长，单位tick */
    uint32_t lefttime;                  /* 剩余时间，单位tick */
    uint8_t  periodic;                  /* 1周期重装执行 */
    void   (*tmrproc)(void *index);     /* 到期回调 */
    void   (*bakproc)(void *index);     /* 回调备份，用于完整性校验 */
} OSAL_TCB_T;

/* 错误管理控制块 */
#define OSAL_RESET_REG_MAX      10
#define OSAL_MAX_DIAG           10

typedef struct {
    uint8_t curindex;
    uint8_t nused;
    uint8_t nbackup;
    uint8_t ct_reset;

    void (*diagproc[OSAL_MAX_DIAG])(void);
    void (*backproc[OSAL_MAX_DIAG])(void);

    void (*c_informer[OSAL_RESET_PRI_MAX][OSAL_RESET_REG_MAX])(uint8_t event, char *filename, uint32_t line);
    void (*b_informer[OSAL_RESET_PRI_MAX][OSAL_RESET_REG_MAX])(uint8_t event, char *filename, uint32_t line);
} OSAL_ECB_T;

static OSAL_TCB_T s_tcb[OSAL_MAX_TIMER];
static uint32_t   s_preticks;
static OSAL_ECB_T s_ecb;
static osal_watchdog_fn s_watchdog;

/*******************************************************************
** 函数名	: osal_register_watchdog
** 函数描述	: 注册看门狗喂狗回调，osal_task_loop与osal_reset内自动调用
** 参数		: [in] fn: 喂狗函数，传0取消
** 返回		: 无
********************************************************************/
void osal_register_watchdog(osal_watchdog_fn fn)
{
    s_watchdog = fn;
}

/*******************************************************************
** 函数名	: osal_init
** 函数描述	: OSAL初始化：软件定时器池+错误管理，须在时基就绪后调用
** 参数		: 无
** 返回		: 无
********************************************************************/
void osal_init(void)
{
    uint8_t i;

    for (i = 0; i < OSAL_MAX_TIMER; i++) {
        s_tcb[i].totaltime = 0;
        s_tcb[i].lefttime  = 0;
        s_tcb[i].periodic  = 0;
        s_tcb[i].tmrproc   = 0;
        s_tcb[i].bakproc   = 0;
    }
    s_preticks = SYSTICK_GetTick();
    memset(&s_ecb, 0, sizeof(s_ecb));
}

/*******************************************************************
** 函数名	: osal_time_tick
** 函数描述	: 获取系统tick计数
** 参数		: 无
** 返回		: tick值，1tick=10us
********************************************************************/
uint32_t osal_time_tick(void)
{
    return SYSTICK_GetTick();
}

/*******************************************************************
** 函数名	: osal_time_ms
** 函数描述	: 获取系统毫秒计数
** 参数		: 无
** 返回		: 毫秒值
********************************************************************/
uint32_t osal_time_ms(void)
{
    return SYSTICK_GetMsTick();
}

/*******************************************************************
** 函数名	: osal_timer_create
** 函数描述	: 创建软件定时器
** 参数		: [in] index: 回调参数指针
**          : [in] cb:    到期回调
** 返回		: 定时器ID，失败返回0xff
********************************************************************/
uint8_t osal_timer_create(void *index, void (*cb)(void *index))
{
    uint8_t id;

    OSAL_ASSERT((cb != 0), 0xff);

    for (id = 0; id < OSAL_MAX_TIMER; id++) {                               /* 查找空闲定时器 */
        if (s_tcb[id].tmrproc == 0) {
            break;
        }
    }
    OSAL_ASSERT((id < OSAL_MAX_TIMER), 0xff);                               /* 无空闲定时器 */

    s_tcb[id].index     = index;
    s_tcb[id].totaltime = 0;
    s_tcb[id].lefttime  = 0;
    s_tcb[id].periodic  = 0;
    s_tcb[id].tmrproc   = cb;
    s_tcb[id].bakproc   = cb;

    return id;
}

/*******************************************************************
** 函数名	: osal_timer_delete
** 函数描述	: 删除定时器
** 参数		: [in] id: 定时器ID
** 返回		: 无
********************************************************************/
void osal_timer_delete(uint8_t id)
{
    OSAL_ASSERT((id < OSAL_MAX_TIMER), RETURN_VOID);

    s_tcb[id].index     = 0;
    s_tcb[id].totaltime = 0;
    s_tcb[id].lefttime  = 0;
    s_tcb[id].periodic  = 0;
    s_tcb[id].tmrproc   = 0;
    s_tcb[id].bakproc   = 0;
}

/*******************************************************************
** 函数名	: osal_timer_start
** 函数描述	: 启动定时器（按毫秒计）
** 参数		: [in] id:       定时器ID
**          : [in] ms:       定时时长(ms)
**          : [in] periodic: 1周期重装执行，0单次到期自停
** 返回		: 无
********************************************************************/
void osal_timer_start(uint8_t id, uint32_t ms, uint8_t periodic)
{
    OSAL_ASSERT((id < OSAL_MAX_TIMER && s_tcb[id].tmrproc != 0), RETURN_VOID);

    s_tcb[id].totaltime = ms * OSAL_TICK_MS;
    s_tcb[id].lefttime  = s_tcb[id].totaltime;
    s_tcb[id].periodic  = periodic;
}

/*******************************************************************
** 函数名	: osal_timer_stop
** 函数描述	: 停止定时器
** 参数		: [in] id: 定时器ID
** 返回		: 无
********************************************************************/
void osal_timer_stop(uint8_t id)
{
    OSAL_ASSERT((id < OSAL_MAX_TIMER), RETURN_VOID);

    s_tcb[id].totaltime = 0;
    s_tcb[id].lefttime  = 0;
    s_tcb[id].periodic  = 0;
}

/*******************************************************************
** 函数名	: osal_timer_is_run
** 函数描述	: 查询定时器是否运行中
** 参数		: [in] id: 定时器ID
** 返回		: 1运行，0停止
********************************************************************/
uint8_t osal_timer_is_run(uint8_t id)
{
    OSAL_ASSERT((id < OSAL_MAX_TIMER), 0);

    if (s_tcb[id].tmrproc != 0 && s_tcb[id].totaltime > 0) {
        return 1;
    } else {
        return 0;
    }
}

/*******************************************************************
** 函数名	: osal_timer_left_ms
** 函数描述	: 获取定时器剩余时间
** 参数		: [in] id: 定时器ID
** 返回		: 剩余时间(ms)
********************************************************************/
uint32_t osal_timer_left_ms(uint8_t id)
{
    OSAL_ASSERT((id < OSAL_MAX_TIMER), 0);

    if (s_tcb[id].tmrproc != 0 && s_tcb[id].totaltime > 0) {
        return (s_tcb[id].lefttime / OSAL_TICK_MS);
    } else {
        return 0;
    }
}

/*******************************************************************
** 函数名	: osal_enter_critical
** 函数描述	: 进入临界区（关全局中断）
** 参数		: 无
** 返回		: 无
********************************************************************/
void osal_enter_critical(void)
{
    port_enter_critical();
}

/*******************************************************************
** 函数名	: osal_exit_critical
** 函数描述	: 退出临界区（恢复全局中断）
** 参数		: 无
** 返回		: 无
********************************************************************/
void osal_exit_critical(void)
{
    port_exit_critical();
}

/*******************************************************************
** 函数名	: osal_task_loop
** 函数描述	: 主循环调度入口：喂狗+软件定时器调度+诊断函数轮询
** 参数		: 无
** 返回		: 无
********************************************************************/
void osal_task_loop(void)
{
    uint8_t  i;
    uint32_t time, curticks;

    if (s_watchdog != 0) {
        s_watchdog();                                                       /* 喂狗：主循环任一环节阻塞超时即复位 */
    }

    curticks = SYSTICK_GetTick();
    if (s_preticks != curticks) {
        if (curticks >= s_preticks) {
            time = curticks - s_preticks;
        } else {
            time = (0xFFFFFFFF - s_preticks) + curticks + 1;                /* tick回绕 */
        }
        s_preticks = curticks;                                              /* 保存当前tick基准 */

        for (i = 0; i < OSAL_MAX_TIMER; i++) {
            if (s_tcb[i].tmrproc != 0 && s_tcb[i].totaltime > 0) {
                if (s_tcb[i].lefttime >= time) {
                    s_tcb[i].lefttime -= time;
                } else {
                    s_tcb[i].lefttime = 0;
                }

                if (s_tcb[i].lefttime == 0) {
                    OSAL_ASSERT((s_tcb[i].tmrproc == s_tcb[i].bakproc), RETURN_VOID);

                    if (s_tcb[i].periodic) {
                        s_tcb[i].lefttime = s_tcb[i].totaltime;             /* 重装，周期执行 */
                    } else {
                        s_tcb[i].totaltime = 0;                             /* 单次执行后自停 */
                    }
                    s_tcb[i].tmrproc(s_tcb[i].index);
                }
            }
        }
    }

    /* 诊断函数轮询 */
    OSAL_ASSERT((s_ecb.nused == s_ecb.nbackup && s_ecb.nused <= OSAL_MAX_DIAG), RETURN_VOID);
    if (s_ecb.nused == 0) {
        return;
    }

    i = s_ecb.curindex++;
    if (i >= s_ecb.nused) {
        i = 0;
        s_ecb.curindex = 0;
    }
    OSAL_ASSERT((s_ecb.diagproc[i] == s_ecb.backproc[i] && s_ecb.diagproc[i] != 0), RETURN_VOID);
    s_ecb.diagproc[i]();
}

/*******************************************************************
** 函数名	: osal_reset
** 函数描述	: 复位设备，复位前依次调用已注册的通知回调
** 参数		: [in] event:    复位事件，见OSAL_RESET_EVENT_E
**          : [in] filename: 触发文件名
**          : [in] line:     触发行号
** 返回		: 无
********************************************************************/
void osal_reset(uint8_t event, char *filename, uint32_t line)
{
    uint8_t  i, j, len;
    uint8_t *ptr;

    len = (uint8_t)strlen(filename);
    ptr = (uint8_t *)filename;
    for (i = len; i > 0; i--) {                                             /* 只保留文件名部分 */
        if (ptr[i - 1] == '\\') {
            break;
        }
    }
    ptr += i;

    #if OSAL_DEBUG_ERR > 0
    printf("<assert:file(%s), line(%u), event(%u)>\r\n", (char *)ptr, (unsigned int)line, (unsigned int)event);
    #endif

    s_ecb.ct_reset++;
    if (event != OSAL_RESET_DIRECT && s_ecb.ct_reset <= 1) {
        for (i = 0; i < OSAL_RESET_PRI_MAX; i++) {
            for (j = 0; j < OSAL_RESET_REG_MAX; j++) {
                if (s_ecb.c_informer[i][j] == s_ecb.b_informer[i][j] && s_ecb.c_informer[i][j] != 0) {
                    s_ecb.c_informer[i][j](event, (char *)ptr, line);
                }
            }
        }
    }

    if (s_watchdog != 0) {
        s_watchdog();                                                       /* 复位前喂一次狗，保证复位标志清晰 */
    }
    port_system_reset();
}

/*******************************************************************
** 函数名	: osal_register_reset_inform
** 函数描述	: 注册复位前通知回调
** 参数		: [in] prior: 优先级，见OSAL_RESET_PRI_E
**          : [in] fp:    回调
** 返回		: 成功1，失败0
********************************************************************/
uint8_t osal_register_reset_inform(uint8_t prior, void (*fp)(uint8_t event, char *filename, uint32_t line))
{
    uint8_t i;

    if (fp == 0) {
        return 0;
    }

    if (prior >= OSAL_RESET_PRI_MAX) {
        return 0;
    }

    for (i = 0; i < OSAL_RESET_REG_MAX; i++) {
        if (s_ecb.c_informer[prior][i] == 0) {
            s_ecb.c_informer[prior][i] = fp;
            s_ecb.b_informer[prior][i] = fp;
            return 1;
        }
    }
    OSAL_ASSERT(0, 0);
}

/*******************************************************************
** 函数名	: osal_register_diag
** 函数描述	: 注册诊断函数，由osal_task_loop轮询执行
** 参数		: [in] fp: 诊断函数
** 返回		: 成功1，失败0
********************************************************************/
uint8_t osal_register_diag(void (*fp)(void))
{
    OSAL_ASSERT((s_ecb.nused < OSAL_MAX_DIAG && fp != 0), 0);

    s_ecb.diagproc[s_ecb.nused++]   = fp;
    s_ecb.backproc[s_ecb.nbackup++] = fp;

    return 1;
}
