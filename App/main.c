/********************************************************************************
**
** 文件名:     main.c
** 版权所有:   无
** 文件描述:   该模块主要实现系统初始化和任务调度
**
*********************************************************************************/


#include "../Config/system_config.h"
#include "../Config/task_config.h"
#include "../OSAL/osal.h"
#include "../Tools/debug.h"
#include "app_config.h"

/* 驱动层头文件 */
#include "../Platform/drv_manager.h"
#include "../Platform/drv_led.h"
#include "../Platform/drv_sensor.h"
#include "../Platform/drv_camera.h"
#include "../Platform/drv_lcd.h"
#include "../Platform/drv_key.h"

/* 组件层头文件 */
#include "../Components/sensor_manager.h"
#include "../Components/cloud_manager.h"
#include "../Components/net_manager.h"
#include "../Components/ota_manager.h"

/* 原有头文件 */
#include "stm32f4xx_hal.h"
#include "../Platform/drv_uart/bsp_debug_usart.h"
#include "../Platform/drv_led/bsp_led.h"
#include "../Platform/drv_eth/bsp_eth.h"
#include "../Tools/dwt_delay/core_delay.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include <string.h>
#include <stdio.h>

/* 任务句柄 */
static osal_task_t task_led;
#if APP_ENABLE_SENSOR
static osal_task_t task_sensor;
#endif
#if APP_ENABLE_CLOUD
static osal_task_t task_cloud;
#endif
#if APP_ENABLE_CAMERA
static osal_task_t task_camera;
#endif
#if APP_ENABLE_LCD
static osal_task_t task_lcd;
#endif
#if APP_ENABLE_NTP
static osal_task_t task_ntp;
#endif
#if APP_ENABLE_OTA
static osal_task_t task_ota;
#endif
static osal_task_t task_net_init;
#if APP_ENABLE_NET_DEBUG
static osal_task_t task_net_debug;
#endif
#if APP_ENABLE_MONITOR
static osal_task_t task_monitor;
#endif

/* 全局变量 */
QueueHandle_t MQTT_Data_Queue = NULL;

/* 外部函数声明 */
extern void TCPIP_Init(void);
extern void BSP_Init(void);
extern void vTaskStartScheduler(void);
extern void vTaskDelete(void *);
extern uint32_t xTaskGetTickCount(void);
extern uint32_t xPortGetFreeHeapSize(void);

/* 任务函数声明 */
static void LED_Task(void *pvParameters);
#if APP_ENABLE_SENSOR
static void Sensor_Task(void *pvParameters);
#endif
#if APP_ENABLE_CLOUD
static void Cloud_Task(void *pvParameters);
#endif
#if APP_ENABLE_CAMERA
static void Camera_Task(void *pvParameters);
#endif
#if APP_ENABLE_LCD
static void LCD_Task(void *pvParameters);
#endif
#if APP_ENABLE_NTP
static void NTP_Task(void *pvParameters);
#endif
#if APP_ENABLE_OTA
static void OTA_Task(void *pvParameters);
#endif
#if APP_ENABLE_MONITOR
static void Monitor_Task(void *pvParameters);
#endif
#if APP_ENABLE_NET_DEBUG
static void NetDebug_Task(void *pvParameters);
#endif
static void Net_Init_Task(void *pvParameters);

/* configTICK_RATE_HZ定义 */
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ          1000
#endif

