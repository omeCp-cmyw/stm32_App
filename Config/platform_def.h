/********************************************************************************
**
** 文件名:     platform_def.h
** 版权所有:   无
** 文件描述:   该模块主要实现平台相关宏定义
**
*********************************************************************************/


#ifndef __PLATFORM_DEF_H
#define __PLATFORM_DEF_H

#include <stdint.h>

/* 设备类型定义 */
typedef enum {
    DEV_TYPE_I2C = 0,
    DEV_TYPE_SPI,
    DEV_TYPE_UART,
    DEV_TYPE_GPIO,
    DEV_TYPE_ADC,
    DEV_TYPE_TIM,
    DEV_TYPE_MAX
} DeviceType_e;

/* I2C设备定义 */
typedef struct {
    uint8_t i2c_id;
    void *hi2c;             // I2C_HandleTypeDef指针
    uint32_t timeout;
    void (*init)(void);
    void (*deinit)(void);
} I2C_DevDef_t;

/* SPI设备定义 */
typedef struct {
    uint8_t spi_id;
    void *hspi;             // SPI_HandleTypeDef指针
    void *cs_port;          // GPIO_TypeDef指针
    uint16_t cs_pin;
    void (*init)(void);
    void (*deinit)(void);
} SPI_DevDef_t;

/* UART设备定义 */
typedef struct {
    uint8_t uart_id;
    void *huart;            // UART_HandleTypeDef指针
    uint32_t baudrate;
    void (*init)(void);
    void (*deinit)(void);
    void (*rx_callback)(uint8_t data);
} UART_DevDef_t;

/* GPIO设备定义 */
typedef struct {
    void *port;             // GPIO_TypeDef指针
    uint16_t pin;
    uint8_t mode;           // 输入/输出/复用/模拟
    uint8_t pull;           // 上拉/下拉/无
    uint8_t speed;          // 速度
    void (*init)(void);
    void (*deinit)(void);
} GPIO_DevDef_t;

/* ADC设备定义 */
typedef struct {
    uint8_t adc_id;
    void *hadc;             // ADC_HandleTypeDef指针
    uint32_t channel;
    void (*init)(void);
    void (*deinit)(void);
} ADC_DevDef_t;

/* 设备注册表 */
typedef struct {
    const char *dev_name;
    DeviceType_e dev_type;
    union {
        I2C_DevDef_t i2c_dev;
        SPI_DevDef_t spi_dev;
        UART_DevDef_t uart_dev;
        GPIO_DevDef_t gpio_dev;
        ADC_DevDef_t adc_dev;
    } dev;
    uint8_t is_used;
} DeviceRegistry_t;

/* 注册表最大设备数 */
#define MAX_DEVICE_NUM  32

/* 设备注册函数 */
int device_register(const char *name, DeviceType_e type, void *dev_def);
int device_unregister(const char *name);
DeviceRegistry_t* device_find(const char *name);

#endif /* __PLATFORM_DEF_H */
