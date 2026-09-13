# STM32 IoT采集终端系统设计计划

## 0. Bootloader与APP分区设计

### 0.1 Flash分区规划
基于STM32F407ZGT6的1MB Flash，设计如下分区：

| 分区名称 | 起始地址 | 结束地址 | 大小 | 用途 |
|---------|----------|----------|------|------|
| Bootloader | 0x08000000 | 0x0801FFFF | 128KB | 引导程序 |
| APP | 0x08020000 | 0x0807FFFF | 384KB | 应用程序 |
| OTA备份 | 0x08080000 | 0x080BFFFF | 256KB | OTA升级备份区 |
| 配置存储 | 0x080C0000 | 0x080FFFFF | 256KB | 系统配置存储 |

### 0.2 Bootloader接口设计
```c
// bootloader_interface.h - Bootloader与APP接口定义
#ifndef __BOOTLOADER_INTERFACE_H
#define __BOOTLOADER_INTERFACE_H

// 升级标志定义
#define UPGRADE_FLAG_NONE       0x00000000  // 无升级
#define UPGRADE_FLAG_OTA        0x55AA55AA  // OTA升级标志
#define UPGRADE_FLAG_YMODEM     0xAA55AA55  // Ymodem升级标志
#define UPGRADE_FLAG_APP        0x5A5A5A5A  // APP升级标志

// Bootloader信息结构体
typedef struct {
    uint32_t magic;             // 魔数 0x424F4F54 "BOOT"
    uint32_t version;           // Bootloader版本
    uint32_t app_addr;          // APP运行地址
    uint32_t app_size;          // APP大小
    uint32_t app_crc;           // APP校验值
    uint32_t upgrade_flag;      // 升级标志
    uint32_t reserve[2];        // 保留
} BootInfo_t;

// APP信息结构体
typedef struct {
    uint32_t magic;             // 魔数 0x41505031 "APP1"
    uint32_t version;           // APP版本
    uint32_t size;              // APP大小
    uint32_t crc;               // APP校验值
    uint32_t timestamp;         // 编译时间戳
    uint32_t reserve[3];        // 保留
} AppInfo_t;

// 备份寄存器分配（使用RTC备份寄存器）
#define BKP_REG_UPGRADE_FLAG    RTC_BKP_DR0  // 升级标志
#define BKP_REG_CONFIG_MAGIC    RTC_BKP_DR1  // 配置魔数
#define BKP_REG_TIME_FLAG       RTC_BKP_DR2  // 授时标志

// Flash操作接口
int bootloader_erase_app_area(void);
int bootloader_write_app_data(uint32_t offset, const uint8_t *data, uint32_t len);
int bootloader_verify_app(void);
int bootloader_jump_to_app(void);

#endif
```

### 0.3 APP启动地址配置
```c
// system_config.h - 系统配置
#define APP_START_ADDR          0x08020000  // APP起始地址
#define APP_END_ADDR            0x0807FFFF  // APP结束地址
#define APP_MAX_SIZE            (APP_END_ADDR - APP_START_ADDR + 1)  // 384KB

// 中断向量表偏移
#define NVIC_VETOR_TABLE_OFFSET (APP_START_ADDR - 0x08000000)  // 0x20000
```

### 0.4 Keil工程配置
需要修改Keil工程的IROM配置：
- IROM起始地址：0x08020000
- IROM大小：0x00060000 (384KB)
- IRAM起始地址：0x20000000
- IRAM大小：0x00020000 (128KB)

## 1. 系统架构设计

### 1.1 分层架构设计（七层架构）
基于记忆中的STM32物联网网关分层架构，设计以下层级：

```
stm32_App/
├── App/                    # 应用层
│   ├── main.c             # 主程序入口
│   ├── app_sensor.c       # 传感器采集应用
│   ├── app_cloud.c        # 云平台通信应用
│   ├── app_camera.c       # 摄像头应用
│   ├── app_lcd.c          # LCD显示应用
│   ├── app_ota.c          # OTA升级应用
│   ├── app_ntp.c          # NTP时间同步应用
│   └── app_ymodem.c       # Ymodem升级应用
├── Components/             # 组件层
│   ├── cloud/             # 云平台组件
│   ├── sensor/            # 传感器组件
│   ├── camera/            # 摄像头组件
│   ├── lcd/               # LCD组件
│   ├── ota/               # OTA组件
│   ├── ntp/               # NTP组件
│   └── ymodem/            # Ymodem组件
├── OSAL/                   # 操作系统抽象层
│   ├── osal_task.c        # 任务管理
│   ├── osal_queue.c       # 消息队列
│   ├── osal_semaphore.c   # 信号量
│   └── osal_mutex.c       # 互斥锁
├── Platform/               # 平台层
│   ├── drv/               # 驱动层
│   │   ├── drv_i2c.c      # I2C驱动
│   │   ├── drv_spi.c      # SPI驱动
│   │   ├── drv_uart.c     # UART驱动
│   │   ├── drv_gpio.c     # GPIO驱动
│   │   └── drv_eth.c      # 以太网驱动（仅封装，不修改LwIP）
│   └── bsp/               # 板级支持包
│       ├── bsp_led.c      # LED驱动
│       ├── bsp_key.c      # 按键驱动
│       └── bsp_dht11.c    # DHT11驱动
├── Tools/                  # 工具层
│   ├── debug.c            # 调试工具
│   ├── ringbuffer.c       # 环形缓冲区
│   └── crc.c              # CRC校验
├── Config/                 # 配置层
│   ├── system_config.h    # 系统配置
│   ├── task_config.h      # 任务配置
│   └── module_config.h    # 模块配置
├── FwUpgrade/              # 固件升级层
│   ├── ota_upgrade.c      # OTA升级
│   └── ymodem_upgrade.c   # Ymodem升级
│
├── LwIP/                   # 【保持不变】LwIP网络栈
│   ├── api/               # 【保持不变】Netconn API
│   ├── apps/              # 【保持不变】LwIP应用
│   ├── core/              # 【保持不变】LwIP核心
│   ├── include/           # 【保持不变】LwIP头文件
│   └── netif/             # 【保持不变】网络接口
│
├── MQTT/                   # 【保持不变】MQTT客户端
├── cJSON/                  # 【保持不变】JSON处理
├── FreeRTOS/               # 【保持不变】FreeRTOS内核
├── Libraries/              # 【保持不变】HAL库和CMSIS
│
└── User/                   # 原有用户代码（逐步迁移）
    ├── arch/              # 【保持不变】LwIP架构相关
    │   ├── sys_arch.c     # 【保持不变】系统架构实现
    │   ├── lwipopts.h     # 【保持不变】LwIP配置
    │   └── ethernetif.c   # 【保持不变】以太网接口
    ├── main.c             # 迁移到App/目录
    ├── board.c            # 迁移到Platform/bsp/
    └── ...其他文件        # 按层级迁移
```

