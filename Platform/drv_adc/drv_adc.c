/********************************************************************************
**
** 文件名:     drv_adc.c
** 版权所有:   无
** 文件描述:   该模块主要实现ADC采样驱动（MQ2模拟量采集）
**
*********************************************************************************/

#include "drv_adc.h"
#include "stm32f4xx_hal.h"

/*
 * ADC1驱动实现：单通道轮询软件触发采样+均值滤波。
 * 依赖: stm32f4xx_hal_adc.c（HAL_ADC_MODULE_ENABLED已启用）。
 * 引脚/通道配置集中在本文件顶部通道表宏，改接线只动表。
 * 参考: STM32F4参考工程 Platform/drv/drv_adc（单通道）。
 */

#define ADC_FILTER_N    8               /* 均值滤波次数（气敏传感器噪声大） */
#define ADC_VREF_MV     3300            /* 参考电压（mV） */
#define ADC_RESOLUTION  4096            /* 12bit分辨率量程 */
#define ADC_TIMEOUT_MS  10              /* 单次转换超时 */

/* 通道配置表：{GPIO端口, GPIO引脚, ADC通道号} */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint32_t channel;
} adc_chan_cfg_t;

static const adc_chan_cfg_t s_chan_cfg[ADC_CH_MAX] = {
    { GPIOA, GPIO_PIN_0, ADC_CHANNEL_0 },    /* ADC_CH_MQ2: PA0 */
};

static ADC_HandleTypeDef s_hadc;

/*******************************************************************************
** 函数名称    drv_adc_init
** 函数说明    初始化ADC1单通道（GPIO模拟输入+12bit软件触发配置）
** 输入参数    无
** 输出参数    无
** 返回参数    无
*******************************************************************************/
void drv_adc_init(void)
{
    GPIO_InitTypeDef gio;
    int i;

    /* 引脚: 模拟输入 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gio.Mode = GPIO_MODE_ANALOG;
    gio.Pull = GPIO_NOPULL;
    for (i = 0; i < ADC_CH_MAX; i++) {
        gio.Pin = s_chan_cfg[i].pin;
        HAL_GPIO_Init(s_chan_cfg[i].port, &gio);
    }

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
}

/*******************************************************************************
** 函数名称    drv_adc_read
** 函数说明    指定通道连续N次采样取均值（滤波），返回原始ADC值
** 输入参数    ch: ADC通道
** 输出参数    无
** 返回参数    采样值（12bit 0~4095），通道非法返回0
*******************************************************************************/
uint16_t drv_adc_read(adc_ch_t ch)
{
    ADC_ChannelConfTypeDef chan;
    uint32_t sum = 0;
    int i;

    if (ch >= ADC_CH_MAX) {
        return 0;
    }

    /* 切换当前采样通道 */
    chan.Channel = s_chan_cfg[ch].channel;
    chan.Rank = 1;
    chan.SamplingTime = ADC_SAMPLETIME_480CYCLES;   /* 采样时间取大降噪 */
    chan.Offset = 0;
    HAL_ADC_ConfigChannel(&s_hadc, &chan);

    for (i = 0; i < ADC_FILTER_N; i++) {
        HAL_ADC_Start(&s_hadc);
        if (HAL_ADC_PollForConversion(&s_hadc, ADC_TIMEOUT_MS) == HAL_OK) {
            sum += HAL_ADC_GetValue(&s_hadc);
        }
        HAL_ADC_Stop(&s_hadc);
    }
    return (uint16_t)(sum / ADC_FILTER_N);
}

/*******************************************************************************
** 函数名称    drv_adc_read_mv
** 函数说明    指定通道采样并换算电压值
** 输入参数    ch: ADC通道
** 输出参数    无
** 返回参数    电压（mV），3.3V参考，通道非法返回0
*******************************************************************************/
uint16_t drv_adc_read_mv(adc_ch_t ch)
{
    uint16_t adc_val = drv_adc_read(ch);

    return (uint16_t)((uint32_t)adc_val * ADC_VREF_MV / ADC_RESOLUTION);
}
