#include <stdio.h>
#include "drv_tim7.h"

static TIM_HandleTypeDef htim7;

/* 中断触发次数计数 */
static volatile uint32_t tim7_tick;

/*******************************************************************
** 函数名	: TIM7_NVIC_Configuration
** 函数描述	: TIM7中断优先级配置（对应野火例程 TIMx_NVIC_Configuration）
**			HAL_Init()已将NVIC优先级分组设为4位抢占(分组4)，
**			此处不再重复设置分组，避免影响串口/DMA等已配置的中断
** 参数		: 无
** 返回		: 无
********************************************************************/
static void TIM7_NVIC_Configuration(void)
{
	/* 设置中断优先级（低于串口/DMA的优先级1）并使能中断 */
	HAL_NVIC_SetPriority(BASIC_TIM_IRQn, 2, 0);
	HAL_NVIC_EnableIRQ(BASIC_TIM_IRQn);
}

/*******************************************************************
** 函数名	: TIM7_Mode_Config
** 函数描述	: TIM7定时器配置
**			TIM6/7是16位定时器，只需配置预分频和自动重装，
**			计数模式固定向上计数。
**			定时器时钟84MHz（APB1=42MHz，预分频非1时定时器时钟x2）
** 参数		: [in] period_ms: 定时周期，单位毫秒（1 ~ 32767）
** 返回		: 无
********************************************************************/
static void TIM7_Mode_Config(uint32_t period_ms)
{
	/* 开启TIM7外设时钟 */
	__HAL_RCC_TIM7_CLK_ENABLE();

	/* 定时器时钟源84MHz，设预分频42000 -> 计数频率2KHz，
	   即每个计数0.5ms，计数period_ms*2次溢出，产生一次中断。
	   注意：PSC/ARR寄存器均为16位(最大65535)，84000-1会溢出截断，
	   故不能用1KHz方案，2KHz下最长周期约32.7s */
	htim7.Instance = BASIC_TIM;
	htim7.Init.Prescaler = 42000 - 1;             /* 84MHz/42000 = 2KHz */
	htim7.Init.CounterMode = TIM_COUNTERMODE_UP;  /* 向上计数，基本定时器唯一模式 */
	htim7.Init.Period = period_ms * 2 - 1;        /* 计数period_ms毫秒溢出 */
	htim7.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	HAL_TIM_Base_Init(&htim7);

	/* 清除定时器溢出中断标志位（野火例程同步骤） */
	__HAL_TIM_CLEAR_FLAG(&htim7, TIM_FLAG_UPDATE);

	/* 启动定时器并使能更新中断（对应野火 TIM_ITConfig + TIM_Cmd） */
	HAL_TIM_Base_Start_IT(&htim7);
}

/*******************************************************************
** 函数名	: TIM7_Debug_Config
** 函数描述	: 初始化TIM7基本定时器
** 返回		: 无
********************************************************************/
void TIM7_Debug_Config(uint32_t period_ms)
{
	TIM7_NVIC_Configuration();

	TIM7_Mode_Config(period_ms);
}

/*******************************************************************
** 函数名	: TIM7_Debug_Print
** 函数描述	: TIM7溢出处理：清中断标志、累加计数并打印调试信息。
**			仅调试用：printf为阻塞发送，期间主循环暂停(115200下约2ms)，
**			且本中断内SysTick无法嵌套，发送超时保护失效，
**			串口异常时会死等；正式代码建议改为置标志、主循环打印
** 参数		: 无
** 返回		: 无
********************************************************************/
static void TIM7_Debug_Print(void)
{
	__HAL_TIM_CLEAR_FLAG(&htim7, TIM_FLAG_UPDATE);
	tim7_tick++;
	printf("[TIM7] tick==%u, uptime==%u s\r\n",
	       (unsigned int)tim7_tick,
	       (unsigned int)(tim7_tick * 10));
}

/*******************************************************************
** 函数名	: PERIOD_TIMER_IRQHandler
** 函数描述	: TIM7全局中断服务函数（宏展开为向量表固定名TIM7_IRQHandler）
** 参数		: 无
** 返回		: 无
********************************************************************/
void PERIOD_TIMER_IRQHandler(void)
{
	if (__HAL_TIM_GET_FLAG(&htim7, TIM_FLAG_UPDATE) != RESET)
	{
		TIM7_Debug_Print();
	}
}

