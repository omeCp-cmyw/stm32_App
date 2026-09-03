#include "drv_adc.h"
#include "stm32f4xx_hal.h"

/*
 * ADC1驱动实现：单通道软件触发采样+均值滤波。
 * 依赖: stm32f4xx_hal_adc.c(HAL_ADC_MODULE_ENABLED已启用)。
 * 引脚/通道配置集中在本文件顶部宏, 改接线只动宏。
 */

#define ADC_PIN_GPIO    GPIOA
#define ADC_PIN_NUM     GPIO_PIN_0
#define ADC_CHANNEL     ADC_CHANNEL_0
#define ADC_FILTER_N    8               /* 均值滤波次数(气敏传感器噪声大) */
#define ADC_VREF_MV     3300            /* 参考电压(mV) */
#define ADC_RESOLUTION  4096            /* 12bit分辨率量程 */
#define ADC_TIMEOUT_MS  10              /* 单次转换超时 */

static ADC_HandleTypeDef s_hadc;

/*******************************************************************
** 函数名	: drv_adc_init
** 函数描述	: ADC1初始化：GPIO模拟输入+ADC配置(12bit软件触发)
** 参数		: 无
** 返回		: 无
********************************************************************/
void drv_adc_init(void)
{
    GPIO_InitTypeDef gio;
    ADC_ChannelConfTypeDef chan;

    /* 引脚: 模拟输入 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gio.Pin = ADC_PIN_NUM;
    gio.Mode = GPIO_MODE_ANALOG;
    gio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ADC_PIN_GPIO, &gio);

    /* ADC1: 12bit, 单次软件触发 */
    __HAL_RCC_ADC1_CLK_ENABLE();
    s_hadc.Instance = ADC1;
    s_hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4; /* PCLK2/4=21M */
    s_hadc.Init.Resolution = ADC_RESOLUTION_12B;
    s_hadc.Init.ScanConvMode = DISABLE;
    s_hadc.Init.ContinuousConvMode = DISABLE;
    s_hadc.Init.DiscontinuousConvMode = DISABLE;
    s_hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    s_hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    s_hadc.Init.NbrOfConversion = 1;
    s_hadc.Init.DMAContinuousRequests = DISABLE;
    s_hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    HAL_ADC_Init(&s_hadc);

    chan.Channel = ADC_CHANNEL;
    chan.Rank = 1;
    chan.SamplingTime = ADC_SAMPLETIME_480CYCLES;   /* 采样时间取大降噪 */
    chan.Offset = 0;
    HAL_ADC_ConfigChannel(&s_hadc, &chan);
}

/*******************************************************************
** 函数名	: drv_adc_read
** 函数描述	: 单通道连续N次采样取均值(滤波)，返回原始ADC值
** 参数		: 无
** 返回		: 采样值(12bit 0~4095)
********************************************************************/
uint16_t drv_adc_read(void)
{
    uint32_t sum = 0;
    int i;

    for (i = 0; i < ADC_FILTER_N; i++) {
        HAL_ADC_Start(&s_hadc);
        if (HAL_ADC_PollForConversion(&s_hadc, ADC_TIMEOUT_MS) == HAL_OK) {
            sum += HAL_ADC_GetValue(&s_hadc);
        }
        HAL_ADC_Stop(&s_hadc);
    }
    return (uint16_t)(sum / ADC_FILTER_N);
}

/*******************************************************************
** 函数名	: drv_adc_read_mv
** 函数描述	: 采样并换算电压值
** 参数		: 无
** 返回		: 电压(mV), 3.3V参考
********************************************************************/
uint16_t drv_adc_read_mv(void)
{
    uint16_t adc_val = drv_adc_read();
    return (uint16_t)((uint32_t)adc_val * ADC_VREF_MV / ADC_RESOLUTION);
}
