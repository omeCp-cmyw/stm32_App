#include "tool_loopbuf.h"
#include "osal.h"

/*
********************************************************************************
* define module variants
********************************************************************************
*/

/* 参考实现关闭本文件优化，避免临界区外并发访问的时序问题 */
#pragma O0

/*******************************************************************
** 函数名	: LP_InitLoopBuffer
** 函数描述	: 初始化环形缓冲。
** 参数		: [in] loop:    环形缓冲控制结构体
**			: [in] memptr:  缓冲首地址
**			: [in] memsize: 缓冲大小
** 返回		: 无
********************************************************************/
void LP_InitLoopBuffer(LOOP_BUF_T *loop, INT8U *memptr, INT32U memsize)
{
    loop->memsize = memsize;
    loop->used    = 0;
    loop->bptr    = memptr;
    loop->eptr    = memptr + memsize;
    loop->wptr    = loop->bptr;
    loop->rptr    = loop->bptr;
}

/*******************************************************************
** 函数名	: LP_ClearLoopBuffer
** 函数描述	: 恢复缓冲为初始状态。
** 参数		: [in] loop: 环形缓冲控制结构体
** 返回		: 无
********************************************************************/
void LP_ClearLoopBuffer(LOOP_BUF_T *loop)
{
    osal_enter_critical();
    loop->used = 0;
    loop->rptr = loop->bptr;
    loop->wptr = loop->bptr;
    osal_exit_critical();
}

/*******************************************************************
** 函数名	: LP_GetLoopBufferStartPtr
** 函数描述	: 获取缓冲首地址。
** 参数		: [in] loop: 环形缓冲控制结构体
** 返回		: 缓冲首地址
********************************************************************/
INT8U *LP_GetLoopBufferStartPtr(LOOP_BUF_T *loop)
{
    return loop->bptr;
}

/*******************************************************************
** 函数名	: LP_WriteLoopBuffer
** 函数描述	: 写入一个字节，带临界区保护。
** 参数		: [in] loop:   环形缓冲控制结构体
**			: [in] indata: 字节数据
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN LP_WriteLoopBuffer(LOOP_BUF_T *loop, INT8U indata)
{
    if (loop == 0) {
        return FALSE;
    }

    osal_enter_critical();
    if (loop->used >= loop->memsize) {                                 /* 缓冲满 */
        osal_exit_critical();
        return FALSE;
    }

    *loop->wptr++ = indata;
    if (loop->wptr >= loop->eptr) {
        loop->wptr = loop->bptr;
    }

    loop->used++;
    osal_exit_critical();
    return TRUE;
}

/*******************************************************************
** 函数名	: LP_WriteLoopBuffer_INT
** 函数描述	: 写入一个字节，中断内使用无临界区保护。
** 参数		: [in] loop:   环形缓冲控制结构体
**			: [in] indata: 字节数据
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN LP_WriteLoopBuffer_INT(LOOP_BUF_T *loop, INT8U indata)
{
    if (loop == 0) {
        return FALSE;
    }

    if (loop->used >= loop->memsize) {                                 /* 缓冲满 */
        return FALSE;
    }

    *loop->wptr++ = indata;
    if (loop->wptr >= loop->eptr) {
        loop->wptr = loop->bptr;
    }

    loop->used++;
    return TRUE;
}

/*******************************************************************
** 函数名	: LP_WriteBlockLoopBuffer
** 函数描述	: 写入一段数据，带临界区保护。
** 参数		: [in] loop: 环形缓冲控制结构体
**			: [in] bptr: 数据块指针
**			: [in] len:  数据块字节数
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN LP_WriteBlockLoopBuffer(LOOP_BUF_T *loop, INT8U *bptr, INT32U len)
{
    INT32U i, temp;

    osal_enter_critical();
    temp = loop->memsize - loop->used;
    if (len > temp) {                                                  /* 剩余空间不足 */
        osal_exit_critical();
        return FALSE;
    }

    for (i = 0; i < len; i++) {
        *loop->wptr++ = *bptr++;
        if (loop->wptr >= loop->eptr) {
            loop->wptr = loop->bptr;
        }
        loop->used++;
    }
    osal_exit_critical();
    return TRUE;
}

