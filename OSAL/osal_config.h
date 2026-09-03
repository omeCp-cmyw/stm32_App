#ifndef OSAL_CONFIG_H
#define OSAL_CONFIG_H

/* 软件定时器数量上限 */
#define OSAL_MAX_TIMER          80

/* 时基：由drv_systick提供10us tick，1ms对应的tick数 */
#define OSAL_TICK_US            10
#define OSAL_TICK_MS            (1000 / OSAL_TICK_US)

/* 断言失败时是否串口打印复位原因 */
#define OSAL_DEBUG_ERR          1

#endif /* OSAL_CONFIG_H */
