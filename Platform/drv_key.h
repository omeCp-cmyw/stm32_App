/********************************************************************************
**
** 文件名:     drv_key.h
** 版权所有:   无
** 文件描述:   该模块主要实现按键驱动接口定义
**
*********************************************************************************/


#ifndef __DRV_KEY_H
#define __DRV_KEY_H

#include <stdint.h>

/* 按键编号 */
typedef enum {
    KEY_ID_1 = 0,
    KEY_ID_2,
    KEY_ID_3,
    KEY_ID_MAX
} KeyID_e;

/* 按键状态 */
typedef enum {
    KEY_STATE_RELEASED = 0,
    KEY_STATE_PRESSED,
    KEY_STATE_LONG_PRESSED
} KeyState_e;

/* 按键事件 */
typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_PRESS,
    KEY_EVENT_RELEASE,
    KEY_EVENT_LONG_PRESS,
    KEY_EVENT_CLICK,
    KEY_EVENT_DOUBLE_CLICK
} KeyEvent_e;

/* 按键配置 */
typedef struct {
    KeyID_e id;
    void *port;             // GPIO端口
    uint16_t pin;
    uint8_t active_level;   // 有效电平: 0-低电平有效, 1-高电平有效
    uint16_t debounce_time; // 消抖时间(ms)
    uint16_t long_press_time; // 长按时间(ms)
} Key_Config_t;

/* 按键驱动接口 */
int drv_key_init(void);
int drv_key_get_state(KeyID_e id);
int drv_key_get_event(KeyID_e id);
int drv_key_register_callback(KeyID_e id, void (*callback)(KeyEvent_e event));

#endif /* __DRV_KEY_H */
