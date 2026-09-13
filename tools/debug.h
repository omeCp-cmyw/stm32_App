/********************************************************************************
**
** 文件名:     debug.h
** 版权所有:   无
** 文件描述:   该模块主要实现网络调试功能接口定义
**
*********************************************************************************/


#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdint.h>
#include <stdio.h>

/* 调试级别定义 */
#define DEBUG_LEVEL_NONE            0
#define DEBUG_LEVEL_ERROR           1
#define DEBUG_LEVEL_WARN            2
#define DEBUG_LEVEL_INFO            3
#define DEBUG_LEVEL_DEBUG           4

/* 当前调试级别 */
#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL                 DEBUG_LEVEL_DEBUG
#endif

/* 调试宏定义 */
#if (DEBUG_LEVEL >= DEBUG_LEVEL_ERROR)
#define DEBUG_ERROR(fmt, ...)       printf("[ERROR][%s:%d] " fmt "\r\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG_ERROR(fmt, ...)
#endif

#if (DEBUG_LEVEL >= DEBUG_LEVEL_WARN)
#define DEBUG_WARN(fmt, ...)        printf("[WARN][%s:%d] " fmt "\r\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG_WARN(fmt, ...)
#endif

#if (DEBUG_LEVEL >= DEBUG_LEVEL_INFO)
#define DEBUG_INFO(fmt, ...)        printf("[INFO] " fmt "\r\n", ##__VA_ARGS__)
#else
#define DEBUG_INFO(fmt, ...)
#endif

#if (DEBUG_LEVEL >= DEBUG_LEVEL_DEBUG)
#define DEBUG_DEBUG(fmt, ...)       printf("[DEBUG] " fmt "\r\n", ##__VA_ARGS__)
#else
#define DEBUG_DEBUG(fmt, ...)
#endif

/* 兼容原有调试宏 */
#define PRINT_DEBUG(fmt, ...)       DEBUG_DEBUG(fmt, ##__VA_ARGS__)
#define PRINT_ERR(fmt, ...)         DEBUG_ERROR(fmt, ##__VA_ARGS__)
#define PRINT_INFO(fmt, ...)        DEBUG_INFO(fmt, ##__VA_ARGS__)

/* 网络调试接口 */
/**
  * @brief  初始化网络调试
  * @retval 0: 成功, 其他: 失败
  */
int debug_net_init(void);

/**
  * @brief  发送调试数据
  * @param  data: 数据指针
  * @param  len: 数据长度
  * @retval 发送的字节数, 负数: 失败
  */
int debug_net_send(const uint8_t *data, uint32_t len);

/**
  * @brief  接收调试数据
  * @param  buffer: 接收缓冲区
  * @param  len: 缓冲区大小
  * @param  timeout: 超时时间（毫秒）
  * @retval 接收的字节数, 0: 超时, 负数: 失败
  */
int debug_net_recv(uint8_t *buffer, uint32_t len, uint32_t timeout);

/**
  * @brief  关闭网络调试连接
  * @retval 无
  */
void debug_net_close(void);

/**
  * @brief  检查网络调试连接状态
  * @retval 1: 已连接, 0: 未连接
  */
int debug_net_is_connected(void);

#endif /* __DEBUG_H */
