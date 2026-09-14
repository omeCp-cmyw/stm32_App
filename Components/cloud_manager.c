/********************************************************************************
**
** 文件名:     cloud_manager.c
** 版权所有:   无
** 文件描述:   该模块主要实现云平台连接管理（OneNET MQTT套件集成）
**
*********************************************************************************/


#include "cloud_manager.h"
#include "mqtt/mqttclient.h"
#include "../Platform/drv_sensor.h"
#include "../Platform/drv_led/bsp_led.h"
#include "../Tools/debug.h"
#include "cJSON.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>

/* MQTT上报请求队列（句柄在main.c定义，此处创建） */
extern QueueHandle_t MQTT_Data_Queue;

#define CLOUD_QUEUE_LEN         8       /* 上报队列深度 */
#define SMOKE_ALARM_MV          1800    /* 烟雾告警电压阈值(mV, 与参考工程sensor_smoke.h一致) */
#define SMOKE_CLEAR_MV          1800    /* 烟雾告警解除电压阈值(mV, 与参考工程sensor_smoke.h一致) */
#define SMOKE_DEBOUNCE_N        3       /* 连续N次满足才翻转告警状态(防毛刺) */
#define SMOKE_EVENT_DEBOUNCE_MS 60000   /* 告警事件上报最小间隔(ms, 参考工程GW_ALARM_MS) */

/* 物模型属性上报消息id(平台应答会回推大id, 此处简单自增) */
static uint32_t s_msg_id = 1;

/* MQTT线程是否已启动 */
static uint8_t mqtt_started = 0;

/* MQ2气体防抖规则状态(参考工程app_devmodel.c DevModelSmokeRule) */
static uint8_t  s_smoke_alarm = 0;      /* 当前告警状态: 1告警 0正常 */
static uint8_t  s_smoke_hi_cnt = 0;     /* 连续超告警阈值计数 */
static uint8_t  s_smoke_lo_cnt = 0;     /* 连续低于解除阈值计数 */
static uint32_t s_alarm_event_tick = 0; /* 上次告警事件上报时刻(防风暴) */