### 1.2 接口注册表设计（.def文件）
设计模块注册表，支持I2C/SPI/USART等接口的快速注册：

```c
// platform_def.h - 平台接口定义表
#ifndef __PLATFORM_DEF_H
#define __PLATFORM_DEF_H

// I2C接口定义
typedef struct {
    uint8_t i2c_id;
    I2C_HandleTypeDef *hi2c;
    uint32_t timeout;
    void (*init)(void);
    void (*deinit)(void);
} I2C_DevDef_t;

// SPI接口定义
typedef struct {
    uint8_t spi_id;
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    void (*init)(void);
    void (*deinit)(void);
} SPI_DevDef_t;

// UART接口定义
typedef struct {
    uint8_t uart_id;
    UART_HandleTypeDef *huart;
    uint32_t baudrate;
    void (*init)(void);
    void (*deinit)(void);
    void (*rx_callback)(uint8_t data);
} UART_DevDef_t;

// 设备注册表
typedef struct {
    const char *dev_name;
    uint8_t dev_type;  // I2C/SPI/UART/GPIO
    union {
        I2C_DevDef_t i2c_dev;
        SPI_DevDef_t spi_dev;
        UART_DevDef_t uart_dev;
    } dev;
    uint8_t is_used;
} DeviceRegistry_t;

// 注册表最大设备数
#define MAX_DEVICE_NUM  32

// 设备注册函数
int device_register(const char *name, uint8_t type, void *dev_def);
int device_unregister(const char *name);
DeviceRegistry_t* device_find(const char *name);

#endif
```

### 1.3 驱动注册表.def文件示例
```c
// drv_i2c.def - I2C驱动注册表
#ifndef DRV_I2C_DEF_H
#define DRV_I2C_DEF_H

// I2C设备注册表
#define I2C_DEVICE_TABLE \
    X(I2C_DEV_TEMP, I2C1, 400000, GPIOB, GPIO_PIN_6, GPIOB, GPIO_PIN_7) \
    X(I2C_DEV_EEPROM, I2C2, 100000, GPIOB, GPIO_PIN_10, GPIOB, GPIO_PIN_11)

// 生成枚举
#define X(id, i2c, speed, sda_port, sda_pin, scl_port, scl_pin) id,
typedef enum {
    I2C_DEVICE_TABLE
    I2C_DEV_MAX
} i2c_dev_id_t;
#undef X

// 生成配置结构
typedef struct {
    i2c_dev_id_t id;
    I2C_TypeDef *i2c;
    uint32_t speed;
    GPIO_TypeDef *sda_port;
    uint16_t sda_pin;
    GPIO_TypeDef *scl_port;
    uint16_t scl_pin;
} i2c_dev_config_t;

// 获取配置函数声明
const i2c_dev_config_t* drv_i2c_get_config(i2c_dev_id_t id);

#endif
```

### 1.4 驱动管理器设计
```c
// drv_manager.h - 驱动管理器
typedef struct {
    void (*init)(void);
    void (*deinit)(void);
    int (*read)(uint8_t dev_id, uint8_t *data, uint16_t len);
    int (*write)(uint8_t dev_id, const uint8_t *data, uint16_t len);
} drv_ops_t;

// 驱动注册宏
#define DRV_REGISTER(type, ops) \
    static const drv_ops_t* const type##_ops __attribute__((used)) = &(ops)

// 驱动管理函数
int drv_manager_init(void);
int drv_register(const char *name, const drv_ops_t *ops);
const drv_ops_t* drv_find(const char *name);
```

## 2. 任务设计

### 2.1 FreeRTOS任务规划
根据系统需求，设计以下任务：

| 任务名称 | 优先级 | 栈大小 | 功能描述 |
|---------|--------|--------|----------|
| Task_Sensor采集 | 4 | 512字节 | 传感器数据采集（DHT11、光照、烟雾等） |
| Task_Cloud通信 | 3 | 1024字节 | 云平台MQTT通信（OneNET） |
| Task_Camera采集 | 2 | 1024字节 | OV5640摄像头图像采集 |
| Task_LCD显示 | 2 | 512字节 | 4.3寸LCD显示控制 |
| Task_NTP同步 | 1 | 512字节 | NTP时间同步 |
| Task_OTA升级 | 1 | 1024字节 | OTA固件升级 |
| Task_Ymodem升级 | 1 | 512字节 | Ymodem串口升级 |
| Task_System监控 | 5 | 256字节 | 系统状态监控 |
| Task_空闲任务 | 0 | 128字节 | FreeRTOS空闲任务 |

### 2.2 任务优先级设计
- 优先级5：系统监控任务（最高优先级，确保系统稳定性）
- 优先级4：传感器采集任务（实时性要求高）
- 优先级3：云平台通信任务（网络通信）
- 优先级2：摄像头和LCD任务（图像处理）
- 优先级1：NTP、OTA、Ymodem任务（后台任务）
- 优先级0：空闲任务（FreeRTOS默认）

## 3. 多链路数据资源管理

### 3.1 链路数据隔离设计
设计多链路数据资源管理，防止链路间数据抢占：

