#include <string.h>
#include "osal.h"
#include "tool_heapmem.h"

/*
********************************************************************************
* 静态堆内存管理：
* 内存池按4字节对齐，块头双向链表管理，分配首适配切块，
* 释放时与前块/后块相邻空闲块合并，抑制碎片。
********************************************************************************
*/

/*
********************************************************************************
* 定义模块配置参数
********************************************************************************
*/
#define _SIGNATURE           0xAA

/*
********************************************************************************
* 定义模块数据结构
********************************************************************************
*/
/* 内存块头，紧邻数据区存放在数据前侧 */
typedef struct blockhead_t {
    struct blockhead_t *prev;                              /* 链表指针 */
    struct blockhead_t *next;                              /* 链表指针 */

    INT8U  signature;                                      /* 固定为_SIGNATURE */
    INT8U  allocated;                                      /* 此区域是否已分配：0空闲，_SIGNATURE已分配 */
    INT16U size;                                           /* 此区域数据区大小 */
} BLOCKHEAD_T;

/*
********************************************************************************
* 定义模块变量
********************************************************************************
*/
static INT32U s_mempool[(SIZE_HEAP_MEM + 3) / 4];          /* 静态内存池，4字节对齐 */
static HEAPMEM_STATISTICS_T s_statictis;

/*******************************************************************
** 函数名	: TOOL_HEAPMEM_Init
** 函数描述	: 初始化堆内存管理模块，将内存池复位为单个空闲块。
** 参数		: 无
** 返回		: 无
********************************************************************/
void TOOL_HEAPMEM_Init(void)
{
    BLOCKHEAD_T *blockptr;

    memset(&s_statictis, 0, sizeof(s_statictis));

    blockptr            = (BLOCKHEAD_T *)s_mempool;                            /* 获取整块内存池的起始地址 */
    blockptr->next      = 0;
    blockptr->prev      = 0;
    blockptr->allocated = FALSE;
    blockptr->signature = _SIGNATURE;
    blockptr->size      = sizeof(s_mempool) - sizeof(BLOCKHEAD_T);             /* 计算内存池中可用的容量大小 */

    s_statictis.blocks++;                                                      /* 初始化内存块数量 */
}

/*******************************************************************
** 函数名	: HEAPMEM_Alloc
** 函数描述	: 申请分配内存，从空闲块中切出并返回数据区地址。
** 参数		: [in] datalen: 申请内存的长度
**			: [in] file:    申请内存的文件名（调试留痕）
**			: [in] line:    申请内存的行号（调试留痕）
** 返回		: 成功返回内存指针，失败返回0
********************************************************************/
void *HEAPMEM_Alloc(INT32U datalen, char *file, INT32U line)
{
    BLOCKHEAD_T *blockptr = (BLOCKHEAD_T *)s_mempool;                          /* 指向内存池的起始位置 */
    BLOCKHEAD_T *newblock;                                                     /* 本次操作后产生的新块（剩余内存块） */

    file = file;
    line = line;

    if ((datalen == 0) || (datalen >= SIZE_HEAP_MEM)) {
        return 0;                                                              /* 申请值非法，无法分配 */
    }

    datalen = ((datalen + 3) / 4) * 4;                                         /* 4字节对齐 */

    while (blockptr != 0) {
        if ((blockptr->allocated == FALSE) && (blockptr->size >= (datalen + sizeof(BLOCKHEAD_T)))) {
            blockptr->allocated = _SIGNATURE;

            newblock            = (BLOCKHEAD_T *)(((INT8U *)blockptr) + sizeof(BLOCKHEAD_T) + datalen);
            newblock->signature = _SIGNATURE;
            newblock->size      = (INT16U)(blockptr->size - datalen - sizeof(BLOCKHEAD_T)); /* 剩余块容量 */
            newblock->allocated = FALSE;
            newblock->prev      = blockptr;                                    /* 新块链接到本次申请块后面 */
            newblock->next      = blockptr->next;                              /* 申请块插入到剩余块之前 */

            if (newblock->next != 0) {
                newblock->next->prev = newblock;
            }

            blockptr->next      = newblock;                                    /* 申请块的下一指针指向剩余块 */
            blockptr->size      = (INT16U)datalen;                             /* 指定本次申请的内存块大小 */
            s_statictis.blocks++;                                              /* 内存块数累加 */
            s_statictis.occupysize += blockptr->size;                          /* 申请容量计入已使用范围 */
            break;
        }
        blockptr = blockptr->next;                                             /* 本块不满足，判断下一块 */
    }

    return (blockptr != 0) ? (((INT8U *)(blockptr)) + sizeof(BLOCKHEAD_T)) : 0; /* 返回数据区地址 */
}

