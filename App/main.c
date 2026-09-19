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
#include "../Platform/drv_rtc.h"

/* 组件层头文件 */
#include "../Components/sensor_manager.h"
#include "../Components/cloud_manager.h"
#include "../Components/net_manager.h"
#include "../Components/ota_manager.h"
#if APP_ENABLE_NTP
#include "../Components/ntp/ntp_mmi.h"
#endif
#if APP_ENABLE_OTA
#include "../Components/ota/ota_mmi.h"
#include "../Components/mqtt/mqttclient.h"
#endif

/* Ymodem本地升级（UART3） */
#include "../Platform/drv_uart/bsp_upgrade_usart.h"
#include "../FwUpgrade/fw_upgrade.h"
#include "../FwUpgrade/ymodem/ymodem.h"

/* 原有头文件 */
#include "stm32f4xx_hal.h"
#include "../Platform/drv_uart/bsp_debug_usart.h"
#include "../Platform/drv_led/bsp_led.h"
#include "../Platform/drv_eth/bsp_eth.h"
#include "../Tools/dwt_delay/core_delay.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/dns.h"
#if APP_ENABLE_NTP
#include "lwip/sockets.h"
#endif
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include <string.h>
#include <stdio.h>

/* FreeRTOS堆放入CCM RAM(0x10000000, 64KB)，heap_4.c应用侧定义 */
uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((section("CCM_RAM"), zero_init));

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
/* NTP收发缓冲 */
static uint8_t s_ntp_req[NTP_MMI_PACKET_SIZE];
static uint8_t s_ntp_rsp[64];
#endif
#if APP_ENABLE_OTA
static osal_task_t task_ota;
#endif
#if APP_ENABLE_YMODEM
static osal_task_t task_ymodem;
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
#if APP_ENABLE_CLOUD && APP_ENABLE_NTP
static osal_sem_t s_ntp_sync_sem;   /* NTP同步完成信号量 */
#endif

/* 外部函数声明 */
extern void TCPIP_Init(void);
extern volatile uint8_t g_lwip_ready;   /* sys_arch.c，DHCP+DNS预热完成后置1 */
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

    /* 等网络就绪：DHCP耗时不定，未就绪时DNS会解析出0.0.0.0 */
    DEBUG_INFO("Waiting for network...");
    while (!(netif_is_up(&gnetif) && !ip_addr_isany(&gnetif.ip_addr))) {
        osal_task_delay(500);
        if (++wait_cnt >= 60) {  /* 最长等30s，后面交给MQTT线程DNS重试 */
            DEBUG_WARN("Network not ready in 30s, start MQTT anyway");
            break;
        }
    }

#if APP_ENABLE_NTP
    /* 先等NTP同步再连MQTT */
    if (s_ntp_sync_sem != NULL) {
        DEBUG_INFO("Waiting for NTP time sync...");
        if (osal_sem_wait(s_ntp_sync_sem, APP_CLOUD_NTP_WAIT_MS) != OSAL_OK) {
            DEBUG_WARN("Wait NTP timeout, connect MQTT anyway");
        }
    }