```c
// net_manager.h - 网络链路管理器
#ifndef __NET_MANAGER_H
#define __NET_MANAGER_H

// 链路类型定义
typedef enum {
    LINK_MQTT = 0,      // MQTT链路
    LINK_HTTP,          // HTTP链路
    LINK_OTA,           // OTA升级链路
    LINK_NTP,           // NTP链路
    LINK_MAX
} net_link_id_t;

// 链路信息结构体
typedef struct {
    net_link_id_t id;
    int socket;
    uint8_t priority;       // 优先级
    uint32_t timeout;       // 超时时间
    uint8_t is_active;      // 是否激活
    SemaphoreHandle_t mutex; // 互斥锁
} net_link_t;

// 链路管理接口
int net_manager_init(void);
int net_manager_create_link(net_link_id_t id, uint8_t priority);
int net_manager_send(net_link_id_t id, const uint8_t *data, uint16_t len);
int net_manager_recv(net_link_id_t id, uint8_t *buf, uint16_t len, uint32_t timeout);
void net_manager_release_link(net_link_id_t id);
int net_manager_get_status(net_link_id_t id);

#endif
```

### 3.2 带宽分配策略
- 基于优先级的链路调度
- 关键链路（MQTT、OTA）保证最低带宽
- 非关键链路（NTP）可被抢占
- 使用信号量控制并发访问

### 3.3 网络多链路数据保护机制
1. **互斥锁保护**：每个链路使用独立的互斥锁，防止多任务同时访问
2. **消息队列**：使用FreeRTOS消息队列进行链路间数据传递
3. **环形缓冲区**：为每个链路分配独立的环形缓冲区
4. **超时机制**：所有链路操作设置超时，防止阻塞
5. **优先级调度**：基于优先级的链路调度，保证关键链路带宽

## 4. 防止阻塞设计

### 4.1 非阻塞API设计
所有外设操作采用非阻塞方式：

```c
// 非阻塞发送函数示例
int uart_send_nonblocking(UART_HandleTypeDef *huart, const uint8_t *data, uint32_t len, uint32_t timeout) {
    BaseType_t ret;
    uint32_t start_tick = xTaskGetTickCount();
    
    // 使用DMA发送，非阻塞
    if (HAL_UART_Transmit_DMA(huart, data, len) != HAL_OK) {
        return -1;
    }
    
    // 等待发送完成，带超时
    while (huart->gState != HAL_UART_STATE_READY) {
        if ((xTaskGetTickCount() - start_tick) > timeout) {
            HAL_UART_AbortTransmit(huart);
            return -2;  // 超时
        }
        vTaskDelay(1);  // 让出CPU
    }
    
    return 0;  // 成功
}
```

### 4.2 任务间通信非阻塞设计
1. **队列非阻塞发送**：使用`xQueueSend`的非阻塞模式
2. **信号量超时等待**：所有信号量等待设置超时时间
3. **事件标志组**：使用事件标志组进行多事件同步
4. **任务通知**：使用任务通知替代信号量，提高效率

## 5. 模块详细设计

### 5.1 传感器模块设计
支持多种传感器接口：

```c
// sensor_def.h - 传感器定义
typedef enum {
    SENSOR_DHT11 = 0,    // 温湿度传感器（单总线）
    SENSOR_LIGHT,        // 光照传感器（ADC）
    SENSOR_SMOKE,        // 烟雾传感器（ADC）
    SENSOR_MQ2,          // MQ2气体传感器（ADC）
    SENSOR_MAX
} SensorType_e;

// 传感器数据结构
typedef struct {
    SensorType_e type;
    float temperature;
    float humidity;
    float light_value;
    float smoke_value;
    uint32_t timestamp;
    uint8_t is_valid;
} SensorData_t;
```

### 5.2 云平台模块设计
支持OneNET MQTT协议：

```c
// cloud_config.h - 云平台配置
#define CLOUD_MQTT_HOST      "mqtt.heclouds.com"
#define CLOUD_MQTT_PORT      6002
#define CLOUD_CLIENT_ID      "518725049"
#define CLOUD_USER_NAME      "217537"
#define CLOUD_PASSWORD       "12345"
#define CLOUD_TOPIC          "temp_hum"

// 云平台消息类型
typedef enum {
    MSG_TYPE_SENSOR_DATA = 0,   // 传感器数据
    MSG_TYPE_DEVICE_STATUS,     // 设备状态
    MSG_TYPE_OTA_COMMAND,       // OTA指令
    MSG_TYPE_CONFIG_UPDATE,     // 配置更新
    MSG_TYPE_MAX
} CloudMsgType_e;
```

### 5.3 OTA升级模块设计
支持OneNET OTA和Ymodem双升级方式：

```c
// ota_config.h - OTA配置
#define OTA_CHUNK_SIZE      1024    // OTA数据块大小
#define OTA_TIMEOUT         30000   // OTA超时时间(ms)
#define OTA_RETRY_COUNT     3       // 重试次数

// OTA状态机
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECK,
    OTA_STATE_DOWNLOAD,
    OTA_STATE_VERIFY,
    OTA_STATE_APPLY,
    OTA_STATE_MAX
} OTA_State_e;
```

### 5.3.1 OTA升级流程设计
```c
// ota_upgrade.c - OTA升级实现
int ota_upgrade_process(void) {
    // 1. 检查升级标志
    uint32_t upgrade_flag = HAL_RTCEx_BKUPRead(&hrtc, BKP_REG_UPGRADE_FLAG);
    if (upgrade_flag != UPGRADE_FLAG_OTA) {
        return -1;  // 无升级请求
    }
    
    // 2. 擦除OTA备份区
    if (flash_erase(OTA_BACKUP_ADDR, OTA_BACKUP_SIZE) != 0) {
        return -2;
    }
    
    // 3. 下载固件到OTA备份区
    if (ota_download_firmware() != 0) {
        return -3;
    }
    
    // 4. 验证固件完整性
    if (ota_verify_firmware() != 0) {
        return -4;
    }
    
    // 5. 擦除APP区
    if (flash_erase(APP_START_ADDR, APP_MAX_SIZE) != 0) {
        return -5;
    }
    
    // 6. 将OTA备份区复制到APP区
    if (flash_copy(OTA_BACKUP_ADDR, APP_START_ADDR, APP_MAX_SIZE) != 0) {
        return -6;
    }
    
    // 7. 清除升级标志
    HAL_RTCEx_BKUPWrite(&hrtc, BKP_REG_UPGRADE_FLAG, UPGRADE_FLAG_NONE);
    
    // 8. 重启系统
    NVIC_SystemReset();
    
    return 0;
}
```

