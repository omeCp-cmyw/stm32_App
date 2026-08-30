#ifndef OS_CONFIG_H
#define OS_CONFIG_H

/* 进/出临界区，关闭/打开全局中断 */
#define OS_ENTER_CRITICAL()     do { __disable_irq(); } while (0)
#define OS_EXIT_CRITICAL()      do { __enable_irq(); } while (0)

/* 工程暂无看门狗，喂狗宏置空 */
#define ClearWatchdog()

/* 断言失败时是否串口打印复位原因 */
#define DEBUG_ERR               1

#endif /* OS_CONFIG_H */