/*******************************************************************
** 函数名	: LP_WriteBlockLoopBuffer_INT
** 函数描述	: 写入一段数据，中断内使用无临界区保护。
** 参数		: [in] loop: 环形缓冲控制结构体
**			: [in] bptr: 数据块指针
**			: [in] len:  数据块字节数
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN LP_WriteBlockLoopBuffer_INT(LOOP_BUF_T *loop, INT8U *bptr, INT32U len)
{
    INT32U i, temp;

    temp = loop->memsize - loop->used;
    if (len > temp) {                                                  /* 剩余空间不足 */
        return FALSE;
    }

    for (i = 0; i < len; i++) {
        *loop->wptr++ = *bptr++;
        if (loop->wptr >= loop->eptr) {
            loop->wptr = loop->bptr;
        }
        loop->used++;
    }
    return TRUE;
}

/*******************************************************************
** 函数名	: LP_ReadLoopBuffer
** 函数描述	: 读取一个字节，带临界区保护。
** 参数		: [in] loop: 环形缓冲控制结构体
** 返回		: 成功返回数据，失败返回-1
********************************************************************/
INT32S LP_ReadLoopBuffer(LOOP_BUF_T *loop)
{
    INT32S ret;

    osal_enter_critical();
    if (loop->used == 0) {                                             /* 缓冲空 */
        osal_exit_critical();
        return -1;
    }

    ret = *loop->rptr++;
    if (loop->rptr >= loop->eptr) {
        loop->rptr = loop->bptr;
    }

    loop->used--;
    osal_exit_critical();
    return ret;
}

/*******************************************************************
** 函数名	: LP_ReadLoopBuffer_INT
** 函数描述	: 读取一个字节，中断内使用无临界区保护。
** 参数		: [in] loop: 环形缓冲控制结构体
** 返回		: 成功返回数据，失败返回-1
********************************************************************/
INT32S LP_ReadLoopBuffer_INT(LOOP_BUF_T *loop)
{
    INT32S ret;

    if (loop->used == 0) {                                             /* 缓冲空 */
        return -1;
    }

    ret = *loop->rptr++;
    if (loop->rptr >= loop->eptr) {
        loop->rptr = loop->bptr;
    }

    loop->used--;
    return ret;
}

/*******************************************************************
** 函数名	: LP_ReadBlockLoopBuffer
** 函数描述	: 读取一段数据，带临界区保护。
** 参数		: [in] loop: 环形缓冲控制结构体
**			: [in] bptr: 数据块指针
**			: [in] len:  数据块字节数
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN LP_ReadBlockLoopBuffer(LOOP_BUF_T *loop, INT8U *bptr, INT32U len)
{
    if (len == 0) {
        return FALSE;
    }

    osal_enter_critical();
    if (len > loop->used) {                                            /* 数据不足 */
        osal_exit_critical();
        return FALSE;
    }
    for (; len > 0; len--) {
        *bptr++ = *loop->rptr++;
        if (loop->rptr >= loop->eptr) {
            loop->rptr = loop->bptr;
        }

        if (loop->used == 0) {                                         /* 计数异常，复位缓冲 */
            loop->rptr = loop->wptr = loop->bptr;
            osal_exit_critical();
            return FALSE;
        } else {
            loop->used--;
        }
    }

    osal_exit_critical();
    return TRUE;
}

/*******************************************************************
** 函数名	: LP_ReadBlockLoopBuffer_INT
** 函数描述	: 读取一段数据，中断内使用无临界区保护。
** 参数		: [in] loop: 环形缓冲控制结构体
**			: [in] bptr: 数据块指针
**			: [in] len:  数据块字节数
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN LP_ReadBlockLoopBuffer_INT(LOOP_BUF_T *loop, INT8U *bptr, INT32U len)
{
    if (len == 0) {
        return FALSE;
    }

    if (len > loop->used) {                                            /* 数据不足 */
        return FALSE;
    }
    for (; len > 0; len--) {
        *bptr++ = *loop->rptr++;
        if (loop->rptr >= loop->eptr) {
            loop->rptr = loop->bptr;
        }

        if (loop->used == 0) {                                         /* 计数异常，复位缓冲 */
            loop->rptr = loop->wptr = loop->bptr;
            return FALSE;
        } else {
            loop->used--;
        }
    }

    return TRUE;
}