### 5.3.2 Ymodem升级流程设计
```c
// ymodem_upgrade.c - Ymodem升级实现
int ymodem_upgrade_process(void) {
    // 1. 检查升级标志
    uint32_t upgrade_flag = HAL_RTCEx_BKUPRead(&hrtc, BKP_REG_UPGRADE_FLAG);
    if (upgrade_flag != UPGRADE_FLAG_YMODEM) {
        return -1;  // 无升级请求
    }
    
    // 2. 擦除OTA备份区
    if (flash_erase(OTA_BACKUP_ADDR, OTA_BACKUP_SIZE) != 0) {
        return -2;
    }
    
    // 3. 通过Ymodem接收固件
    if (ymodem_receive_firmware() != 0) {
        return -3;
    }
    
    // 4. 验证固件完整性
    if (ymodem_verify_firmware() != 0) {
        return -4;
    }
    
    // 5. 擦除APP区
    if (flash_erase(APP_START_ADDR, APP_MAX_SIZE) != 0) {
        return -5;
    }
    
    // 6. 将OTA备份区复制到APP区
    if (flash_copy(OTA_BACKUP_ADDR, APP_START_ADDR, APP_MAX_SIZE) != 0) {
        return -6;
    }
    
    // 7. 清除升级标志
    HAL_RTCEx_BKUPWrite(&hrtc, BKP_REG_UPGRADE_FLAG, UPGRADE_FLAG_NONE);
    
    // 8. 重启系统
    NVIC_SystemReset();
    
    return 0;
}
```

### 5.4 摄像头模块设计
OV5640摄像头驱动：

```c
// camera_config.h - 摄像头配置
#define CAMERA_OV5640       1
#define CAMERA_RESOLUTION   RESOLUTION_640x480
#define CAMERA_FORMAT       FORMAT_RGB565

// 摄像头数据结构
typedef struct {
    uint8_t *frame_buffer;
    uint32_t frame_size;
    uint16_t width;
    uint16_t height;
    uint8_t format;
    uint32_t timestamp;
} CameraFrame_t;
```

### 5.5 LCD模块设计
4.3寸NT35510 LCD驱动：

```c
// lcd_config.h - LCD配置
#define LCD_NT35510         1
#define LCD_WIDTH           480
#define LCD_HEIGHT          800
#define LCD_PIXEL_FORMAT    LCD_PIXEL_FORMAT_RGB565

// LCD显示区域
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} LCD_Region_t;
```

### 5.6 NTP模块设计
网络时间同步：

```c
// ntp_config.h - NTP配置
#define NTP_SERVER          "ntp.aliyun.com"
#define NTP_PORT            123
#define NTP_TIMEOUT         5000    // NTP超时时间(ms)
#define NTP_SYNC_INTERVAL   3600000 // 同步间隔(ms)

// NTP时间结构
typedef struct {
    uint32_t timestamp;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t timezone;
} NTP_Time_t;
```

## 6. 系统监控模块

### 6.1 任务管理器设计
```c
// task_manager.h - 任务管理器
typedef enum {
    TASK_ID_IDLE = 0,
    TASK_ID_SENSOR,
    TASK_ID_CLOUD,
    TASK_ID_OTA,
    TASK_ID_CAMERA,
    TASK_ID_NTP,
    TASK_ID_LCD,
    TASK_ID_MONITOR,
    TASK_ID_MAX
} task_id_t;

typedef struct {
    task_id_t id;
    const char *name;
    TaskFunction_t entry;
    uint16_t stack_size;
    UBaseType_t priority;
    TaskHandle_t handle;
    uint32_t runtime;       // 运行时间统计
    uint8_t is_running;     // 运行状态
} task_info_t;

// 任务管理接口
int task_manager_create_task(task_id_t id);
void task_manager_delete_task(task_id_t id);
void task_manager_suspend_task(task_id_t id);
void task_manager_resume_task(task_id_t id);
void task_manager_get_status(task_id_t id, task_info_t *info);
```

### 6.2 系统监控任务设计
```c
// system_monitor.c - 系统监控
typedef struct {
    uint32_t cpu_usage;         // CPU使用率
    uint32_t heap_free;         // 空闲堆内存
    uint32_t stack_free;        // 空闲栈空间
    uint32_t task_count;        // 任务数量
    uint32_t uptime;            // 运行时间
    uint8_t link_status[LINK_MAX]; // 链路状态
} SystemStatus_t;

// 监控任务
void Task_SystemMonitor(void *pvParameters) {
    SystemStatus_t status;
    while (1) {
        // 获取系统状态
        status.cpu_usage = get_cpu_usage();
        status.heap_free = xPortGetFreeHeapSize();
        status.uptime = xTaskGetTickCount() / configTICK_RATE_HZ;
        
        // 打印系统状态
        printf("CPU: %d%%, Heap: %d bytes, Uptime: %d s\r\n", 
               status.cpu_usage, status.heap_free, status.uptime);
        
        vTaskDelay(10000);  // 每10秒监控一次
    }
}
```

### 6.3 系统监控功能
1. **CPU使用率监控**：统计各任务CPU占用时间
2. **内存使用监控**：监控堆栈使用情况，防止内存泄漏
3. **任务状态监控**：监控各任务运行状态，检测任务异常
4. **网络链路监控**：监控各网络链路连接状态
5. **传感器数据监控**：监控传感器数据异常
6. **系统日志记录**：记录系统运行日志，便于故障排查

## 7. 编码规范

### 7.1 函数说明格式
按照用户要求的格式：
```c
/*******************************************************************************
** 函数名称    function_name
** 函数说明    函数功能描述
** 输入参数    参数说明
** 输出参数    参数说明
** 返回参数    返回值说明
*******************************************************************************/
```

### 7.2 文件编码格式
- 所有源代码文件使用UTF-8编码
- 注释使用中文，避免乱码问题
- 头文件包含标准#ifndef保护

