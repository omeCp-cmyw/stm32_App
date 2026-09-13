/********************************************************************************
**
** 文件名:     osal.h
** 版权所有:   无
** 文件描述:   该模块主要实现操作系统抽象层接口定义
**
*********************************************************************************/


#ifndef __OSAL_H
#define __OSAL_H

#include <stdint.h>

/* OSAL类型定义 */
typedef void* osal_task_t;
typedef void* osal_queue_t;
typedef void* osal_sem_t;
typedef void* osal_mutex_t;

/* 错误码定义 */
#define OSAL_OK                 0
#define OSAL_ERROR              (-1)
#define OSAL_TIMEOUT            (-2)
#define OSAL_NOMEM              (-3)

/* 超时定义 */
#define OSAL_WAIT_FOREVER       0xFFFFFFFF
#define OSAL_NO_WAIT            0

/**
  * @brief  创建任务
  * @param  task: 任务句柄指针
  * @param  name: 任务名称
  * @param  entry: 任务入口函数
  * @param  param: 任务参数
  * @param  stack_size: 任务栈大小（字）
  * @param  priority: 任务优先级
  * @retval 0: 成功, 其他: 失败
  */
int osal_task_create(osal_task_t *task, const char *name,
                     void (*entry)(void*), void *param,
                     uint32_t stack_size, uint32_t priority);

/**
  * @brief  删除任务
  * @param  task: 任务句柄
  * @retval 无
  */
void osal_task_delete(osal_task_t task);

/**
  * @brief  任务延时
  * @param  ms: 延时时间（毫秒）
  * @retval 无
  */
void osal_task_delay(uint32_t ms);

/**
  * @brief  创建消息队列
  * @param  size: 队列大小
  * @param  item_size: 每个消息的大小
  * @retval 队列句柄, NULL: 失败
  */
osal_queue_t osal_queue_create(uint32_t size, uint32_t item_size);

/**
  * @brief  发送消息到队列
  * @param  queue: 队列句柄
  * @param  item: 消息指针
  * @param  timeout: 超时时间
  * @retval 0: 成功, 其他: 失败
  */
int osal_queue_send(osal_queue_t queue, const void *item, uint32_t timeout);

/**
  * @brief  从队列接收消息
  * @param  queue: 队列句柄
  * @param  item: 消息缓冲区
  * @param  timeout: 超时时间
  * @retval 0: 成功, 其他: 失败
  */
int osal_queue_recv(osal_queue_t queue, void *item, uint32_t timeout);

/**
  * @brief  创建信号量
  * @retval 信号量句柄, NULL: 失败
  */
osal_sem_t osal_sem_create(void);

/**
  * @brief  等待信号量
  * @param  sem: 信号量句柄
  * @param  timeout: 超时时间
  * @retval 0: 成功, 其他: 失败
  */
int osal_sem_wait(osal_sem_t sem, uint32_t timeout);

/**
  * @brief  释放信号量
  * @param  sem: 信号量句柄
  * @retval 无
  */
void osal_sem_post(osal_sem_t sem);

/**
  * @brief  创建互斥锁
  * @retval 互斥锁句柄, NULL: 失败
  */
osal_mutex_t osal_mutex_create(void);

/**
  * @brief  获取互斥锁
  * @param  mutex: 互斥锁句柄
  * @retval 无
  */
void osal_mutex_lock(osal_mutex_t mutex);

/**
  * @brief  释放互斥锁
  * @param  mutex: 互斥锁句柄
  * @retval 无
  */
void osal_mutex_unlock(osal_mutex_t mutex);

/**
  * @brief  获取系统滴答计数
  * @retval 滴答计数
  */
uint32_t osal_get_tick(void);

/**
  * @brief  获取系统时间（毫秒）
  * @retval 时间（毫秒）
  */
uint32_t osal_get_time_ms(void);

#endif /* __OSAL_H */
