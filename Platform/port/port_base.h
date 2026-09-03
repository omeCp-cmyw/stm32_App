#ifndef PORT_BASE_H
#define PORT_BASE_H

#include <stdio.h>

/*
 * 平台移植基础接口（芯片相关，收敛所有芯片差异）：
 * 驱动层与OSAL仅依赖本接口，换芯片时只需重写port_base.c。
 */

/*******************************************************************
** 函数名	: port_enter_critical
** 函数描述	: 进入临界区（关全局中断）
** 参数		: 无
** 返回		: 无
********************************************************************/
void port_enter_critical(void);

/*******************************************************************
** 函数名	: port_exit_critical
** 函数描述	: 退出临界区（恢复全局中断）
** 参数		: 无
** 返回		: 无
********************************************************************/
void port_exit_critical(void);

/*******************************************************************
** 函数名	: port_system_reset
** 函数描述	: 系统复位：复位前喂一次看门狗，保证走NVIC复位而非看门狗复位
** 参数		: 无
** 返回		: 无
********************************************************************/
void port_system_reset(void);

#define PORT_ENTER_CRITICAL()   do { port_enter_critical(); } while (0)
#define PORT_EXIT_CRITICAL()    do { port_exit_critical(); } while (0)

/*
 * 驱动层断言：条件不满足时打印出错位置并复位设备。
 * retvalue支持空值（return;），与OSAL_ASSERT用法一致
 */
#define PORT_ASSERT(EXPRESSION, retvalue)                   \
do {                                                        \
    if (!(EXPRESSION)) {                                    \
        printf("<port_assert:file(%s), line(%u)>\r\n",      \
               __FILE__, (unsigned int)__LINE__);           \
        port_system_reset();                                \
        return retvalue;                                    \
    }                                                       \
} while (0)

#endif /* PORT_BASE_H */