### 7.3 命名规范
- 文件名：小写+下划线，按层加前缀（app_/drv_/tool_等）
- 函数名：驱动层用大写驼峰，组件层用小写下划线
- 宏定义：全大写+下划线，配置用GW_前缀
- 类型定义：结构体用_T或_t后缀

## 8. 实施步骤

### 阶段1：基础架构搭建（1-2天）
1. 创建目录结构
2. 设计platform_def.h接口注册表
3. 实现OSAL层基础函数
4. 配置FreeRTOSConfig.h
5. 修改Keil工程IROM配置（0x08020000开始）

### 阶段2：驱动层开发（2-3天）
1. 实现I2C/SPI/UART驱动
2. 实现GPIO驱动
3. 实现以太网驱动封装（仅封装LwIP接口，不修改LwIP代码）
4. 实现基础外设驱动（LED、按键）
5. 实现驱动注册表.def文件

### 阶段3：组件层开发（3-4天）
1. 实现传感器组件
2. 实现云平台组件（调用现有MQTT库）
3. 实现摄像头组件
4. 实现LCD组件
5. 实现网络多链路管理器（基于LwIP Socket API）

### 阶段4：应用层开发（2-3天）
1. 实现传感器采集应用
2. 实现云平台通信应用
3. 实现摄像头应用
4. 实现LCD显示应用
5. 实现任务管理器

### 阶段5：系统集成（1-2天）
1. 实现系统监控任务
2. 实现OTA升级功能
3. 实现Ymodem升级功能
4. 实现NTP时间同步
5. 实现Bootloader接口

### 阶段6：测试优化（1-2天）
1. 系统稳定性测试
2. 性能优化
3. 文档编写
4. 编译验证

### ⚠️ 重要提醒
- **LwIP相关代码（LwIP/、User/arch/、MQTT/、cJSON/）保持原样，不做任何修改**
- **LwIP调试信息、printf输出、错误日志全部保留**
- **网络初始化仅调用TCPIP_Init()接口**
- **新功能通过LwIP Socket API实现，不修改LwIP内部代码**

## 9. 注意事项

### 9.1 LWIP代码保护（重要）
1. **不修改LWIP相关代码**：LwIP目录下的所有文件（api/、apps/、core/、include/、netif/）保持原样
2. **保留所有调试信息**：
   - LwIP调试代码、printf输出、错误信息等全部保留
   - sys_arch.c中的printf打印（如"[sys_arch]:new sem fail!"）保留
   - ethernetif.c中的调试输出保留
   - 所有LWIP_DEBUG、LWIP_ERROR相关打印保留
3. **不修改sys_arch.c**：User/arch/目录下的sys_arch.c、lwipopts.h等文件保持原样
4. **不修改ethernetif.c**：以太网接口文件保持原样
5. **不修改FreeRTOS配置**：FreeRTOSConfig.h中的LwIP相关配置保持不变
6. **网络初始化调用**：仅调用TCPIP_Init()接口，不修改其内部实现
7. **MQTT相关打印保留**：mqttclient.c、transport.c中的调试输出保留
8. **cJSON相关打印保留**：cJSON_Process.c中的调试输出保留

### 9.2 其他注意事项
1. **HAL库使用**：所有外设操作使用HAL库函数
2. **内存管理**：合理分配堆栈空间，避免内存泄漏
3. **任务优先级**：合理设置任务优先级，防止优先级反转
4. **编码格式**：所有代码使用UTF-8编码，避免乱码
5. **文档生成**：文档存放在D:\self_work\stm32\STM32_APP\self_function\stm32_App\Doc目录下
6. **Bootloader兼容**：确保APP与bootloader接口兼容
7. **Flash分区**：严格按照Flash分区规划使用，避免地址冲突
8. **中断向量表**：APP启动时需要重新设置中断向量表偏移
9. **备份寄存器**：合理使用RTC备份寄存器，避免冲突

## 10. 风险评估

1. **内存不足**：STM32F407内存有限，需要合理分配
   - 缓解措施：使用内存池，优化缓冲区大小

2. **任务冲突**：多任务访问共享资源可能导致死锁
   - 缓解措施：合理设计优先级，使用互斥锁

3. **网络不稳定**：网络通信可能中断，需要重连机制
   - 缓解措施：实现重连机制，心跳保活

4. **外设冲突**：多个外设使用相同引脚可能冲突
   - 缓解措施：使用.def文件统一管理引脚分配

5. **实时性要求**：传感器采集需要高实时性，需要优化任务调度
   - 缓解措施：使用DMA，减少中断处理时间

6. **升级失败**：OTA或Ymodem升级可能失败
   - 缓解措施：使用备份区，支持回滚机制

7. **Flash损坏**：频繁写入可能导致Flash损坏
   - 缓解措施：减少写入次数，使用磨损均衡

8. **Bootloader兼容**：APP与bootloader接口不兼容
   - 缓解措施：严格遵循接口规范，版本控制

## 11. 预期成果

1. 完整的IoT采集终端系统
2. 模块化、可扩展的代码架构
3. 稳定的多任务运行环境
4. 完善的系统监控机制
5. 支持OTA和Ymodem双升级方式
6. 详细的开发文档

---

## 12. 详细三级开发计划

### 网络环境配置
- **上位机IPv4地址**：192.168.1.12
- **子网掩码**：255.255.255.0
- **默认网关**：192.168.1.1
- **STM32目标IP**：192.168.1.100（需与上位机同网段）
- **STM32目标端口**：8080（用于调试通信）

---

### 第一阶段：基础框架搭建与LWIP调试验证（5-7天）

#### 目标
完成基础架构搭建，编译通过，实现LWIP网络通信调试功能，可与上位机进行TCP/UDP通信。