/*******************************************************************************
** 函数名称    LED_Task
** 函数说明    LED闪烁任务
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void LED_Task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        LED1_TOGGLE;
        osal_task_delay(APP_LED_TOGGLE_PERIOD_MS);
    }
}

#if APP_ENABLE_NET_DEBUG
/*******************************************************************************
** 函数名称    NetDebug_Task
** 函数说明    网络调试任务，与上位机通信
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void NetDebug_Task(void *pvParameters)
{
    uint8_t recv_buffer[256];
    int ret;
    uint32_t heartbeat_cnt = 0;
    const char *welcome = APP_NET_DEBUG_WELCOME;
    const char *heartbeat = APP_NET_DEBUG_HEARTBEAT;

    (void)pvParameters;

    /* 等待网络就绪 */
    DEBUG_INFO("Waiting for network...");
    osal_task_delay(3000);

    /* 初始化网络调试 */
    while (debug_net_init() != 0) {
        DEBUG_WARN("Retry connect to debug server in 2s...");
        osal_task_delay(APP_NET_DEBUG_RECONNECT_MS);
    }

    /* 发送欢迎消息 */
    debug_net_send((const uint8_t *)welcome, strlen(welcome));
    DEBUG_INFO("NetDebug task started");

    while (1) {
        /* 接收上位机数据 */
        ret = debug_net_recv(recv_buffer, sizeof(recv_buffer), APP_NET_DEBUG_RECV_TIMEOUT);
        if (ret > 0) {
            /* 回显数据 */
            debug_net_send(recv_buffer, (uint32_t)ret);

            /* 打印实际数据内容 */
            if (ret < (int)sizeof(recv_buffer)) {
                recv_buffer[ret] = '\0';
            } else {
                recv_buffer[sizeof(recv_buffer) - 1] = '\0';
            }
            DEBUG_INFO("Recv from PC: %s", (char *)recv_buffer);
        } else if (ret < 0) {
            /* 连接断开，重新连接 */
            DEBUG_WARN("Connection lost, reconnecting...");
            debug_net_close();
            osal_task_delay(APP_NET_DEBUG_RECONNECT_MS);

            while (debug_net_init() != 0) {
                DEBUG_WARN("Retry connect to debug server in 2s...");
                osal_task_delay(APP_NET_DEBUG_RECONNECT_MS);
            }

            debug_net_send((const uint8_t *)welcome, strlen(welcome));
        }

        /* 发送心跳（默认每10秒） */
        heartbeat_cnt++;
        if (heartbeat_cnt >= (APP_NET_DEBUG_HEARTBEAT_MS / APP_NET_DEBUG_RECV_TIMEOUT)) {
            heartbeat_cnt = 0;
            if (debug_net_is_connected()) {
                debug_net_send((const uint8_t *)heartbeat, strlen(heartbeat));
            }
        }

        osal_task_delay(100);
    }
}
#endif /* APP_ENABLE_NET_DEBUG */

#if APP_ENABLE_SENSOR
/*******************************************************************************
** 函数名称    Sensor_Task
** 函数说明    传感器采集任务（真实采集并上报云平台）
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void Sensor_Task(void *pvParameters)
{
    SensorData_t sensor_data;
    CloudMessage_t cloud_msg;
    uint32_t sub_cnt;

    (void)pvParameters;

    /* 启动采集 */
    sensor_manager_start_collect();

    while (1) {
        /* MQ2气体按参考工程1s周期快速采样并做防抖判断(规则在cloud层),
         * 主周期(10s)到点后整体采集上报 */
        for (sub_cnt = 0; sub_cnt < APP_SENSOR_COLLECT_PERIOD_MS / 1000U; sub_cnt++) {
            SensorData_t smoke_data;

            if (drv_sensor_read(SENSOR_TYPE_MQ2, &smoke_data) == 0) {
                cloud_smoke_rule_update((uint16_t)smoke_data.smoke_value);
            }
            osal_task_delay(1000);
        }

        /* 采集传感器数据 */
        if (sensor_manager_get_data(&sensor_data) == 0) {
            DEBUG_INFO("Sensor Data: Temp=%.1f, Hum=%.1f, Light=%.1flux, MQ2=%.1fmV",
                       sensor_data.temperature, sensor_data.humidity,
                       sensor_data.light_value, sensor_data.smoke_value);

            /* 上报云平台 */
            cloud_msg.type = MSG_TYPE_SENSOR_DATA;
            cloud_msg.payload = (uint8_t *)&sensor_data;
            cloud_msg.payload_len = sizeof(sensor_data);
            cloud_msg.timestamp = sensor_data.timestamp;
            if (cloud_manager_send_message(&cloud_msg) != 0) {
                DEBUG_WARN("Sensor data report failed");
            }
        } else {
            DEBUG_WARN("Sensor collect failed");
        }

        osal_task_delay(APP_SENSOR_COLLECT_PERIOD_MS);
    }
}
#endif

