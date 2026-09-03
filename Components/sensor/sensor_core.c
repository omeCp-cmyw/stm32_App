#include <stdio.h>
#include "osal.h"
#include "sensor_core.h"

/*
 * 传感器框架实现：注册表 + 采集引擎。
 * 主循环sensor_poll()按各传感器period_ms到期触发read()，
 * 采集结果统一走物模型写入回调(sensor_apply_fn)。
 */

#define SENSOR_MAX      8       /* 注册表容量 */

static const sensor_t *s_list[SENSOR_MAX];
static uint8_t s_num;
static uint32_t s_last_ms[SENSOR_MAX];
static sensor_apply_fn s_apply;

/*******************************************************************
** 函数名	: sensor_core_init
** 函数描述	: 传感器框架初始化(注册物模型写入回调)，须在osal_init后
** 参数		: [in] apply: 采集数据写入回调
** 返回		: 无
********************************************************************/
void sensor_core_init(sensor_apply_fn apply)
{
    s_apply = apply;
    s_num = 0;
}

/*******************************************************************
** 函数名	: sensor_register
** 函数描述	: 注册传感器，内部调用其init
** 参数		: [in] s: 传感器接口(静态生命周期)
** 返回		: 1成功, 0失败(表满或init失败)
********************************************************************/
int sensor_register(const sensor_t *s)
{
    if (s == 0 || s->read == 0 || s_num >= SENSOR_MAX) {
        return 0;
    }
    if (s->init != 0 && s->init() != 0) {
        printf("[sensor] %s init failed\r\n", s->name);
        return 0;
    }
    s_list[s_num] = s;
    s_last_ms[s_num] = osal_time_ms();
    s_num++;
    printf("[sensor] %s registered, period %lums\r\n",
           s->name, (unsigned long)s->period_ms);
    return 1;
}

/*******************************************************************
** 函数名	: sensor_poll
** 函数描述	: 采集引擎轮询(主循环调用)：到期传感器执行read并回调物模型
** 参数		: 无
** 返回		: 无
********************************************************************/
void sensor_poll(void)
{
    uint32_t now = osal_time_ms();
    uint8_t i;

    for (i = 0; i < s_num; i++) {
        const sensor_t *s = s_list[i];
        sensor_sample_t smp;

        if (s->period_ms == 0 || now - s_last_ms[i] < s->period_ms) {
            continue;
        }
        s_last_ms[i] = now;

        if (s->read(&smp) == 0 && s_apply != 0) {
            smp.ts_ms = now;
            s_apply(&smp);
        }
    }
}