#### 1.1 目录结构创建（0.5天）
```
stm32_App/
├── App/                    # 应用层
│   ├── main.c             # 主程序入口（新建）
│   └── app_config.h       # 应用配置（新建）
├── Components/             # 组件层（空目录，后续填充）
├── OSAL/                   # 操作系统抽象层
│   ├── osal.h             # OSAL接口定义（新建）
│   └── osal.c             # OSAL实现（新建）
├── Platform/               # 平台层
│   ├── drv/               # 驱动层（空目录，后续填充）
│   └── bsp/               # 板级支持包（空目录，后续填充）
├── Config/                 # 配置层
│   ├── system_config.h    # 系统配置（新建）
│   └── task_config.h      # 任务配置（新建）
├── Tools/                  # 工具层
│   ├── debug.h            # 调试工具头文件（新建）
│   └── debug.c            # 调试工具实现（新建）
└── [保持不变的目录]
    ├── LwIP/              # 【保持不变】
    ├── MQTT/              # 【保持不变】
    ├── cJSON/             # 【保持不变】
    ├── FreeRTOS/          # 【保持不变】
    ├── Libraries/         # 【保持不变】
    └── User/              # 【保持不变】原代码
```

#### 1.2 系统配置文件创建（0.5天）

**system_config.h**：
```c
#ifndef __SYSTEM_CONFIG_H
#define __SYSTEM_CONFIG_H

// APP运行地址配置
#define APP_START_ADDR          0x08020000
#define APP_END_ADDR            0x0807FFFF
#define APP_MAX_SIZE            (APP_END_ADDR - APP_START_ADDR + 1)
#define NVIC_VETOR_TABLE_OFFSET (APP_START_ADDR - 0x08000000)

// 网络配置（与上位机同网段）
#define LOCAL_IP_ADDR0          192
#define LOCAL_IP_ADDR1          168
#define LOCAL_IP_ADDR2          1
#define LOCAL_IP_ADDR3          100

#define LOCAL_NETMASK0          255
#define LOCAL_NETMASK1          255
#define LOCAL_NETMASK2          255
#define LOCAL_NETMASK3          0

#define LOCAL_GW0               192
#define LOCAL_GW1               168
#define LOCAL_GW2               1
#define LOCAL_GW3               1

// 调试服务器配置（上位机）
#define DEBUG_SERVER_IP0        192
#define DEBUG_SERVER_IP1        168
#define DEBUG_SERVER_IP2        1
#define DEBUG_SERVER_IP3        12

#define DEBUG_SERVER_PORT       8080

// 调试串口配置
#define DEBUG_USART_BAUDRATE    115200

#endif
```

**task_config.h**：
```c
#ifndef __TASK_CONFIG_H
#define __TASK_CONFIG_H

// 任务优先级定义
#define TASK_PRIO_IDLE          0
#define TASK_PRIO_LOW           1
#define TASK_PRIO_NORMAL        2
#define TASK_PRIO_HIGH          3
#define TASK_PRIO_HIGHEST       4
#define TASK_PRIO_MONITOR       5

// 任务栈大小定义（单位：字）
#define TASK_STACK_SIZE_SMALL   128
#define TASK_STACK_SIZE_MEDIUM  256
#define TASK_STACK_SIZE_LARGE   512
#define TASK_STACK_SIZE_XLARGE  1024

// 任务ID定义
typedef enum {
    TASK_ID_IDLE = 0,
    TASK_ID_LED,
    TASK_ID_NET_DEBUG,
    TASK_ID_MONITOR,
    TASK_ID_MAX
} task_id_t;

#endif
```

#### 1.3 OSAL层实现（1天）

**osal.h**：
```c
#ifndef __OSAL_H
#define __OSAL_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

// OSAL任务管理
typedef TaskHandle_t osal_task_t;
typedef QueueHandle_t osal_queue_t;
typedef SemaphoreHandle_t osal_sem_t;
typedef SemaphoreHandle_t osal_mutex_t;

// 任务管理接口
int osal_task_create(osal_task_t *task, const char *name, 
                     void (*entry)(void*), void *param,
                     uint32_t stack_size, uint32_t priority);
void osal_task_delete(osal_task_t task);
void osal_task_delay(uint32_t ms);

// 队列管理接口
osal_queue_t osal_queue_create(uint32_t size, uint32_t item_size);
int osal_queue_send(osal_queue_t queue, const void *item, uint32_t timeout);
int osal_queue_recv(osal_queue_t queue, void *item, uint32_t timeout);

// 信号量管理接口
osal_sem_t osal_sem_create(void);
int osal_sem_wait(osal_sem_t sem, uint32_t timeout);
void osal_sem_post(osal_sem_t sem);

// 互斥锁管理接口
osal_mutex_t osal_mutex_create(void);
void osal_mutex_lock(osal_mutex_t mutex);
void osal_mutex_unlock(osal_mutex_t mutex);

#endif
```

#### 1.4 调试工具实现（1天）

**debug.h**：
```c
#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdint.h>

// 调试级别定义
#define DEBUG_LEVEL_NONE        0
#define DEBUG_LEVEL_ERROR       1
#define DEBUG_LEVEL_WARN        2
#define DEBUG_LEVEL_INFO        3
#define DEBUG_LEVEL_DEBUG       4

// 当前调试级别
#define DEBUG_LEVEL             DEBUG_LEVEL_DEBUG

// 调试宏定义
#define DEBUG_ERROR(fmt, ...)   do { \
    if (DEBUG_LEVEL >= DEBUG_LEVEL_ERROR) \
        printf("[ERROR] " fmt "\r\n", ##__VA_ARGS__); \
} while(0)

#define DEBUG_WARN(fmt, ...)    do { \
    if (DEBUG_LEVEL >= DEBUG_LEVEL_WARN) \
        printf("[WARN] " fmt "\r\n", ##__VA_ARGS__); \
} while(0)

#define DEBUG_INFO(fmt, ...)    do { \
    if (DEBUG_LEVEL >= DEBUG_LEVEL_INFO) \
        printf("[INFO] " fmt "\r\n", ##__VA_ARGS__); \
} while(0)

#define DEBUG_DEBUG(fmt, ...)   do { \
    if (DEBUG_LEVEL >= DEBUG_LEVEL_DEBUG) \
        printf("[DEBUG] " fmt "\r\n", ##__VA_ARGS__); \
} while(0)

// 网络调试接口
int debug_net_init(void);
int debug_net_send(const uint8_t *data, uint32_t len);
int debug_net_recv(uint8_t *buffer, uint32_t len, uint32_t timeout);

#endif
```

