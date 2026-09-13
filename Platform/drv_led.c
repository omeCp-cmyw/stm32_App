/********************************************************************************
**
** 文件名:     drv_led.c
** 版权所有:   无
** 文件描述:   该模块主要实现LED驱动
**
*********************************************************************************/


#include "drv_led.h"
#include <stdio.h>

/* LED配置表 */
static const LED_Config_t led_config[LED_ID_MAX] = {
    {LED_ID_1, NULL, 0, 0},
    {LED_ID_2, NULL, 0, 0},
    {LED_ID_3, NULL, 0, 0}
};

/* LED状态表 */
static LED_State_e led_state[LED_ID_MAX] = {LED_STATE_OFF};

/*******************************************************************************
** 函数名称    drv_led_init
** 函数说明    初始化LED驱动
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_led_init(void)
{
    (void)led_config;  /* 后续实现时使用 */
    printf("[DRV_LED] LED driver initialized\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    drv_led_on
** 函数说明    点亮LED
** 输入参数    id: LED编号
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_led_on(LED_ID_e id)
{
    if (id >= LED_ID_MAX) {
        return -1;
    }
    
    led_state[id] = LED_STATE_ON;
    printf("[DRV_LED] LED %d ON\r\n", id);
    return 0;
}

/*******************************************************************************
** 函数名称    drv_led_off
** 函数说明    熄灭LED
** 输入参数    id: LED编号
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_led_off(LED_ID_e id)
{
    if (id >= LED_ID_MAX) {
        return -1;
    }
    
    led_state[id] = LED_STATE_OFF;
    printf("[DRV_LED] LED %d OFF\r\n", id);
    return 0;
}

/*******************************************************************************
** 函数名称    drv_led_toggle
** 函数说明    翻转LED
** 输入参数    id: LED编号
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_led_toggle(LED_ID_e id)
{
    if (id >= LED_ID_MAX) {
        return -1;
    }
    
    led_state[id] = (led_state[id] == LED_STATE_OFF) ? LED_STATE_ON : LED_STATE_OFF;
    printf("[DRV_LED] LED %d TOGGLE\r\n", id);
    return 0;
}

/*******************************************************************************
** 函数名称    drv_led_get_state
** 函数说明    获取LED状态
** 输入参数    id: LED编号
** 输出参数    无
** 返回参数    LED状态
*******************************************************************************/
int drv_led_get_state(LED_ID_e id)
{
    if (id >= LED_ID_MAX) {
        return -1;
    }
    
    return (int)led_state[id];
}