/*******************************************************************************
** 函数名称    cloud_add_prop_number
** 函数说明    数值属性追加到params(物模型格式: "key":{"value":v})
** 输入参数    params: params对象
**             key: 属性名(与平台物模型一致)
**             v: 数值
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void cloud_add_prop_number(cJSON *params, const char *key, double v)
{
    cJSON *node = cJSON_AddObjectToObject(params, key);

    if (node != NULL) {
        cJSON_AddNumberToObject(node, "value", v);
    }
}

/*******************************************************************************
** 函数名称    cloud_add_prop_bool
** 函数说明    布尔属性追加到params(物模型格式: "key":{"value":b})
** 输入参数    params: params对象
**             key: 属性名(与平台物模型一致)
**             b: 布尔值
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void cloud_add_prop_bool(cJSON *params, const char *key, int b)
{
    cJSON *node = cJSON_AddObjectToObject(params, key);

    if (node != NULL) {
        cJSON_AddBoolToObject(node, "value", (cJSON_bool)b);
    }
}

/*******************************************************************************
** 函数名称    cloud_smoke_alarm_post
** 函数说明    告警状态翻转时立即上报smoke_alarm属性(参考工程cloud_set_property模式)
** 输入参数    alarm: 1告警 0解除
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void cloud_smoke_alarm_post(int alarm)
{
    cJSON *root;
    cJSON *params;
    char id_str[12];
    char *text;
    mqtt_report_t *report;
    int text_len;

    root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    snprintf(id_str, sizeof(id_str), "%u", (unsigned)s_msg_id++);
    cJSON_AddStringToObject(root, "id", id_str);
    cJSON_AddStringToObject(root, "version", "1.0");
    params = cJSON_AddObjectToObject(root, "params");
    if (params != NULL) {
        cloud_add_prop_bool(params, "smoke_alarm", alarm);
    }

    text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return;
    }
    text_len = (int)strlen(text);
    if (text_len >= MQTT_REPORT_JSON_MAX) {
        cJSON_free(text);
        return;
    }

    report = (mqtt_report_t *)pvPortMalloc(sizeof(mqtt_report_t));
    if (report == NULL) {
        cJSON_free(text);
        return;
    }
    report->json_len = (uint16_t)text_len;
    memcpy(report->json, text, text_len + 1);
    cJSON_free(text);

    if (xQueueSend(MQTT_Data_Queue, &report, 0) != pdTRUE) {
        DEBUG_WARN("[CLOUD_MANAGER] Alarm report queue full, drop");
        vPortFree(report);
    }
}

/*******************************************************************************
** 函数名称    cloud_smoke_rule_update
** 函数说明    MQ2气体防抖规则: 电压超告警阈值连续N次置告警,
**             低于解除阈值连续N次解除(参照参考工程DevModelSmokeRule)
** 输入参数    smoke_mv: MQ2气体ADC电压(mV)
** 输出参数    无
** 返回参数    无
*******************************************************************************/
void cloud_smoke_rule_update(uint16_t smoke_mv)
{
    if (!s_smoke_alarm) {
        /* 未告警: 超阈值连续N次才置告警(防单次毛刺) */
        if (smoke_mv >= SMOKE_ALARM_MV) {
            if (++s_smoke_hi_cnt >= SMOKE_DEBOUNCE_N) {
                s_smoke_hi_cnt = 0;
                s_smoke_alarm = 1;
                LED3_ON;    /* 本地告警指示(参考工程BEEP, 此处复用空闲蓝灯) */
                cloud_smoke_alarm_post(1);  /* 翻转立即上报属性 */
                DEBUG_INFO("smoke alarm on, %d mV", (int)smoke_mv);
                /* 越限告警事件(限频防风暴, 参考工程GW_ALARM_MS) */
                if (xTaskGetTickCount() - s_alarm_event_tick >= SMOKE_EVENT_DEBOUNCE_MS) {
                    s_alarm_event_tick = xTaskGetTickCount();
                    (void)cloud_manager_post_event("limit_alarm",
                            "{\"type\":\"smoke_out_of_range\"}");
                }
            }
        } else {
            s_smoke_hi_cnt = 0;
        }
    } else {
        /* 已告警: 低于解除阈值连续N次才解除(迟滞) */
        if (smoke_mv < SMOKE_CLEAR_MV) {
            if (++s_smoke_lo_cnt >= SMOKE_DEBOUNCE_N) {
                s_smoke_lo_cnt = 0;
                s_smoke_alarm = 0;
                LED3_OFF;
                cloud_smoke_alarm_post(0);
                DEBUG_INFO("smoke alarm off, %d mV", (int)smoke_mv);
            }
        } else {
            s_smoke_lo_cnt = 0;
        }
    }
}

/*******************************************************************************
** 函数名称    cloud_manager_init
** 函数说明    初始化云平台管理器（cJSON内存钩子 + 上报队列创建）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int cloud_manager_init(void)
{
    cJSON_Hooks hooks;

    /* cJSON内存走FreeRTOS堆（heap_4共32KB，启动堆仅512B不够），
     * 与cJSON_Process.c中vPortFree配对 */
    hooks.malloc_fn = pvPortMalloc;
    hooks.free_fn = vPortFree;
    cJSON_InitHooks(&hooks);

    /* 创建上报请求队列（元素为mqtt_report_t指针，mqtt_send线程消费） */
    MQTT_Data_Queue = xQueueCreate(CLOUD_QUEUE_LEN, sizeof(mqtt_report_t *));
    if (MQTT_Data_Queue == NULL) {
        DEBUG_ERROR("[CLOUD_MANAGER] Report queue create failed");
        return -1;
    }

    mqtt_started = 0;

    DEBUG_INFO("[CLOUD_MANAGER] Cloud manager initialized");
    return 0;
}