**debug.c**：
```c
#include "debug.h"
#include "system_config.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static int debug_socket = -1;

int debug_net_init(void) {
    struct sockaddr_in server_addr;
    
    // 创建TCP socket
    debug_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (debug_socket < 0) {
        DEBUG_ERROR("Create socket failed");
        return -1;
    }
    
    // 配置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DEBUG_SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(
        (DEBUG_SERVER_IP0 << 24) | 
        (DEBUG_SERVER_IP1 << 16) | 
        (DEBUG_SERVER_IP2 << 8) | 
        DEBUG_SERVER_IP3
    );
    
    // 连接服务器
    if (connect(debug_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        DEBUG_ERROR("Connect to debug server failed");
        close(debug_socket);
        debug_socket = -1;
        return -2;
    }
    
    DEBUG_INFO("Connected to debug server %d.%d.%d.%d:%d",
               DEBUG_SERVER_IP0, DEBUG_SERVER_IP1, 
               DEBUG_SERVER_IP2, DEBUG_SERVER_IP3,
               DEBUG_SERVER_PORT);
    
    return 0;
}

int debug_net_send(const uint8_t *data, uint32_t len) {
    if (debug_socket < 0) {
        return -1;
    }
    
    int ret = send(debug_socket, data, len, 0);
    if (ret < 0) {
        DEBUG_ERROR("Send data failed");
        return -2;
    }
    
    return ret;
}

int debug_net_recv(uint8_t *buffer, uint32_t len, uint32_t timeout) {
    if (debug_socket < 0) {
        return -1;
    }
    
    // 设置超时
    struct timeval tv;
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    setsockopt(debug_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    int ret = recv(debug_socket, buffer, len, 0);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // 超时
        }
        DEBUG_ERROR("Receive data failed");
        return -2;
    }
    
    return ret;
}
```

#### 1.5 主程序实现（1天）

**App/main.c**：
```c
#include "main.h"
#include "system_config.h"
#include "task_config.h"
#include "osal.h"
#include "debug.h"

// 任务句柄
static osal_task_t task_led;
static osal_task_t task_net_debug;
static osal_task_t task_monitor;

/*******************************************************************************
** 函数名称    LED_Task
** 函数说明    LED闪烁任务
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void LED_Task(void *pvParameters) {
    while (1) {
        LED1_TOGGLE;
        osal_task_delay(500);
    }
}

/*******************************************************************************
** 函数名称    NetDebug_Task
** 函数说明    网络调试任务，与上位机通信
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void NetDebug_Task(void *pvParameters) {
    uint8_t recv_buffer[256];
    int ret;
    
    // 等待网络就绪
    osal_task_delay(3000);
    
    // 初始化网络调试
    while (debug_net_init() != 0) {
        DEBUG_WARN("Retry connect to debug server...");
        osal_task_delay(2000);
    }
    
    // 发送欢迎消息
    const char *welcome = "STM32 IoT Terminal Connected!\r\n";
    debug_net_send((uint8_t*)welcome, strlen(welcome));
    
    while (1) {
        // 接收上位机数据
        ret = debug_net_recv(recv_buffer, sizeof(recv_buffer), 100);
        if (ret > 0) {
            // 回显数据
            debug_net_send(recv_buffer, ret);
            
            // 打印到串口
            DEBUG_INFO("Received %d bytes from PC", ret);
        }
        
        // 发送心跳
        static uint32_t heartbeat_cnt = 0;
        if (++heartbeat_cnt >= 100) {  // 每10秒
            heartbeat_cnt = 0;
            const char *heartbeat = "HEARTBEAT\r\n";
            debug_net_send((uint8_t*)heartbeat, strlen(heartbeat));
        }
        
        osal_task_delay(100);
    }
}

/*******************************************************************************
** 函数名称    Monitor_Task
** 函数说明    系统监控任务
** 输入参数    pvParameters - 任务参数
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void Monitor_Task(void *pvParameters) {
    while (1) {
        // 打印系统状态
        DEBUG_INFO("Heap free: %d bytes", xPortGetFreeHeapSize());
        DEBUG_INFO("Uptime: %d seconds", xTaskGetTickCount() / configTICK_RATE_HZ);
        
        osal_task_delay(10000);  // 每10秒打印一次
    }
}

/*******************************************************************************
** 函数名称    main
** 函数说明    主函数
** 输入参数    无
** 输出参数    无
** 返回参数    无
*******************************************************************************/
int main(void) {
    // 硬件初始化
    BSP_Init();
    
    // 创建任务
    osal_task_create(&task_led, "LED_Task", LED_Task, NULL, 
                     TASK_STACK_SIZE_SMALL, TASK_PRIO_LOW);
    
    osal_task_create(&task_net_debug, "NetDebug_Task", NetDebug_Task, NULL, 
                     TASK_STACK_SIZE_LARGE, TASK_PRIO_NORMAL);
    
    osal_task_create(&task_monitor, "Monitor_Task", Monitor_Task, NULL, 
                     TASK_STACK_SIZE_SMALL, TASK_PRIO_LOW);
    
    // 启动调度器
    vTaskStartScheduler();
    
    while (1);
}
```

#### 1.6 Keil工程配置（0.5天）
1. 修改IROM起始地址为0x08020000
2. 添加新创建的源文件到工程
3. 配置头文件搜索路径
4. 编译验证

#### 1.7 第一阶段验证（1天）
1. **编译验证**：确保编译通过，无错误无警告
2. **串口调试**：通过串口打印验证系统启动
3. **网络连接**：STM32连接到上位机（192.168.1.12:8080）
4. **数据收发**：上位机发送数据，STM32回显
5. **心跳机制**：STM32定期发送心跳包

#### 第一阶段交付物
- [x] 基础目录结构
- [x] 系统配置文件
- [x] OSAL层实现
- [x] 调试工具实现
- [x] 主程序框架
- [x] Keil工程配置
- [x] 编译通过
- [x] LWIP网络调试功能验证

---

### 第二阶段：驱动层与组件层开发（7-10天）