/*******************************************************************
** 函数名	: HEAPMEM_Free
** 函数描述	: 释放内存，归还块并做前后向合并。
** 参数		: [in] sptr: 释放内存的地址（Alloc返回的指针）
**			: [in] file: 释放内存的文件名（调试留痕）
**			: [in] line: 释放内存的行号（调试留痕）
** 返回		: 无
********************************************************************/
void HEAPMEM_Free(void *sptr, char *file, INT32U line)
{
    BLOCKHEAD_T *blockptr      = 0;
    BLOCKHEAD_T *prevblock     = 0;
    BLOCKHEAD_T *backblock     = 0;
    BLOCKHEAD_T *nextbackblock = 0;

    file = file;
    line = line;

    OSAL_ASSERT((sptr != 0), RETURN_VOID);
    blockptr = (BLOCKHEAD_T *)(((INT8U *)(sptr)) - sizeof(BLOCKHEAD_T));       /* 指向块头起始地址 */

    OSAL_ASSERT((blockptr->signature == _SIGNATURE), RETURN_VOID);
    OSAL_ASSERT((blockptr->allocated == _SIGNATURE), RETURN_VOID);

    blockptr->allocated = FALSE;
    s_statictis.occupysize -= blockptr->size;                                  /* 从已使用总量中扣除 */

    prevblock = blockptr->prev;                                                /* 前块 */
    backblock = blockptr->next;                                                /* 后块 */

    if (prevblock != 0) {                                                      /* 前项合并 */
        if (prevblock->allocated == FALSE) {
            prevblock->size += (blockptr->size + sizeof(BLOCKHEAD_T));         /* 本块大小归并到前块 */
            prevblock->next  = backblock;                                      /* 后块与前块串接 */
            if (backblock != 0) {
                backblock->prev = prevblock;
            }
            s_statictis.blocks--;                                              /* 内存块数累减 */
        }
    }

    if (backblock != 0) {                                                      /* 后项合并 */
        if (prevblock != 0) {
            if (prevblock->next == backblock) {
                blockptr = prevblock;                                          /* 已发生前项合并，从前块起合并 */
            }
        }
        backblock     = blockptr->next;                                        /* 后面内存块起始地址 */
        nextbackblock = (backblock != 0) ? backblock->next : 0;                /* 再下一个内存块 */
        if ((backblock != 0) && (backblock->allocated == FALSE)) {
            blockptr->size += (backblock->size + sizeof(BLOCKHEAD_T));         /* 两块大小合并 */
            blockptr->next  = nextbackblock;                                   /* 合并后指向再下一块 */
            if (nextbackblock != 0) {
                nextbackblock->prev = blockptr;
            }
            s_statictis.blocks--;                                              /* 内存块数累减 */
        }
    }

    if ((s_statictis.occupysize == 0) && (s_statictis.blocks != 1)) {
        OSAL_ASSERT((0), RETURN_VOID);                                           /* 全释放后块数必须归1，否则链表损坏 */
    }
}

/*******************************************************************
** 函数名	: TOOL_HEAPMEM_GetStatistics
** 函数描述	: 查询内存池当前使用状态。
** 参数		: 无
** 返回		: 统计信息指针
********************************************************************/
HEAPMEM_STATISTICS_T *TOOL_HEAPMEM_GetStatistics(void)
{
    return &s_statictis;
}

/*******************************************************************
** 函数名	: TOOL_HEAPMEM_GetAddress
** 函数描述	: 获取内存池首地址。
** 参数		: 无
** 返回		: 内存池首地址
********************************************************************/
INT8U *TOOL_HEAPMEM_GetAddress(void)
{
    return (INT8U *)s_mempool;
}
