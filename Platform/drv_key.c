/********************************************************************************
**
** 文件名:     drv_key.c
** 版权所有:   无
** 文件描述:   该模块主要实现按键驱动
**
*********************************************************************************/


#include "drv_key.h"
#include <stdio.h>
#include <string.h>

/* 按键配置表 */
static const Key_Config_t key_config[KEY_ID_MAX] = {
    {KEY_ID_1, NULL, 0, 0, 20, 1000},
    {KEY_ID_2, NULL, 0, 0, 20, 1000},
    {KEY_ID_3, NULL, 0, 0, 20, 1000}
};

/* 按键状态表 */
static KeyState_e key_state[KEY_ID_MAX] = {KEY_STATE_RELEASED};

/* 按键事件表 */
static KeyEvent_e key_event[KEY_ID_MAX] = {KEY_EVENT_NONE};

/* 按键回调函数表 */
static void (*key_callback[KEY_ID_MAX])(KeyEvent_e event) = {NULL};

/*******************************************************************************
** 函数名称    drv_key_init
** 函数说明    初始化按键驱动
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_key_init(void)
{
    (void)key_config;  /* 后续实现时使用 */
    memset(key_state, KEY_STATE_RELEASED, sizeof(key_state));
    memset(key_event, KEY_EVENT_NONE, sizeof(key_event));
    memset(key_callback, 0, sizeof(key_callback));
    
    printf("[DRV_KEY] Key driver initialized\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    drv_key_get_state
** 函数说明    获取按键状态
** 输入参数    id: 按键编号
** 输出参数    无
** 返回参数    按键状态
*******************************************************************************/
int drv_key_get_state(KeyID_e id)
{
    if (id >= KEY_ID_MAX) {
        return -1;
    }
    
    return (int)key_state[id];
}

/*******************************************************************************
** 函数名称    drv_key_get_event
** 函数说明    获取按键事件
** 输入参数    id: 按键编号
** 输出参数    无
** 返回参数    按键事件
*******************************************************************************/
int drv_key_get_event(KeyID_e id)
{
    if (id >= KEY_ID_MAX) {
        return -1;
    }
    
    KeyEvent_e event = key_event[id];
    key_event[id] = KEY_EVENT_NONE;  // 清除事件
    
    return (int)event;
}

/*******************************************************************************
** 函数名称    drv_key_register_callback
** 函数说明    注册按键回调
** 输入参数    id: 按键编号
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_key_register_callback(KeyID_e id, void (*callback)(KeyEvent_e event))
{
    if (id >= KEY_ID_MAX) {
        return -1;
    }
    
    key_callback[id] = callback;
    printf("[DRV_KEY] Register callback for key %d\r\n", id);
    return 0;
}