#### 目标
完成驱动注册表机制，实现传感器、云平台等核心组件。

#### 2.1 驱动注册表机制（2天）
1. 实现platform_def.h接口定义
2. 实现drv_manager驱动管理器
3. 实现I2C/SPI/UART驱动注册表.def文件
4. 实现GPIO驱动
5. 实现LED/按键BSP驱动

#### 2.2 传感器组件开发（2天）
1. 实现sensor_manager传感器管理器
2. 实现DHT11温湿度传感器驱动
3. 实现ADC采集驱动（光照、烟雾）
4. 传感器数据采集任务

#### 2.3 云平台组件开发（2天）
1. 实现cloud_manager云平台管理器
2. 集成现有MQTT库
3. 实现OneNET MQTT连接
4. 实现数据上报功能

#### 2.4 网络多链路管理器（2天）
1. 实现net_manager链路管理器
2. 实现链路优先级调度
3. 实现链路互斥锁保护
4. 实现链路超时机制

#### 2.5 第二阶段验证（2天）
1. 传感器数据采集验证
2. 云平台连接验证
3. 多链路并发测试
4. 系统稳定性测试

#### 第二阶段交付物
- [x] 驱动注册表机制
- [x] 传感器组件
- [x] 云平台组件
- [x] 网络多链路管理器
- [x] 功能集成测试

---

### 第三阶段：高级功能开发（7-10天）

#### 目标
完成OTA升级、摄像头、LCD、NTP等高级功能。

#### 3.1 OTA升级模块（2天）
1. 实现OTA状态机
2. 实现固件下载功能
3. 实现固件验证功能
4. 实现Bootloader接口

#### 3.2 Ymodem升级模块（1天）
1. 实现Ymodem协议
2. 实现串口升级功能
3. 实现固件写入Flash

#### 3.3 摄像头模块（2天）
1. 实现OV5640驱动
2. 实现图像采集功能
3. 实现图像压缩传输

#### 3.4 LCD显示模块（2天）
1. 实现NT35510 LCD驱动
2. 实现UI显示框架
3. 实现数据可视化

#### 3.5 NTP时间同步（1天）
1. 实现NTP客户端
2. 实现时间同步功能
3. 实现RTC时间设置

#### 3.6 系统监控完善（1天）
1. 实现CPU使用率统计
2. 实现内存监控
3. 实现任务状态监控
4. 实现日志记录功能

#### 3.7 第三阶段验证（2天）
1. OTA升级测试
2. Ymodem升级测试
3. 摄像头采集测试
4. LCD显示测试
5. NTP同步测试
6. 系统稳定性测试

#### 第三阶段交付物
- [x] OTA升级功能
- [x] Ymodem升级功能
- [x] 摄像头模块
- [x] LCD显示模块
- [x] NTP时间同步
- [x] 系统监控完善
- [x] 完整系统测试

---

### 第一阶段详细实施步骤

#### Day 1：目录结构与配置文件
1. 创建App/、OSAL/、Platform/、Config/、Tools/目录
2. 创建system_config.h（网络配置192.168.1.100）
3. 创建task_config.h（任务配置）
4. 创建app_config.h（应用配置）

#### Day 2：OSAL层实现
1. 创建osal.h（OSAL接口定义）
2. 创建osal.c（OSAL实现）
3. 实现任务管理接口
4. 实现队列管理接口
5. 实现信号量管理接口

#### Day 3：调试工具实现
1. 创建debug.h（调试接口定义）
2. 创建debug.c（网络调试实现）
3. 实现debug_net_init()函数
4. 实现debug_net_send()函数
5. 实现debug_net_recv()函数

#### Day 4：主程序实现
1. 创建App/main.c
2. 实现LED_Task任务
3. 实现NetDebug_Task任务
4. 实现Monitor_Task任务
5. 实现main()函数

#### Day 5：Keil工程配置
1. 修改IROM起始地址为0x08020000
2. 添加新源文件到工程
3. 配置头文件搜索路径
4. 配置网络参数
5. 编译验证

#### Day 6：网络调试验证
1. 配置上位机监听192.168.1.12:8080
2. 烧录程序到STM32
3. 观察串口输出
4. 验证TCP连接建立
5. 测试数据收发功能

#### Day 7：功能完善与测试
1. 完善错误处理
2. 添加重连机制
3. 测试心跳功能
4. 测试长时间运行稳定性
5. 记录测试结果

---

### 验证标准

#### 第一阶段验收标准
1. **编译通过**：无错误，无警告
2. **串口输出**：系统启动信息正常打印
3. **网络连接**：STM32成功连接到192.168.1.12:8080
4. **数据收发**：上位机发送数据，STM32正确回显
5. **心跳机制**：STM32每10秒发送心跳包
6. **稳定性**：连续运行30分钟无异常

#### 第二阶段验收标准
1. **传感器采集**：DHT11数据正确读取
2. **云平台连接**：成功连接OneNET MQTT
3. **数据上报**：传感器数据正确上报
4. **多链路**：MQTT和调试链路并发正常

#### 第三阶段验收标准
1. **OTA升级**：固件下载、验证、升级成功
2. **Ymodem升级**：串口升级功能正常
3. **摄像头**：OV5640图像采集正常
4. **LCD显示**：NT35510显示正常
5. **NTP同步**：时间同步功能正常

---

### 风险与应对

#### 第一阶段风险
1. **LWIP编译错误**
   - 应对：保留原有LwIP代码，仅调用接口

2. **网络连接失败**
   - 应对：检查IP配置，确保同网段

3. **内存不足**
   - 应对：优化栈大小，使用动态分配

#### 第二阶段风险
1. **传感器驱动问题**
   - 应对：参考现有DHT11驱动

2. **MQTT连接失败**
   - 应对：使用现有MQTT库，保留调试信息

3. **多链路冲突**
   - 应对：实现互斥锁保护

#### 第三阶段风险
1. **OTA升级失败**
   - 应对：使用备份区，支持回滚

2. **摄像头驱动问题**
   - 应对：参考OV5640 datasheet

3. **LCD显示异常**
   - 应对：参考NT35510 datasheet