#if APP_ENABLE_CLOUD
/*******************************************************************************
** 函数名称    Cloud_Task
** 函数说明    云平台通信任务（启动MQTT线程并监控连接状态）
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void Cloud_Task(void *pvParameters)
{
    CloudState_e last_state = CLOUD_STATE_DISCONNECTED;
    extern struct netif gnetif;
    uint32_t wait_cnt = 0;

    (void)pvParameters;

    /* 等待网络就绪（Net_Init_Task完成LwIP初始化且DHCP分配IP），
     * 固定延时不可靠: DHCP耗时不定, 未就绪时DNS解析会拿到0.0.0.0 */
    DEBUG_INFO("Waiting for network...");
    while (!(netif_is_up(&gnetif) && !ip_addr_isany(&gnetif.ip_addr))) {
        osal_task_delay(500);
        if (++wait_cnt >= 60) {  /* 最长等30s, 之后交给MQTT线程DNS重试兜底 */
            DEBUG_WARN("Network not ready in 30s, start MQTT anyway");
            break;
        }
    }

    /* 启动MQTT收发线程（连接/重连由线程内部自理） */
    if (cloud_manager_connect() == 0) {
        DEBUG_INFO("Cloud manager started");
    } else {
        DEBUG_ERROR("Cloud manager start failed");
    }

    while (1) {
        /* 状态变化时打印 */
        CloudState_e state = cloud_manager_get_state();
        if (state != last_state) {
            last_state = state;
            if (state == CLOUD_STATE_CONNECTED) {
                DEBUG_INFO("Cloud connected (OneNET MQTT)");
            } else {
                DEBUG_WARN("Cloud state: %d", state);
            }
        }

        osal_task_delay(APP_CLOUD_STATUS_PERIOD_MS);
    }
}
#endif

#if APP_ENABLE_CAMERA
/*******************************************************************************
** 函数名称    Camera_Task
** 函数说明    摄像头采集任务
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void Camera_Task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        /* 摄像头采集处理 */
        osal_task_delay(1000);
    }
}
#endif

#if APP_ENABLE_LCD
/*******************************************************************************
** 函数名称    LCD_Task
** 函数说明    LCD显示任务
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void LCD_Task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        /* LCD显示处理 */
        osal_task_delay(100);
    }
}
#endif

#if APP_ENABLE_NTP
/*******************************************************************************
** 函数名称    NTP_Task
** 函数说明    NTP时间同步任务
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void NTP_Task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        /* NTP时间同步处理 */
        osal_task_delay(60000);  /* 每分钟同步一次 */
    }
}
#endif

#if APP_ENABLE_OTA
/*******************************************************************************
** 函数名称    OTA_Task
** 函数说明    OTA升级任务
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void OTA_Task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        /* OTA升级处理 */
        osal_task_delay(10000);
    }
}
#endif

#if APP_ENABLE_MONITOR
/*******************************************************************************
** 函数名称    Monitor_Task
** 函数说明    系统监控任务
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void Monitor_Task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        /* 打印系统状态 */
        DEBUG_INFO("========== System Status ==========");
        DEBUG_INFO("Heap free: %d bytes", xPortGetFreeHeapSize());
        DEBUG_INFO("Uptime: %d seconds", xTaskGetTickCount() / configTICK_RATE_HZ);
        DEBUG_INFO("===================================");

        osal_task_delay(APP_MONITOR_PERIOD_MS);  /* 默认每10秒打印一次 */
    }
}
#endif /* APP_ENABLE_MONITOR */

