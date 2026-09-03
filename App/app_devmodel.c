#include <stdio.h>
#include <string.h>
#include "app_devmodel.h"
#include "cloud_agent.h"
#include "product_def.h"
#include "sensor_smoke.h"
#include "drv_beep.h"

/*
 * 物模型绑定实现：
 * 上行: 传感器采样值经cloud_set_property写入云端物模型(周期上报携带)；
 * 下行: 云端下发值更新本地影子并驱动执行器(当前日志占位)。
 * 规则: 温度/湿度越限时置告警, 由app_gateway读取并上报事件。
 * 影子值统一x10定点存储, 与sensor_sample_t的fvalue_x10对齐。
 */

#define DM_SHADOW_MAX   16

typedef struct {
    const char *key;
    int32_t     val_x10;    /* 统一x10定点值(整数也乘10, 比较同尺度) */
} dm_shadow_t;

static dm_shadow_t s_shadow[DM_SHADOW_MAX];
static uint8_t s_shadow_num;
static uint8_t s_alarm;             /* 越限告警标志(读后清) */
static int32_t s_smoke_hi_cnt;      /* 烟雾超阈值连续计数(防毛刺) */
static int32_t s_smoke_lo_cnt;      /* 烟雾低于解除阈值连续计数(迟滞) */

/* 影子初值表: 由product_def.h宏展开生成, 与云端物模型默认值一致 */
#undef PROPERTY_DEF
#define PROPERTY_DEF(_KEY_, _TYPE_, _WRITABLE_, _INT_DEF_, _FLOAT_DEF_) \
    { _KEY_, (_TYPE_ == PROP_FLOAT) ? (int32_t)(_FLOAT_DEF_ * 10.0f + 0.5f) \
                                    : (int32_t)(_INT_DEF_ * 10) },
static const dm_shadow_t s_defs[] = {
    PRODUCT_PROPERTY_TABLE
};
#undef PROPERTY_DEF

/*******************************************************************
** 函数名	: AppAtofX10
** 函数描述	: 字符串转x10定点(MicroLIB兼容, 支持负数与1位小数, true/false)
** 参数		: [in] s: 数字串("25.5"/"45"/"true")
** 返回		: 数值x10
********************************************************************/
static int32_t AppAtofX10(const char *s)
{
    int neg = 0;
    int32_t ip = 0, dec = 0;

    if (*s == 't' || *s == 'T') {   /* "true" -> 1.0 */
        return 10;
    }
    if (*s == 'f' || *s == 'F') {   /* "false" -> 0.0 */
        return 0;
    }
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        ip = ip * 10 + (*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        if (*s >= '0' && *s <= '9') {
            dec = *s - '0';
        }
    }
    return neg ? -(ip * 10 + dec) : ip * 10 + dec;
}

/*******************************************************************
** 函数名	: DmShadowFind
** 函数描述	: 按属性名查找影子值指针
** 参数		: [in] key: 属性名
** 返回		: 影子值指针, 未找到NULL
********************************************************************/
static int32_t *DmShadowFind(const char *key)
{
    int i;

    for (i = 0; i < s_shadow_num; i++) {
        if (strcmp(s_shadow[i].key, key) == 0) {
            return &s_shadow[i].val_x10;
        }
    }
    return 0;
}

/*******************************************************************
** 函数名	: DevModelRule
** 函数描述	: 规则分发：当前仅烟雾告警规则(温湿度阈值属性已删)
** 参数		: [in] key: 刚更新的属性名
** 返回		: 无
********************************************************************/
static void DevModelSmokeRule(void);

static void DevModelRule(const char *key)
{
    if (strcmp(key, "smoke_value") == 0) {
        DevModelSmokeRule();
    }
}

