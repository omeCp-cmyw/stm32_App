#include "drv_iwdg.h"

static IWDG_HandleTypeDef hiwdg;

/*******************************************************************
** 函数名	: IWDG_Init
** 函数描述	: 初始化独立看门狗并启动计数，超时未喂狗则复位系统。
**			时钟源为内部LSI(典型32kHz，随温度漂移30~60kHz，定时不精确)，
**			超时时间 Tout = prv/32 * rlv (ms)，prv为分频系数4~256，
**			rlv为重装载值0~0xFFF。启动后主循环须周期性调用IWDG_Feed。
** 参数		: [in] timeout_ms: 超时时间，单位毫秒(应大于主循环最长阻塞时间)
** 返回		: 无
********************************************************************/
void IWDG_Init(uint32_t timeout_ms)
{
    static const uint32_t presc_tbl[] = {
        IWDG_PRESCALER_4,   IWDG_PRESCALER_8,   IWDG_PRESCALER_16,
        IWDG_PRESCALER_32,  IWDG_PRESCALER_64,  IWDG_PRESCALER_128,
        IWDG_PRESCALER_256
    };
    static const uint32_t div_tbl[] = {4, 8, 16, 32, 64, 128, 256};
    uint8_t i;

    hiwdg.Instance = IWDG;
    /* 默认最大分频、最大重装载，保证超时过小时也有合法值 */
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg.Init.Reload = 0xFFF;

    /* 从最细分频起选，取重装载值不超0xFFF的组合，分频越细超时精度越高 */
    for (i = 0; i < 7; i++) {
        if (timeout_ms * 32 <= 0xFFF * div_tbl[i]) {
            hiwdg.Init.Prescaler = presc_tbl[i];
            hiwdg.Init.Reload = timeout_ms * 32 / div_tbl[i];
            if (hiwdg.Init.Reload == 0) {
                hiwdg.Init.Reload = 1;
            }
            break;
        }
    }

    /* HAL_IWDG_Init内部使能LSI时钟并启动看门狗，启动后无法关闭 */
    HAL_IWDG_Init(&hiwdg);
}

/*******************************************************************
** 函数名	: IWDG_Feed
** 函数描述	: 喂狗：重装载计数器，须在超时时间内周期性调用，否则系统复位
** 参数		: 无
** 返回		: 无
********************************************************************/
void IWDG_Feed(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}
