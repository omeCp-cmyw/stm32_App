/********************************************************************************
**
** 文件名:     drv_manager.c
** 版权所有:   无
** 文件描述:   该模块主要实现驱动注册与管理
**
*********************************************************************************/


#include "drv_manager.h"
#include <string.h>
#include <stdio.h>

/* 驱动注册表 */
static const drv_ops_t *drv_table[16] = {0};
static uint8_t drv_count = 0;

/*******************************************************************************
** 函数名称    drv_manager_init
** 函数说明    初始化驱动管理器
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_manager_init(void)
{
    memset(drv_table, 0, sizeof(drv_table));
    drv_count = 0;
    
    printf("[DRV_MANAGER] Driver manager initialized\r\n");
    
    return 0;
}

/*******************************************************************************
** 函数名称    drv_register
** 函数说明    注册驱动
** 输入参数    name: 驱动名称
** 输出参数    无
** 返回参数    0: 成功, -1: 失败
*******************************************************************************/
int drv_register(const char *name, const drv_ops_t *ops)
{
    if (name == NULL || ops == NULL) {
        return -1;
    }
    
    if (drv_count >= 16) {
        printf("[DRV_MANAGER] Driver table full\r\n");
        return -1;
    }
    
    /* 检查是否已注册 */
    for (int i = 0; i < drv_count; i++) {
        if (strcmp(drv_table[i]->name, name) == 0) {
            printf("[DRV_MANAGER] Driver %s already registered\r\n", name);
            return -1;
        }
    }
    
    drv_table[drv_count] = ops;
    drv_count++;
    
    printf("[DRV_MANAGER] Driver %s registered\r\n", name);
    
    return 0;
}

/*******************************************************************************
** 函数名称    drv_unregister
** 函数说明    注销驱动
** 输入参数    name: 驱动名称
** 输出参数    无
** 返回参数    0: 成功, -1: 失败
*******************************************************************************/
int drv_unregister(const char *name)
{
    if (name == NULL) {
        return -1;
    }
    
    for (int i = 0; i < drv_count; i++) {
        if (strcmp(drv_table[i]->name, name) == 0) {
            /* 前移后续驱动 */
            for (int j = i; j < drv_count - 1; j++) {
                drv_table[j] = drv_table[j + 1];
            }
            drv_table[drv_count - 1] = NULL;
            drv_count--;
            
            printf("[DRV_MANAGER] Driver %s unregistered\r\n", name);
            return 0;
        }
    }
    
    printf("[DRV_MANAGER] Driver %s not found\r\n", name);
    return -1;
}

/*******************************************************************************
** 函数名称    drv_find
** 函数说明    查找驱动
** 输入参数    name: 驱动名称
** 输出参数    无
** 返回参数    驱动操作集指针, NULL: 未找到
*******************************************************************************/
const drv_ops_t* drv_find(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    
    for (int i = 0; i < drv_count; i++) {
        if (strcmp(drv_table[i]->name, name) == 0) {
            return drv_table[i];
        }
    }
    
    return NULL;
}