/*******************************************************************
** 函数名	: DevModelSmokeRule
** 函数描述	: 烟雾规则：电压超告警阈值连续N次->smoke_alarm=1(置告警)；
**             低于解除阈值连续N次->smoke_alarm=0(防抖)
** 参数		: 无
** 返回		: 无
********************************************************************/
static void DevModelSmokeRule(void)
{
    int32_t *val = DmShadowFind("smoke_value");
    int32_t *alarm = DmShadowFind("smoke_alarm");
    int32_t alarm_x10 = (int32_t)SMOKE_ALARM_MV * 10;
    int32_t clear_x10 = (int32_t)SMOKE_CLEAR_MV * 10;
    uint8_t cur_state = (alarm != 0 && *alarm != 0) ? 1 : 0;

    if (val == 0) {
        return;
    }
    if (!cur_state && *val >= alarm_x10) {
        /* 未告警: 超阈值连续N次才置告警(防单次毛刺) */
        if (++s_smoke_hi_cnt >= SMOKE_DEBOUNCE_N) {
            s_smoke_hi_cnt = 0;
            s_alarm = 1;                    /* 复用越限告警事件通道 */
            if (alarm != 0) {
                *alarm = 10;
            }
            BEEP_ON;
            cloud_set_property("smoke_alarm", "true");
            printf("[devmodel] smoke alarm on, %d mV\r\n", (int)(*val / 10));
        }
    } else if (cur_state && *val < clear_x10) {
        /* 已告警: 低于解除阈值连续N次才解除(防抖) */
        if (++s_smoke_lo_cnt >= SMOKE_DEBOUNCE_N) {
            s_smoke_lo_cnt = 0;
            if (alarm != 0) {
                *alarm = 0;
            }
            BEEP_OFF;
            cloud_set_property("smoke_alarm", "false");
            printf("[devmodel] smoke alarm off, %d mV\r\n", (int)(*val / 10));
        }
    } else {
        s_smoke_hi_cnt = 0;
        s_smoke_lo_cnt = 0;
    }
}

/*******************************************************************
** 函数名	: app_devmodel_init
** 函数描述	: 物模型绑定初始化：影子值从product_def.h默认值展开装载
** 参数		: 无
** 返回		: 无
********************************************************************/
void app_devmodel_init(void)
{
    int i;

    s_shadow_num = (uint8_t)(sizeof(s_defs) / sizeof(s_defs[0]));
    if (s_shadow_num > DM_SHADOW_MAX) {
        s_shadow_num = DM_SHADOW_MAX;
    }
    for (i = 0; i < s_shadow_num; i++) {
        s_shadow[i] = s_defs[i];
    }
    s_alarm = 0;
    printf("[devmodel] %u props shadowed\r\n", (unsigned int)s_shadow_num);
}

/*******************************************************************
** 函数名	: app_devmodel_sensor_apply
** 函数描述	: 传感器采样写入(注册给sensor框架)：写云端物模型+更新影子+越限判断
** 参数		: [in] s: 采样数据
** 返回		: 无
********************************************************************/
void app_devmodel_sensor_apply(const sensor_sample_t *s)
{
    char vbuf[24];
    int32_t *p;

    if (s == 0 || s->key == 0) {
        return;
    }

    /* 采样值转文本写入云端物模型(周期上报携带) */
    if (s->is_float) {
        snprintf(vbuf, sizeof(vbuf), "%d.%d",
                 (int)(s->fvalue_x10 / 10), (int)(s->fvalue_x10 % 10));
    } else {
        snprintf(vbuf, sizeof(vbuf), "%d", (int)s->ivalue);
    }
    cloud_set_property(s->key, vbuf);

    /* 更新影子并做越限判断 */
    p = DmShadowFind(s->key);
    if (p != 0) {
        *p = s->is_float ? (int32_t)s->fvalue_x10 : (int32_t)s->ivalue * 10;
        DevModelRule(s->key);
    }
}

/*******************************************************************
** 函数名	: app_devmodel_cloud_set
** 函数描述	: 云端属性下发执行(注册给cloud_init)：影子更新+执行器驱动
** 参数		: [in] key: 属性名
**          : [in] value: 值文本
** 返回		: 无
********************************************************************/
void app_devmodel_cloud_set(const char *key, const char *value)
{
    int32_t *p;

    if (key == 0 || value == 0) {
        return;
    }
    p = DmShadowFind(key);
    if (p == 0) {
        printf("[devmodel] unknown prop %s\r\n", key);
        return;
    }
    *p = AppAtofX10(value);

    /* 执行器: 接入真实执行器(继电器/加热器/阀门)时在此驱动 */
    printf("[devmodel] set %s = %s, exec placeholder\r\n", key, value);
}

/*******************************************************************
** 函数名	: app_devmodel_alarm_pending
** 函数描述	: 越限告警读取并清除(app_gateway周期调用)
** 参数		: 无
** 返回		: 1有告警, 0无
********************************************************************/
int app_devmodel_alarm_pending(void)
{
    int a = s_alarm;

    s_alarm = 0;
    return a;
}
