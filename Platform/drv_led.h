/********************************************************************************
**
** 文件名:     drv_led.h
** 版权所有:   无
** 文件描述:   该模块主要实现LED驱动接口定义
**
*********************************************************************************/


#ifndef __DRV_LED_H
#define __DRV_LED_H

#include <stdint.h>

/* LED编号 */
typedef enum {
    LED_ID_1 = 0,
    LED_ID_2,
    LED_ID_3,
    LED_ID_MAX
} LED_ID_e;

/* LED状态 */
typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_ON,
    LED_STATE_TOGGLE
} LED_State_e;

/* LED配置 */
typedef struct {
    LED_ID_e id;
    void *port;             // GPIO端口
    uint16_t pin;
    uint8_t active_level;   // 有效电平: 0-低电平有效, 1-高电平有效
} LED_Config_t;

/* LED驱动接口 */
int drv_led_init(void);
int drv_led_on(LED_ID_e id);
int drv_led_off(LED_ID_e id);
int drv_led_toggle(LED_ID_e id);
int drv_led_get_state(LED_ID_e id);

#endif /* __DRV_LED_H */
