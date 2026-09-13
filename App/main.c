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
static osal_task_t task_net_debug;
static osal_task_t task_monitor;

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
static void Monitor_Task(void *pvParameters);
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

#if APP_ENABLE_SENSOR
/*******************************************************************************
** 函数名称    Sensor_Task
** 函数说明    传感器采集任务
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void Sensor_Task(void *pvParameters)
{
    (void)pvParameters;
    SensorData_t sensor_data;

    while (1) {
        /* 采集传感器数据 */
        if (sensor_manager_get_data(&sensor_data) == 0) {
            DEBUG_INFO("Sensor Data: Temp=%.1f, Hum=%.1f, Light=%.1f, Smoke=%.1f",
                       sensor_data.temperature, sensor_data.humidity,
                       sensor_data.light_value, sensor_data.smoke_value);
        }

        osal_task_delay(5000);  /* 每5秒采集一次 */
    }
}
#endif

#if APP_ENABLE_CLOUD
/*******************************************************************************
** 函数名称    Cloud_Task
** 函数说明    云平台通信任务
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void Cloud_Task(void *pvParameters)
{
    (void)pvParameters;

    /* 等待网络就绪 */
    osal_task_delay(5000);

    /* 连接云平台 */
    if (cloud_manager_connect() == 0) {
        DEBUG_INFO("Cloud connected");
    } else {
        DEBUG_WARN("Cloud connection failed");
    }

    while (1) {
        /* 云平台通信处理 */
        osal_task_delay(1000);
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
    
    DEBUG_INFO("========================================");
    DEBUG_INFO("STM32 IoT Terminal Starting...");
    DEBUG_INFO("Software Version: %s", SOFTWARE_VERSION);
    DEBUG_INFO("Hardware Version: %s", HARDWARE_VERSION);
    DEBUG_INFO("Target IP: %d.%d.%d.%d",
               LOCAL_IP_ADDR0, LOCAL_IP_ADDR1,
               LOCAL_IP_ADDR2, LOCAL_IP_ADDR3);
    DEBUG_INFO("Debug Server: %d.%d.%d.%d:%d",
               DEBUG_SERVER_IP0, DEBUG_SERVER_IP1,
               DEBUG_SERVER_IP2, DEBUG_SERVER_IP3,
               DEBUG_SERVER_PORT);
    DEBUG_INFO("========================================");

    /* 初始化驱动管理器 */
    drv_manager_init();
    
    /* 初始化各驱动模块 */
    drv_led_init();
    drv_sensor_init();
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
                     TASK_STACK_SIZE_MEDIUM, TASK_PRIO_NORMAL);
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