/*******************************************************************
** 函数名	: LP_ReadBlockLoopBufferOnly
** 函数描述	: 读取一段数据但不移动读指针（只读不取），带临界区保护。
**			先在结构体副本上读取，不影响原缓冲读指针。
** 参数		: [in] loop: 环形缓冲控制结构体
**			: [in] bptr: 数据块指针
**			: [in] len:  数据块字节数
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN LP_ReadBlockLoopBufferOnly(LOOP_BUF_T *loop, INT8U *bptr, INT32U len)
{
    LOOP_BUF_T tmploop;

    if (len == 0) {
        return FALSE;
    }

    osal_enter_critical();
    tmploop.memsize = loop->memsize;
    tmploop.used    = loop->used;
    tmploop.bptr    = loop->bptr;
    tmploop.eptr    = loop->eptr;
    tmploop.wptr    = loop->wptr;
    tmploop.rptr    = loop->rptr;
    loop = &tmploop;
    if (len > loop->used) {                                            /* 数据不足 */
        osal_exit_critical();
        return FALSE;
    }
    for (; len > 0; len--) {
        *bptr++ = *loop->rptr++;
        if (loop->rptr >= loop->eptr) {
            loop->rptr = loop->bptr;
        }

        if (loop->used == 0) {                                         /* 计数异常，复位缓冲 */
            loop->rptr = loop->wptr = loop->bptr;
            osal_exit_critical();
            return FALSE;
        } else {
            loop->used--;
        }
    }

    osal_exit_critical();
    return TRUE;
}

/*******************************************************************
** 函数名	: LP_ReadBlockLoopBufferOnly_INT
** 函数描述	: 读取一段数据但不移动读指针，中断内使用无临界区保护。
** 参数		: [in] loop: 环形缓冲控制结构体
**			: [in] bptr: 数据块指针
**			: [in] len:  数据块字节数
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN LP_ReadBlockLoopBufferOnly_INT(LOOP_BUF_T *loop, INT8U *bptr, INT32U len)
{
    LOOP_BUF_T tmploop;

    if (len == 0) {
        return FALSE;
    }

    tmploop.memsize = loop->memsize;
    tmploop.used    = loop->used;
    tmploop.bptr    = loop->bptr;
    tmploop.eptr    = loop->eptr;
    tmploop.wptr    = loop->wptr;
    tmploop.rptr    = loop->rptr;
    loop = &tmploop;
    if (len > loop->used) {                                            /* 数据不足 */
        return FALSE;
    }
    for (; len > 0; len--) {
        *bptr++ = *loop->rptr++;
        if (loop->rptr >= loop->eptr) {
            loop->rptr = loop->bptr;
        }

        if (loop->used == 0) {                                         /* 计数异常，复位缓冲 */
            loop->rptr = loop->wptr = loop->bptr;
            return FALSE;
        } else {
            loop->used--;
        }
    }

    return TRUE;
}

/*******************************************************************
** 函数名	: LP_LeftOfLoopBuffer
** 函数描述	: 获取缓冲剩余空间，带临界区保护。
** 参数		: [in] loop: 环形缓冲控制结构体
** 返回		: 剩余空间字节数
********************************************************************/
INT32U LP_LeftOfLoopBuffer(LOOP_BUF_T *loop)
{
    INT32U temp;

    osal_enter_critical();
    temp = loop->memsize - loop->used;
    osal_exit_critical();

    return temp;
}

/*******************************************************************
** 函数名	: LP_LeftOfLoopBuffer_INT
** 函数描述	: 获取缓冲剩余空间，中断内使用无临界区保护。
** 参数		: [in] loop: 环形缓冲控制结构体
** 返回		: 剩余空间字节数
********************************************************************/
INT32U LP_LeftOfLoopBuffer_INT(LOOP_BUF_T *loop)
{
    INT32U temp;

    temp = loop->memsize - loop->used;

    return temp;
}

/*******************************************************************
** 函数名	: LP_UsedOfLoopBuffer
** 函数描述	: 获取缓冲已存字节数，带临界区保护。
** 参数		: [in] loop: 环形缓冲控制结构体
** 返回		: 已存字节数
********************************************************************/
INT32U LP_UsedOfLoopBuffer(LOOP_BUF_T *loop)
{
    INT32U temp;

    osal_enter_critical();
    temp = loop->used;
    osal_exit_critical();

    return temp;
}

/*******************************************************************
** 函数名	: LP_UsedOfLoopBuffer_INT
** 函数描述	: 获取缓冲已存字节数，中断内使用无临界区保护。
** 参数		: [in] loop: 环形缓冲控制结构体
** 返回		: 已存字节数
********************************************************************/
INT32U LP_UsedOfLoopBuffer_INT(LOOP_BUF_T *loop)
{
    INT32U temp;

    temp = loop->used;

    return temp;
}