/*******************************************************************************
** 函数名称    cloud_manager_deinit
** 函数说明    反初始化云平台管理器（删除上报队列）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int cloud_manager_deinit(void)
{
    mqtt_started = 0;

    if (MQTT_Data_Queue != NULL) {
        vQueueDelete(MQTT_Data_Queue);
        MQTT_Data_Queue = NULL;
    }

    DEBUG_INFO("[CLOUD_MANAGER] Cloud manager deinitialized");
    return 0;
}

/*******************************************************************************
** 函数名称    cloud_manager_connect
** 函数说明    连接云平台（启动MQTT收发线程，连接/重连由线程内部自理）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int cloud_manager_connect(void)
{
    if (mqtt_started) {
        return 0;
    }

    /* 启动mqtt_thread(收包/心跳)与mqtt_send(上报)线程，
     * 内部循环重试直至登录成功 */
    mqtt_thread_init();
    mqtt_started = 1;

    DEBUG_INFO("[CLOUD_MANAGER] MQTT threads started, connecting...");
    return 0;
}

/*******************************************************************************
** 函数名称    cloud_manager_disconnect
** 函数说明    断开云平台（MQTT线程自动重连架构下仅复位状态标记）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int cloud_manager_disconnect(void)
{
    /* 注：mqtt线程为自动重连架构，物理断链由线程检测网络异常后自行处理 */

    DEBUG_INFO("[CLOUD_MANAGER] Disconnected from cloud");
    return 0;
}

/*******************************************************************************
** 函数名称    cloud_manager_send_message
** 函数说明    发送消息到云平台（传感器数据组JSON入队，由mqtt_send发布$dp）
** 输入参数    msg: 消息指针（SENSOR_DATA类型payload为SensorData_t*）
** 输出参数    无
** 返回参数    0: 成功, -1: 失败
*******************************************************************************/
int cloud_manager_send_message(CloudMessage_t *msg)
{
    cJSON *root = NULL;
    char *text = NULL;
    mqtt_report_t *report = NULL;
    int text_len;

    if (msg == NULL || msg->payload == NULL) {
        return -1;
    }

    if (MQTT_Data_Queue == NULL) {
        return -1;
    }

    switch (msg->type) {
    case MSG_TYPE_SENSOR_DATA: {
        SensorData_t *sdata = (SensorData_t *)msg->payload;
        cJSON *params;
        char id_str[12];

        /* 组装物模型属性上报JSON:
         * {"id":"1","version":"1.0",
         *  "params":{"temp_value":{"value":25.5},...}} */
        root = cJSON_CreateObject();
        if (root == NULL) {
            return -1;
        }
        snprintf(id_str, sizeof(id_str), "%u", (unsigned)s_msg_id++);
        cJSON_AddStringToObject(root, "id", id_str);
        cJSON_AddStringToObject(root, "version", "1.0");
        params = cJSON_AddObjectToObject(root, "params");
        if (params == NULL) {
            cJSON_Delete(root);
            return -1;
        }

        /* 属性与平台物模型一致: temp_value/humidity_value/
         * smoke_value/smoke_alarm */
        cloud_add_prop_number(params, "temp_value", sdata->temperature);
        cloud_add_prop_number(params, "humidity_value", sdata->humidity);
        cloud_add_prop_number(params, "smoke_value", sdata->smoke_value);
        /* 告警状态取自防抖规则(1s快采持续更新, 非单次比较) */
        cloud_add_prop_bool(params, "smoke_alarm", s_smoke_alarm);
        break;
    }

    case MSG_TYPE_DEVICE_STATUS:
    case MSG_TYPE_OTA_COMMAND:
    case MSG_TYPE_CONFIG_UPDATE:
        /* 预留消息类型，后续阶段实现 */
        return -1;

    default:
        return -1;
    }

    /* 序列化JSON文本 */
    text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return -1;
    }
    text_len = (int)strlen(text);
    if (text_len >= MQTT_REPORT_JSON_MAX) {
        cJSON_free(text);
        return -1;
    }

    /* 构建上报请求并入队（mqtt_send线程消费后释放） */
    report = (mqtt_report_t *)pvPortMalloc(sizeof(mqtt_report_t));
    if (report == NULL) {
        cJSON_free(text);
        return -1;
    }
    report->json_len = (uint16_t)text_len;
    memcpy(report->json, text, text_len + 1);
    cJSON_free(text);

    if (xQueueSend(MQTT_Data_Queue, &report, 0) != pdTRUE) {
        /* 队列满丢弃（上报周期远大于发送周期，正常情况下不溢出） */
        DEBUG_WARN("[CLOUD_MANAGER] Report queue full, drop");
        vPortFree(report);
        return -1;
    }

    return 0;
}

