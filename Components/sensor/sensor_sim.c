#include "sensor_core.h"

/*
 * 示例传感器：模拟温湿度数据源(无硬件时的框架验证)。
 * 每5s产生一个缓慢波动的温湿度采样，供sensor框架采集与上报闭环。
 * 接入真实传感器(如SHT30)时替换本文件注册即可。
 */

#define SIM_PERIOD_MS   5000

static uint32_t s_tick;

/*******************************************************************
** 函数名	: SensorSimInit
** 函数描述	: 模拟传感器初始化(计数归零)
** 参数		: 无
** 返回		: 0成功
********************************************************************/
static int SensorSimInit(void)
{
    s_tick = 0;
    return 0;
}

/*******************************************************************
** 函数名	: SensorSimRead
** 函数描述	: 产出一次模拟采样：温度25.5±2.5波动、湿度45±10波动
** 参数		: [out] out: 采样输出(填充temp_value)
** 返回		: 0成功
********************************************************************/
static int SensorSimRead(sensor_sample_t *out)
{
    uint32_t phase = s_tick++;

    /* 温度: 24.0~27.0度 三角波; 湿度: 38%~52% 三角波 */
    out->key = "temp_value";
    out->is_float = 1;
    out->fvalue_x10 = 240 + (phase % 60);   /* 24.0 + 0.0~5.9 */
    return 0;
}

/* 传感器注册表项 */
const sensor_t sensor_sim = {
    "temp/hum(sim)",
    SensorSimInit,
    SensorSimRead,
    SIM_PERIOD_MS,
};
