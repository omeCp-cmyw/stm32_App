/********************************************************************************
**
** 文件名:     drv_manager.h
** 版权所有:   无
** 文件描述:   该模块主要实现驱动注册与管理接口定义
**
*********************************************************************************/


#ifndef __DRV_MANAGER_H
#define __DRV_MANAGER_H

#include <stdint.h>

/* 驱动操作集 */
typedef struct {
    const char *name;
    void (*init)(void);
    void (*deinit)(void);
    int (*read)(uint8_t dev_id, uint8_t *data, uint16_t len);
    int (*write)(uint8_t dev_id, const uint8_t *data, uint16_t len);
    int (*ioctl)(uint8_t dev_id, uint32_t cmd, void *arg);
} drv_ops_t;

/* 驱动注册宏 */
#define DRV_REGISTER(type, ops) \
    static const drv_ops_t* const type##_ops __attribute__((used)) = &(ops)

/* 驱动管理器接口 */
int drv_manager_init(void);
int drv_register(const char *name, const drv_ops_t *ops);
int drv_unregister(const char *name);
const drv_ops_t* drv_find(const char *name);

/* 各驱动初始化接口 */
int drv_led_init(void);
int drv_key_init(void);
int drv_sensor_init(void);
int drv_camera_init(void);
int drv_lcd_init(void);

#endif /* __DRV_MANAGER_H */
