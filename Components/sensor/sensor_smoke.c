#include "sensor_smoke.h"
#include "drv_adc.h"

/*
 * 烟雾传感器实现：ADC采集电压输出smoke_value(mV)。
 * read周期1s，输出原始电压，告警判断在App物模型规则层。
 */

#define SMOKE_PERIOD_MS 1000

/*******************************************************************
** 函数名	: SensorSmokeInit
** 函数描述	: 烟雾传感器初始化(ADC通道配置)
** 参数		: 无
** 返回		: 0成功
********************************************************************/
static int SensorSmokeInit(void)
{
    drv_adc_init();
    return 0;
}

/*******************************************************************
** 函数名	: SensorSmokeRead
** 函数描述	: 采集一次电压采样(均值滤波后换算mV)
** 参数		: [out] out: 采样输出(smoke_value, mV整数)
** 返回		: 0成功
********************************************************************/
static int SensorSmokeRead(sensor_sample_t *out)
{
    out->key = "smoke_value";
    out->is_float = 0;
    out->ivalue = (int32_t)drv_adc_read_mv();
    return 0;
}

/* 传感器注册表项 */
const sensor_t sensor_smoke = {
    "smoke(adc)",
    SensorSmokeInit,
    SensorSmokeRead,
    SMOKE_PERIOD_MS,
};
