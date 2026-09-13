/********************************************************************************
**
** 文件名:     osal.c
** 版权所有:   无
** 文件描述:   该模块主要实现操作系统抽象层
**
*********************************************************************************/


#include "osal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include <stdio.h>

/*******************************************************************************
** 函数名称    osal_task_create
** 函数说明    创建任务
** 输入参数    task: 任务句柄指针
**             name: 任务名称
**             entry: 任务入口函数
**             param: 任务参数
**             stack_size: 任务栈大小（字）
**             priority: 任务优先级
** 输出参数    无
** 返回参数    0: 成功, 其他: 失败
*******************************************************************************/
int osal_task_create(osal_task_t *task, const char *name,
                     void (*entry)(void*), void *param,
                     uint32_t stack_size, uint32_t priority)
{
    TaskHandle_t handle;
    BaseType_t ret;

    ret = xTaskCreate((TaskFunction_t)entry,
                      (const char*)name,
                      (uint16_t)stack_size,
                      (void*)param,
                      (UBaseType_t)priority,
                      &handle);

    if (ret == pdPASS) {
        if (task != NULL) {
            *task = (osal_task_t)handle;
        }
        return OSAL_OK;
    }

    return OSAL_ERROR;
}

/*******************************************************************************
** 函数名称    osal_task_delete
** 函数说明    删除任务
** 输入参数    task: 任务句柄
** 输出参数    无
** 返回参数    无
*******************************************************************************/
void osal_task_delete(osal_task_t task)
{
    if (task != NULL) {
        vTaskDelete((TaskHandle_t)task);
    } else {
        vTaskDelete(NULL);
    }
}

/*******************************************************************************
** 函数名称    osal_task_delay
** 函数说明    任务延时
** 输入参数    ms: 延时时间（毫秒）
** 输出参数    无
** 返回参数    无
*******************************************************************************/
void osal_task_delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/*******************************************************************************
** 函数名称    osal_queue_create
** 函数说明    创建消息队列
** 输入参数    size: 队列大小
**             item_size: 每个消息的大小
** 输出参数    无
** 返回参数    队列句柄, NULL: 失败
*******************************************************************************/
osal_queue_t osal_queue_create(uint32_t size, uint32_t item_size)
{
    QueueHandle_t queue;

    queue = xQueueCreate((UBaseType_t)size, (UBaseType_t)item_size);

    return (osal_queue_t)queue;
}

/*******************************************************************************
** 函数名称    osal_queue_send
** 函数说明    发送消息到队列
** 输入参数    queue: 队列句柄
**             item: 消息指针
**             timeout: 超时时间
** 输出参数    无
** 返回参数    0: 成功, 其他: 失败
*******************************************************************************/
int osal_queue_send(osal_queue_t queue, const void *item, uint32_t timeout)
{
    TickType_t ticks;
    BaseType_t ret;

    if (timeout == OSAL_WAIT_FOREVER) {
        ticks = portMAX_DELAY;
    } else {
        ticks = pdMS_TO_TICKS(timeout);
    }

    ret = xQueueSend((QueueHandle_t)queue, item, ticks);

    if (ret == pdPASS) {
        return OSAL_OK;
    }

    return OSAL_TIMEOUT;
}

/*******************************************************************************
** 函数名称    osal_queue_recv
** 函数说明    从队列接收消息
** 输入参数    queue: 队列句柄
**             item: 消息缓冲区
**             timeout: 超时时间
** 输出参数    无
** 返回参数    0: 成功, 其他: 失败
*******************************************************************************/
int osal_queue_recv(osal_queue_t queue, void *item, uint32_t timeout)
{
    TickType_t ticks;
    BaseType_t ret;

    if (timeout == OSAL_WAIT_FOREVER) {
        ticks = portMAX_DELAY;
    } else {
        ticks = pdMS_TO_TICKS(timeout);
    }

    ret = xQueueReceive((QueueHandle_t)queue, item, ticks);

    if (ret == pdPASS) {
        return OSAL_OK;
    }

    return OSAL_TIMEOUT;
}

/*******************************************************************************
** 函数名称    osal_sem_create
** 函数说明    创建信号量
** 输入参数    无
** 输出参数    无
** 返回参数    信号量句柄, NULL: 失败
*******************************************************************************/
osal_sem_t osal_sem_create(void)
{
    SemaphoreHandle_t sem;

    sem = xSemaphoreCreateBinary();

    return (osal_sem_t)sem;
}

/*******************************************************************************
** 函数名称    osal_sem_wait
** 函数说明    等待信号量
** 输入参数    sem: 信号量句柄
**             timeout: 超时时间
** 输出参数    无
** 返回参数    0: 成功, 其他: 失败
*******************************************************************************/
int osal_sem_wait(osal_sem_t sem, uint32_t timeout)
{
    TickType_t ticks;
    BaseType_t ret;

    if (timeout == OSAL_WAIT_FOREVER) {
        ticks = portMAX_DELAY;
    } else {
        ticks = pdMS_TO_TICKS(timeout);
    }

    ret = xSemaphoreTake((SemaphoreHandle_t)sem, ticks);

    if (ret == pdPASS) {
        return OSAL_OK;
    }

    return OSAL_TIMEOUT;
}

/*******************************************************************************
** 函数名称    osal_sem_post
** 函数说明    释放信号量
** 输入参数    sem: 信号量句柄
** 输出参数    无
** 返回参数    无
*******************************************************************************/
void osal_sem_post(osal_sem_t sem)
{
    xSemaphoreGive((SemaphoreHandle_t)sem);
}

/*******************************************************************************
** 函数名称    osal_mutex_create
** 函数说明    创建互斥锁
** 输入参数    无
** 输出参数    无
** 返回参数    互斥锁句柄, NULL: 失败
*******************************************************************************/
osal_mutex_t osal_mutex_create(void)
{
    SemaphoreHandle_t mutex;

    mutex = xSemaphoreCreateMutex();

    return (osal_mutex_t)mutex;
}

/*******************************************************************************
** 函数名称    osal_mutex_lock
** 函数说明    获取互斥锁
** 输入参数    mutex: 互斥锁句柄
** 输出参数    无
** 返回参数    无
*******************************************************************************/
void osal_mutex_lock(osal_mutex_t mutex)
{
    xSemaphoreTake((SemaphoreHandle_t)mutex, portMAX_DELAY);
}

/*******************************************************************************
** 函数名称    osal_mutex_unlock
** 函数说明    释放互斥锁
** 输入参数    mutex: 互斥锁句柄
** 输出参数    无
** 返回参数    无
*******************************************************************************/
void osal_mutex_unlock(osal_mutex_t mutex)
{
    xSemaphoreGive((SemaphoreHandle_t)mutex);
}

/*******************************************************************************
** 函数名称    osal_get_tick
** 函数说明    获取系统滴答计数
** 输入参数    无
** 输出参数    无
** 返回参数    滴答计数
*******************************************************************************/
uint32_t osal_get_tick(void)
{
    return xTaskGetTickCount();
}

/*******************************************************************************
** 函数名称    osal_get_time_ms
** 函数说明    获取系统时间（毫秒）
** 输入参数    无
** 输出参数    无
** 返回参数    时间（毫秒）
*******************************************************************************/
uint32_t osal_get_time_ms(void)
{
    return (xTaskGetTickCount() * 1000) / configTICK_RATE_HZ;
}
