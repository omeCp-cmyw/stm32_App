/********************************************************************************
**
** 文件名:     debug.c
** 版权所有:   无
** 文件描述:   该模块主要实现网络调试功能
**
*********************************************************************************/


#include "debug.h"
#include "../Config/system_config.h"
#include "../OSAL/osal.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include <string.h>
#include <errno.h>
#include <stdio.h>

/* 调试连接状态 */
static int debug_socket = -1;
static uint8_t debug_connected = 0;

/* 服务器地址 */
static struct sockaddr_in server_addr;

/*******************************************************************************
** 函数名称    debug_net_init
** 函数说明    初始化网络调试连接
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功, 其他: 失败
*******************************************************************************/
int debug_net_init(void)
{
    int ret;

    /* 创建TCP socket */
    debug_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (debug_socket < 0) {
        DEBUG_ERROR("Create socket failed: %d", errno);
        return -1;
    }

    /* 配置服务器地址 */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DEBUG_SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(
        ((uint32_t)DEBUG_SERVER_IP0 << 24) |
        ((uint32_t)DEBUG_SERVER_IP1 << 16) |
        ((uint32_t)DEBUG_SERVER_IP2 << 8)  |
        (uint32_t)DEBUG_SERVER_IP3
    );

    /* 连接服务器 */
    ret = connect(debug_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        DEBUG_ERROR("Connect to %d.%d.%d.%d:%d failed: %d",
                    DEBUG_SERVER_IP0, DEBUG_SERVER_IP1,
                    DEBUG_SERVER_IP2, DEBUG_SERVER_IP3,
                    DEBUG_SERVER_PORT, errno);
        close(debug_socket);
        debug_socket = -1;
        return -2;
    }

    debug_connected = 1;

    DEBUG_INFO("Connected to debug server %d.%d.%d.%d:%d",
               DEBUG_SERVER_IP0, DEBUG_SERVER_IP1,
               DEBUG_SERVER_IP2, DEBUG_SERVER_IP3,
               DEBUG_SERVER_PORT);

    return 0;
}

/*******************************************************************************
** 函数名称    debug_net_send
** 函数说明    发送调试数据
** 输入参数    data: 数据指针
**             len: 数据长度
** 输出参数    无
** 返回参数    发送的字节数, 负数: 失败
*******************************************************************************/
int debug_net_send(const uint8_t *data, uint32_t len)
{
    int ret;

    if (!debug_connected || debug_socket < 0) {
        return -1;
    }

    ret = send(debug_socket, data, len, 0);
    if (ret < 0) {
        DEBUG_ERROR("Send data failed: %d", errno);
        debug_connected = 0;
        return -2;
    }

    return ret;
}

/*******************************************************************************
** 函数名称    debug_net_recv
** 函数说明    接收调试数据
** 输入参数    buffer: 接收缓冲区
**             len: 缓冲区大小
**             timeout: 超时时间（毫秒）
** 输出参数    无
** 返回参数    接收的字节数, 0: 超时, 负数: 失败
*******************************************************************************/
int debug_net_recv(uint8_t *buffer, uint32_t len, uint32_t timeout)
{
    int ret;
    struct timeval tv;

    if (!debug_connected || debug_socket < 0) {
        return -1;
    }

    /* 设置超时 */
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    setsockopt(debug_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ret = recv(debug_socket, buffer, len, 0);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  /* 超时 */
        }
        DEBUG_ERROR("Receive data failed: %d", errno);
        debug_connected = 0;
        return -2;
    }

    return ret;
}

/*******************************************************************************
** 函数名称    debug_net_close
** 函数说明    关闭网络调试连接
** 输入参数    无
** 输出参数    无
** 返回参数    无
*******************************************************************************/
void debug_net_close(void)
{
    if (debug_socket >= 0) {
        close(debug_socket);
        debug_socket = -1;
    }
    debug_connected = 0;
    DEBUG_INFO("Debug connection closed");
}

/*******************************************************************************
** 函数名称    debug_net_is_connected
** 函数说明    检查网络调试连接状态
** 输入参数    无
** 输出参数    无
** 返回参数    1: 已连接, 0: 未连接
*******************************************************************************/
int debug_net_is_connected(void)
{
    return debug_connected;
}
