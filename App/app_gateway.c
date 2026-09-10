#include <stdio.h>
#include "drv_systick.h"
#include "drv_led.h"
#include "sensor_core.h"
#include "cloud_agent.h"
#include "link.h"
#include "gateway_config.h"
#include "app_devmodel.h"
#include "app_gateway.h"
#include "../Components/cloud/ota/ota_mmi.h"

/*
 * 网关业务编排实现：
 * init: 物模型绑定 -> 传感器框架注册 -> 传感器注册 -> 云平台接入(带下发回调)；
 * loop: 传感器采集轮询 + 在线状态LED指示 + 越限告警事件上报。
 * 业务只依赖三个抽象接口(cloud_agent/sensor_core/link), 不感知具体实现。
 */

/* 云端接入配置: 由gateway_config.h集中参数组装(静态生命周期) */
static const cloud_cfg_t s_cloud_cfg = {
    GW_ONENET_BROKER_HOST,
    GW_ONENET_BROKER_PORT,
    GW_ONENET_CLIENT_ID,
    GW_ONENET_USERNAME,
    GW_ONENET_ACCESS_KEY,
    GW_ONENET_PRODUCT_ID,
    GW_ONENET_DEVICE_NAME,
};

/* 传感器注册表项(在Components/sensor下实现) */
extern const sensor_t sensor_sim;
extern const sensor_t sensor_smoke;

/* 状态指示LED刷新周期与告警上报防抖(ms) */
#define GW_LED_MS       500
#define GW_ALARM_MS     60000

/* 平台断链计数挂载测试：检测周期(ms) */
#define GW_DISCNT_MS    5000

/*******************************************************************
** 函数名	: app_gateway_init
** 函数描述	: 业务总初始化：物模型绑定->传感器注册->云平台接入
** 参数		: 无
** 返回		: 无
********************************************************************/
void app_gateway_init(void)
{
    printf("app_gateway init\r\n");

    /* 物模型绑定: 影子装载/规则/执行器 */
    app_devmodel_init();

    /* 传感器框架: 注册采集写入回调, 须在传感器注册前 */
    sensor_core_init(app_devmodel_sensor_apply);

    /* 注册传感器(新增传感器在此追加一行) */
    sensor_register(&sensor_sim);
    sensor_register(&sensor_smoke);

    /* 云平台: 暂时禁用MQTT，单独测试OTA
    cloud_init(&s_cloud_cfg, app_devmodel_cloud_set); */

    /* OTA升级: 状态机初始化 */
    ota_init();

    printf("app_gateway init done\r\n");
}

/*******************************************************************
** 函数名	: app_gateway_loop
** 函数描述	: 业务主循环：传感器采集轮询/状态LED/告警事件上报
** 参数		: 无
** 返回		: 无
********************************************************************/
void app_gateway_loop(void)
{
    static uint32_t led_tick;
    static uint32_t alarm_tick;
    static uint32_t discnt_tick;
    static uint32_t last_discnt;
    static uint8_t last_online = 0;
    uint32_t now = SYSTICK_GetMsTick();

    /* 传感器采集引擎轮询(到期自动read并写入物模型) */
    sensor_poll();

    /* 平台断链计数挂载测试: 周期检测link层断链次数变化并打印 */
    if (now - discnt_tick >= GW_DISCNT_MS) {
        uint32_t discnt = link_get()->get_discnt();

        discnt_tick = now;
        if (discnt != last_discnt) {
            last_discnt = discnt;
            printf("[link] disconnect count: %u\r\n", (unsigned int)discnt);
        }
    }

    /* 状态指示: 在线绿灯, 离线红灯 */
    if (now - led_tick >= GW_LED_MS) {
        uint8_t wifi_ready = link_get()->is_ready();
        
        led_tick = now;
        if (wifi_ready) {
            LED_GREEN;
        } else {
            LED_RED;
        }
        
        /* WiFi连接边沿触发OTA检查（MQTT已禁用） */
        if (wifi_ready && !last_online) {
            printf("[gateway] wifi ready, trigger OTA check\r\n");
            ota_trigger();
        }
        last_online = wifi_ready;
    }

    /* 越限告警事件上报(WiFi就绪时, 防抖避免风暴) */
    if (app_devmodel_alarm_pending()) {
        if (link_get()->is_ready() && now - alarm_tick >= GW_ALARM_MS) {
            alarm_tick = now;
            printf("[gateway] alarm event post\r\n");
            cloud_post_event("limit_alarm",
                             "{\"type\":\"temp_or_humi_out_of_range\"}");
        } else {
            printf("[gateway] alarm dropped (offline or debounce)\r\n");
        }
    }
}