/*******************************************************************************
** 函数名称    Net_Init_Task
** 函数说明    网络初始化任务
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void Net_Init_Task(void *pvParameters)
{
    (void)pvParameters;

    /* 初始化LWIP网络栈 */
    DEBUG_INFO("Initializing LWIP...");
    TCPIP_Init();
    DEBUG_INFO("LWIP initialized");

    /* 删除自身任务 */
    vTaskDelete(NULL);
}

/*******************************************************************************
** 函数名称    main
** 函数说明    主函数
** 输入参数    无
** 输出参数    无
** 返回参数    无
*******************************************************************************/
int main(void)
{
    /* 硬件初始化 */
    BSP_Init();
    
    /* 使能DWT CYCCNT计数器（DHT11等驱动依赖Delay_us/Delay_ms精确延时，
     * 未初始化时CPU_TS_TmrRd恒返0会导致延时函数死循环） */
    CPU_TS_TmrInit();
    
    DEBUG_INFO("STM32 IoT Terminal V%s starting...", SOFTWARE_VERSION);

    /* 初始化驱动管理器 */
    drv_manager_init();
    
    /* 初始化各驱动模块（drv_sensor_init由sensor_manager_init内部调用） */
    drv_led_init();
    drv_camera_init();
    drv_lcd_init();
    drv_key_init();
    
    /* 初始化组件管理器 */
    sensor_manager_init();
    cloud_manager_init();
    net_manager_init();
    ota_manager_init();

    /* 创建网络初始化任务 */
    osal_task_create(&task_net_init, "Net_Init_Task", Net_Init_Task, NULL,
                     TASK_STACK_SIZE_LARGE, TASK_PRIO_HIGH);

    /* 第一阶段任务：LED闪烁 + 网络调试 + 系统监控 */
#if APP_ENABLE_LED
    osal_task_create(&task_led, "LED_Task", LED_Task, NULL,
                     TASK_STACK_SIZE_SMALL, TASK_PRIO_LOW);
#endif

    /* 网络调试任务（依赖网络初始化任务） */
#if APP_ENABLE_NET_DEBUG
    osal_task_create(&task_net_debug, "NetDebug_Task", NetDebug_Task, NULL,
                     TASK_STACK_SIZE_MEDIUM, TASK_PRIO_NORMAL);
#endif

#if APP_ENABLE_MONITOR
    osal_task_create(&task_monitor, "Monitor_Task", Monitor_Task, NULL,
                     TASK_STACK_SIZE_SMALL, TASK_PRIO_LOW);
#endif

    /* 第二阶段任务（组件层实现后启用） */
#if APP_ENABLE_SENSOR
    osal_task_create(&task_sensor, "Sensor_Task", Sensor_Task, NULL,
                     TASK_STACK_SIZE_LARGE, TASK_PRIO_NORMAL);
#endif
#if APP_ENABLE_CLOUD
    osal_task_create(&task_cloud, "Cloud_Task", Cloud_Task, NULL,
                     TASK_STACK_SIZE_LARGE, TASK_PRIO_NORMAL);
#endif
#if APP_ENABLE_CAMERA
    osal_task_create(&task_camera, "Camera_Task", Camera_Task, NULL,
                     TASK_STACK_SIZE_LARGE, TASK_PRIO_LOW);
#endif
#if APP_ENABLE_LCD
    osal_task_create(&task_lcd, "LCD_Task", LCD_Task, NULL,
                     TASK_STACK_SIZE_MEDIUM, TASK_PRIO_LOW);
#endif
#if APP_ENABLE_NTP
    osal_task_create(&task_ntp, "NTP_Task", NTP_Task, NULL,
                     TASK_STACK_SIZE_MEDIUM, TASK_PRIO_LOW);
#endif
#if APP_ENABLE_OTA
    osal_task_create(&task_ota, "OTA_Task", OTA_Task, NULL,
                     TASK_STACK_SIZE_LARGE, TASK_PRIO_LOW);
#endif

    DEBUG_INFO("All tasks created, starting scheduler...");

    /* 启动调度器 */
    vTaskStartScheduler();

    /* 不应该执行到这里 */
    while (1) {
    }
}