/*******************************************************************************
** 函数名称    cloud_manager_recv_message
** 函数说明    接收云平台消息（下发消息由mqtt线程UserMsgCtl直接处理，
**             此处预留主动接收接口）
** 输入参数    msg: 消息指针
**             timeout: 超时时间(ms)
** 输出参数    无
** 返回参数    0: 成功, -1: 失败, -2: 超时
*******************************************************************************/
int cloud_manager_recv_message(CloudMessage_t *msg, uint32_t timeout)
{
    (void)msg;
    (void)timeout;

    /* 平台下发消息已由mqtt线程自动处理(UserMsgCtl->Proscess) */
    return -2;
}

/*******************************************************************************
** 函数名称    cloud_manager_get_state
** 函数说明    获取云平台连接状态
** 输入参数    无
** 输出参数    无
** 返回参数    连接状态
*******************************************************************************/
CloudState_e cloud_manager_get_state(void)
{
    if (mqtt_is_connected()) {
        return CLOUD_STATE_CONNECTED;
    }
    if (mqtt_started) {
        return CLOUD_STATE_CONNECTING;
    }
    return CLOUD_STATE_DISCONNECTED;
}

/*******************************************************************************
** 函数名称    cloud_manager_set_reconnect_interval
** 函数说明    设置重连间隔（预留：当前mqtt线程固定3s重试）
** 输入参数    interval: 重连间隔(ms)
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int cloud_manager_set_reconnect_interval(uint32_t interval)
{
    /* 预留接口：当前mqtt线程固定3s重试，interval暂仅记录 */
    DEBUG_INFO("[CLOUD_MANAGER] Reconnect interval set to %d ms", (int)interval);
    return 0;
}

/*******************************************************************************
** 函数名称    cloud_manager_post_event
** 函数说明    上报物模型事件(信息型, 如LED开关事件)
** 输入参数    event_id: 事件标识符(如"led")
**             params_json: 事件参数JSON(如"{\"switch\":1}")
** 输出参数    无
** 返回参数    0: 成功, -1: 失败
*******************************************************************************/
int cloud_manager_post_event(const char *event_id, const char *params_json)
{
    if (event_id == NULL || params_json == NULL) {
        return -1;
    }

    /* 组消息体并入队, 由mqtt_send线程发布到thing/event/post */
    return mqtt_post_event(event_id, params_json);
}

/*******************************************************************************
** 函数名称    cloud_manager_desired_get
** 函数说明    获取属性期望值(发布desired/get, 应答由mqtt线程解析应用)
** 输入参数    props_json: 属性名数组JSON(如"[\"led\"]")
** 输出参数    无
** 返回参数    0: 成功, -1: 失败
*******************************************************************************/
int cloud_manager_desired_get(const char *props_json)
{
    if (props_json == NULL) {
        return -1;
    }

    /* 发布desired/get请求, 期望值由mqtt线程CloudDesiredReply解析应用 */
    return mqtt_desired_get(props_json);
}
