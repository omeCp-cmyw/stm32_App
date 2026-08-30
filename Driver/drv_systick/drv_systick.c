#include "drv_systick.h"

static volatile uint32_t TimingDelay;

/* 系统tick计数，10us加1 */
static volatile uint32_t g_systicks;

/* 挂接在系统滴答上的周期回调表，每个SysTick中断执行一次 */
#define TICK_FUN_MAX        5
static void (*s_tick_fun[TICK_FUN_MAX])(void);

static void systick_delay_decrement(void);

/*******************************************************************
** 函数名	: SYSTICK_Init
** 函数描述	: 配置SysTick为10us中断一次，须在时钟配置完成后调用。
** 参数		: 无
** 返回		: 无
********************************************************************/
void SYSTICK_Init(void)
{
    /* SystemCoreClock / 1000    1ms中断一次
     * SystemCoreClock / 100000  10us中断一次
     * SystemCoreClock / 1000000 1us中断一次
     */
    if (SysTick_Config(SystemCoreClock / 100000))
    {
        while (1);
    }

    for (uint8_t i = 0; i < TICK_FUN_MAX; i++)
    {
        s_tick_fun[i] = 0;
    }
}

/*******************************************************************
** 函数名	: SYSTICK_DelayUs
** 函数描述	: 阻塞延时，单位10us，SYSTICK_DelayUs(1)即延时10us。
** 参数		: [in] nTime: 10us的个数
** 返回		: 无
********************************************************************/
void SYSTICK_DelayUs(uint32_t nTime)
{
    TimingDelay = nTime;

    while (TimingDelay != 0);
}

/*******************************************************************
** 函数名	: systick_delay_decrement
** 函数描述	: 延时计数递减，仅中断处理内部使用。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void systick_delay_decrement(void)
{
    if (TimingDelay != 0)
    {
        TimingDelay--;
    }
}

/*******************************************************************
** 函数名	: SYSTICK_InstallTickFun
** 函数描述	: 挂接滴答周期回调，每个SysTick中断(10us)执行一次，回调须极短。
** 参数		: [in] fun: 回调函数
** 返回		: 1成功，0失败（空指针或表满）。
********************************************************************/
uint8_t SYSTICK_InstallTickFun(void (*fun)(void))
{
    uint8_t i;

    if (fun == 0)
    {
        return 0;
    }

    for (i = 0; i < TICK_FUN_MAX; i++)
    {
        if (s_tick_fun[i] == 0)
        {
            s_tick_fun[i] = fun;
            return 1;
        }
    }
    return 0;
}

/*******************************************************************
** 函数名	: SYSTICK_GetTick
** 函数描述	: 获取系统tick计数，10us加1。
** 参数		: 无
** 返回		: tick值。
********************************************************************/
uint32_t SYSTICK_GetTick(void)
{
    return g_systicks;
}

/*******************************************************************
** 函数名	: SYSTICK_GetMsTick
** 函数描述	: 获取系统ms计数，上电后经过的毫秒数。
** 参数		: 无
** 返回		: ms值。
********************************************************************/
uint32_t SYSTICK_GetMsTick(void)
{
    return g_systicks / 100;
}

/*******************************************************************
** 函数名	: SYSTICK_DelayMs
** 函数描述	: 基于系统tick的ms级阻塞延时，不占用TimingDelay。
** 参数		: [in] period: 延时时间，单位ms。
** 返回		: 无
********************************************************************/
void SYSTICK_DelayMs(uint32_t period)
{
    uint32_t tick1 = g_systicks;

    while ((g_systicks - tick1) < (uint32_t)(period * 100));
}

/*******************************************************************
** 函数名	: SYSTICK_Handler
** 函数描述	: SysTick中断处理，在SysTick_Handler中调用。
** 参数		: 无
** 返回		: 无
********************************************************************/
void SYSTICK_Handler(void)
{
    static uint16_t hal_tick_cnt = 0;
    uint8_t i;

    systick_delay_decrement();

    /* 执行挂接的滴答回调 */
    for (i = 0; i < TICK_FUN_MAX; i++)
    {
        if (s_tick_fun[i] != 0)
        {
            s_tick_fun[i]();
        }
    }

    g_systicks++;

    /* 每100次10us中断给HAL喂一次1ms节拍，保证HAL_Delay/HAL_GetTick正常 */
    if (++hal_tick_cnt >= 100)
    {
        hal_tick_cnt = 0;
        HAL_IncTick();
    }
}