#endif

    /* 启动MQTT收发线程 */
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
    SensorData_t lcd_data;
    uint32_t last_timestamp = 0;
    char line[32];
    char last_time_str[32] = "";
    LCD_Region_t time_region = {0, 50, 480, 32};    /* 时间行区域 */
    LCD_Region_t data_region = {0, 72, 480, 200};   /* 传感器数据区(下移32px给时间行) */
    uint8_t lcd_mode = 0;           /* 0:正常 1:升级中 2:升级成功 */
    uint32_t last_percent = 1000;   /* 非法初值，强制首次重绘 */

    (void)pvParameters;

    /* 标题栏 + 等待提示 */
    drv_lcd_clear(BLACK);
    drv_lcd_draw_string(10, 10, "STM32 IoT Terminal", YELLOW);
    drv_lcd_draw_string(10, 82, "Waiting for sensor data...", WHITE);

    while (1) {
        uint8_t fw_state = FW_UPG_GetState();

        if (fw_state == FW_UPG_STATE_RECVING || fw_state == FW_UPG_STATE_DONE) {
            uint8_t new_mode = (fw_state == FW_UPG_STATE_DONE) ? 2 : 1;
            uint32_t fw_size = FW_UPG_GetFwSize();
            uint32_t recv = FW_UPG_GetRecvSize();
            uint32_t percent = (fw_size > 0) ? (recv * 100 / fw_size) : 0;
            LCD_Region_t bar_bg = {20, 60, 440, 24};
            LCD_Region_t bar_fg;
            LCD_Region_t txt_line = {20, 100, 320, 32};

            if (percent > 100) {
                percent = 100;
            }

            /* 模式切换（进入升级/成功画面）时整屏重绘 */
            if (lcd_mode != new_mode) {
                lcd_mode = new_mode;
                last_percent = 1000;    /* 强制进度条重绘 */
                drv_lcd_clear(BLACK);
                drv_lcd_draw_string(10, 10, "Firmware Upgrading...", YELLOW);
            }

            /* 进度变化才重绘进度条与文字 */
            if (percent != last_percent) {
                last_percent = percent;
                drv_lcd_fill_rect(&bar_bg, GREY);
                bar_fg.x = 22;
                bar_fg.y = 62;
                bar_fg.width = (uint16_t)(percent * 436 / 100);
                bar_fg.height = 20;
                drv_lcd_fill_rect(&bar_fg, GREEN);

                drv_lcd_fill_rect(&txt_line, BLACK);
                if (fw_state == FW_UPG_STATE_DONE) {
                    drv_lcd_draw_string(20, 100, "Upgrade Success!", GREEN);
                    drv_lcd_draw_string(20, 140, "Rebooting...", CYAN);
                } else if (recv >= fw_size && fw_size > 0) {
                    snprintf(line, sizeof(line), "%u%%  Verifying...", (unsigned)percent);
                    drv_lcd_draw_string(20, 100, line, WHITE);
                    drv_lcd_draw_string(20, 140, "Do not power off!", RED);
                } else {
                    snprintf(line, sizeof(line), "%u%%", (unsigned)percent);
                    drv_lcd_draw_string(20, 100, line, WHITE);
                    drv_lcd_draw_string(20, 140, "Do not power off!", RED);
                }
            }
        } else if (lcd_mode != 0) {
            /* 升级取消/失败回到正常显示 */
            lcd_mode = 0;
            last_timestamp = 0;     /* 强制重绘传感器数据区 */
            last_time_str[0] = '\0'; /* 强制重绘时间行 */
            drv_lcd_clear(BLACK);
            drv_lcd_draw_string(10, 10, "STM32 IoT Terminal", YELLOW);
            drv_lcd_draw_string(10, 82, "Waiting for sensor data...", WHITE);
        } else {
            /* 时间行：读RTC，变化才重绘（RTC无效时显示占位符） */
            char time_str[24];
            if (drv_rtc_get_str(time_str, sizeof(time_str)) == 0) {
                snprintf(line, sizeof(line), "Time: %s", time_str);
            } else {
                strcpy(line, "Time: ----");
            }
            if (strcmp(line, last_time_str) != 0) {
                strcpy(last_time_str, line);
                drv_lcd_fill_rect(&time_region, BLACK);
                drv_lcd_draw_string(10, 50, line, CYAN);
            }

            /* 每秒轮询传感器缓存，数据有更新（时间戳变化）才重绘 */
            if (sensor_manager_get_latest_data(&lcd_data) == 0 &&
                lcd_data.timestamp != last_timestamp) {
                last_timestamp = lcd_data.timestamp;

                /* 只清数据区重绘，避免整屏刷新闪烁 */
                drv_lcd_fill_rect(&data_region, BLACK);

                if (lcd_data.is_valid) {
                    snprintf(line, sizeof(line), "Temp : %5.1f C", lcd_data.temperature);
                    drv_lcd_draw_string(10, 82, line, WHITE);
                    snprintf(line, sizeof(line), "Hum  : %5.1f %%", lcd_data.humidity);
                    drv_lcd_draw_string(10, 114, line, WHITE);
                    snprintf(line, sizeof(line), "Light: %5.1f lux", lcd_data.light_value);
                    drv_lcd_draw_string(10, 146, line, WHITE);
                    snprintf(line, sizeof(line), "Smoke: %5.1f mV", lcd_data.smoke_value);
                    drv_lcd_draw_string(10, 178, line, WHITE);
                } else {
                    drv_lcd_draw_string(10, 82, "Sensor data invalid", RED);
                }
            }
        }

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

    /* 等待网络就绪：网卡已注册、链路up、IP已分配、DNS预热完成 */
    while (netif_default == NULL || !netif_is_up(netif_default) ||
           ip4_addr_isany_val(*netif_ip4_addr(netif_default)) ||
           !g_lwip_ready) {
        osal_task_delay(1000);
    }
    DEBUG_INFO("NTP: network ready, start time sync");

    while (1) {
        struct sockaddr_in srv;
        ip_addr_t srv_ip;
        uint32_t unix_ts;
        int fd;
        int ret;

        /* 解析NTP服务器域名 */
        if (dns_gethostbyname(APP_NTP_SERVER, &srv_ip, NULL, NULL) != ERR_OK) {
            DEBUG_INFO("NTP: dns resolve %s fail, retry in %d s",
                       APP_NTP_SERVER, APP_NTP_RETRY_MS / 1000);
            osal_task_delay(APP_NTP_RETRY_MS);
            continue;
        }

        /* 创建UDP套接字 */
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            DEBUG_INFO("NTP: socket create fail, retry in %d s",
                       APP_NTP_RETRY_MS / 1000);
            osal_task_delay(APP_NTP_RETRY_MS);
            continue;
        }

        /* 发送NTP请求 */
        memset(&srv, 0, sizeof(srv));
        srv.sin_family = AF_INET;
        srv.sin_port = htons(APP_NTP_PORT);
        srv.sin_addr.s_addr = srv_ip.addr;

        NTP_MMI_BuildRequest(s_ntp_req);
        ret = sendto(fd, s_ntp_req, NTP_MMI_PACKET_SIZE, 0,
                     (struct sockaddr *)&srv, sizeof(srv));
        if (ret < 0) {
            DEBUG_INFO("NTP: sendto fail");
            close(fd);
            osal_task_delay(APP_NTP_RETRY_MS);
            continue;
        }

        /* 非阻塞轮询接收应答，超时重试 */
        ret = -1;
        {
            uint32_t elapsed = 0;

            while (elapsed < APP_NTP_RECV_TIMEOUT_MS) {
                int rlen = recv(fd, s_ntp_rsp, sizeof(s_ntp_rsp), MSG_DONTWAIT);
                if (rlen > 0) {
                    if (NTP_MMI_ParseReply(s_ntp_rsp, rlen, &unix_ts) == 0) {
                        NTP_MMI_SetTime(unix_ts);
                        /* 北京时间(UTC+8)写入RTC */
                        drv_rtc_set_unix(unix_ts + 8 * 3600);
                        ret = 0;
                    } else {
                        DEBUG_INFO("NTP: invalid reply");
                    }
                    break;
                }
                osal_task_delay(100);
                elapsed += 100;
            }
        }
        close(fd);

        if (ret == 0) {
            /* 同步成功：唤醒Cloud任务，删除本任务释放链路与任务栈 */
#if APP_ENABLE_CLOUD
            if (s_ntp_sync_sem != NULL) {
                osal_sem_post(s_ntp_sync_sem);
            }
#endif
            DEBUG_INFO("NTP: sync success, socket released, task exit");
            osal_task_delete(task_ntp);
        } else {
            DEBUG_INFO("NTP: reply timeout, retry in %d s",
                       APP_NTP_RETRY_MS / 1000);
            osal_task_delay(APP_NTP_RETRY_MS);
        }
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
    uint8_t last_conn = 0;

    (void)pvParameters;

    while (1) {
        uint8_t conn = mqtt_is_connected();

        /* 上线触发一轮OTA */
        if (conn && !last_conn) {
            DEBUG_INFO("[OTA] cloud online, start upgrade check");
            ota_task_proc();
        }
        last_conn = conn;
        osal_task_delay(1000);
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

#if APP_ENABLE_NTP || APP_ENABLE_CLOUD || APP_ENABLE_OTA
    /* DNS预热：把三个域名首查消耗在开机阶段，避开路由器代理丢包 */
    {
        static const char *hosts[] = {
            APP_NTP_SERVER,
            "mqtts.heclouds.com",   /* 与mqttclient.h的HOST_NAME一致 */
            APP_OTA_HOST
        };
        ip_addr_t dns_ip;
        int h, try;

        for (h = 0; h < 3; h++) {
            for (try = 0; try < 3; try++) {
                if (dns_gethostbyname(hosts[h], &dns_ip, NULL, NULL) == ERR_OK &&
                    !ip_addr_isany(&dns_ip)) {
                    break;
                }
                osal_task_delay(1000);
            }
        }
    }
#endif
    g_lwip_ready = 1;

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
    
    /* 初始化各驱动模块 */
    drv_led_init();
    drv_camera_init();
    drv_lcd_init();
    drv_key_init();
    drv_rtc_init();
    
    /* 初始化组件管理器 */
    sensor_manager_init();
    cloud_manager_init();
    net_manager_init();
    ota_manager_init();

#if APP_ENABLE_YMODEM
    /* 初始化Ymodem本地升级通道：UART3 + 升级主控 + 打开升级窗口 */
    UPGRADE_USART_Config();
    FW_UPG_Init();
    FW_UPG_YM_Init();
#endif

#if APP_ENABLE_CLOUD && APP_ENABLE_NTP
    /* 创建NTP同步完成信号量（二进制，初值为空），Cloud任务连接前等待 */
    s_ntp_sync_sem = osal_sem_create();
#endif

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
                     TASK_STACK_SIZE_LARGE, TASK_PRIO_LOW);
#endif
#if APP_ENABLE_NTP
    osal_task_create(&task_ntp, "NTP_Task", NTP_Task, NULL,
                     TASK_STACK_SIZE_MEDIUM, TASK_PRIO_LOW);
#endif
#if APP_ENABLE_OTA
    osal_task_create(&task_ota, "OTA_Task", OTA_Task, NULL,
                     TASK_STACK_SIZE_LARGE, TASK_PRIO_LOW);
#endif
#if APP_ENABLE_YMODEM
    osal_task_create(&task_ymodem, "Ymodem_Task", FW_UPG_YM_Task, NULL,
                     TASK_STACK_SIZE_LARGE, TASK_PRIO_HIGHEST);
#endif

    DEBUG_INFO("All tasks created, starting scheduler...");

    /* 启动调度器 */
    vTaskStartScheduler();

    /* 不应该执行到这里 */
    while (1) {
    }
}
