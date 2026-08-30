#include "tool_stream.h"

/*
********************************************************************************
* 数据流管理（移植自野火yx_stream）：
* STREAM_T管理一段线性内存的顺序读/写指针，写入超限自动截断保护，
* 读取越界返回0保护；另提供一块全局共享流缓存供组帧临时使用。
********************************************************************************
*/

/*
********************************************************************************
* 定义模块变量
********************************************************************************
*/
#define MAX_STREAM_SIZE      1024                        /* 全局共享流缓存大小 */

static INT8U s_tempbuf[MAX_STREAM_SIZE];
static STREAM_T s_wstrm;

/*******************************************************************
** 函数名	: TOOL_STRM_Init
** 函数描述	: 初始化数据流。
** 参数		: [in] sp:     数据流
**			: [in] bp:     数据流所管理内存地址
**			: [in] maxlen: 数据流所管理内存字节数
** 返回		: 成功返回true，失败返回false
********************************************************************/
BOOLEAN TOOL_STRM_Init(STREAM_T *sp, void *bp, INT32U maxlen)
{
    if ((sp == 0) || (bp == 0)) {
        return FALSE;
    }

    sp->len      = 0;
    sp->maxlen   = maxlen;
    sp->curptr   = bp;
    sp->startptr = bp;
    return TRUE;
}

/*******************************************************************
** 函数名	: TOOL_STRM_GetLeftLen
** 函数描述	: 获取数据流中剩余的可用字节数。
** 参数		: [in] sp: 数据流
** 返回		: 数据流剩余的可用字节数
********************************************************************/
INT32U TOOL_STRM_GetLeftLen(STREAM_T *sp)
{
    if (sp->maxlen >= sp->len) {
        return (sp->maxlen - sp->len);
    } else {
        return 0;
    }
}

/*******************************************************************
** 函数名	: TOOL_STRM_GetLen
** 函数描述	: 获取数据流中已用字节数。
** 参数		: [in] sp: 数据流
** 返回		: 数据流已用字节数
********************************************************************/
INT32U TOOL_STRM_GetLen(STREAM_T *sp)
{
    return (sp->len);
}

/*******************************************************************
** 函数名	: TOOL_STRM_GetMaxLen
** 函数描述	: 获取数据流缓存总长度。
** 参数		: [in] sp: 数据流
** 返回		: 数据流的最大长度
********************************************************************/
INT32U TOOL_STRM_GetMaxLen(STREAM_T *sp)
{
    return (sp->maxlen);
}

/*******************************************************************
** 函数名	: TOOL_STRM_GetPtr
** 函数描述	: 获取数据流当前读/写指针。
** 参数		: [in] sp: 数据流
** 返回		: 当前读/写指针
********************************************************************/
void *TOOL_STRM_GetPtr(STREAM_T *sp)
{
    return (sp->curptr);
}

/*******************************************************************
** 函数名	: TOOL_STRM_GetStartPtr
** 函数描述	: 获取数据流所管理内存的地址。
** 参数		: [in] sp: 数据流
** 返回		: 所管理内存地址
********************************************************************/
void *TOOL_STRM_GetStartPtr(STREAM_T *sp)
{
    return (sp->startptr);
}

/*******************************************************************
** 函数名	: TOOL_STRM_MovPtr
** 函数描述	: 移动数据流中读/写指针。
** 参数		: [in] sp:  数据流
**			: [in] len: 移动字节数
** 返回		: 无
********************************************************************/
void TOOL_STRM_MovPtr(STREAM_T *sp, INT32U len)
{
    if (sp != 0) {
        if ((sp->len + len) <= sp->maxlen) {
            sp->len    += len;
            sp->curptr += len;
        } else {
            sp->len = sp->maxlen;                                              /* 越界截断到末尾 */
        }
    }
}

/*******************************************************************
** 函数名	: TOOL_STRM_WriteBYTE
** 函数描述	: 往数据流中写入一个字节数据。
** 参数		: [in] sp:        数据流
**			: [in] writebyte: 写入的数据
** 返回		: 无
********************************************************************/
void TOOL_STRM_WriteBYTE(STREAM_T *sp, INT8U writebyte)
{
    if (sp != 0) {
        if (sp->len < sp->maxlen) {
            *sp->curptr++ = writebyte;
            sp->len++;
        }
    }
}

/*******************************************************************
** 函数名	: TOOL_STRM_WriteHWORD
** 函数描述	: 往数据流中写入一个半字(16位)数据，大端模式(高字节先写)。
** 参数		: [in] sp:        数据流
**			: [in] writeword: 写入的数据
** 返回		: 无
********************************************************************/
void TOOL_STRM_WriteHWORD(STREAM_T *sp, INT16U writeword)
{
    TOOL_STRM_WriteBYTE(sp, (INT8U)(writeword >> 8));
    TOOL_STRM_WriteBYTE(sp, (INT8U)writeword);
}

/*******************************************************************
** 函数名	: TOOL_STRM_LE_WriteHWORD
** 函数描述	: 往数据流中写入一个半字(16位)数据，小端模式(低字节先写)。
** 参数		: [in] sp:        数据流
**			: [in] writeword: 写入的数据
** 返回		: 无
********************************************************************/
void TOOL_STRM_LE_WriteHWORD(STREAM_T *sp, INT16U writeword)
{
    TOOL_STRM_WriteBYTE(sp, (INT8U)writeword);
    TOOL_STRM_WriteBYTE(sp, (INT8U)(writeword >> 8));
}

/*******************************************************************
** 函数名	: TOOL_STRM_WriteLONG
** 函数描述	: 往数据流中写入一个字(32位)数据，大端模式(高字节先写)。
** 参数		: [in] sp:         数据流
**			: [in] writelong: 写入的数据
** 返回		: 无
********************************************************************/
void TOOL_STRM_WriteLONG(STREAM_T *sp, INT32U writelong)
{
    TOOL_STRM_WriteBYTE(sp, (INT8U)(writelong >> 24));
    TOOL_STRM_WriteBYTE(sp, (INT8U)(writelong >> 16));
    TOOL_STRM_WriteBYTE(sp, (INT8U)(writelong >> 8));
    TOOL_STRM_WriteBYTE(sp, (INT8U)writelong);
}

/*******************************************************************
** 函数名	: TOOL_STRM_LE_WriteLONG
** 函数描述	: 往数据流中写入一个字(32位)数据，小端模式(低字节先写)。
** 参数		: [in] sp:         数据流
**			: [in] writelong: 写入的数据
** 返回		: 无
********************************************************************/
void TOOL_STRM_LE_WriteLONG(STREAM_T *sp, INT32U writelong)
{
    TOOL_STRM_WriteBYTE(sp, (INT8U)writelong);
    TOOL_STRM_WriteBYTE(sp, (INT8U)(writelong >> 8));
    TOOL_STRM_WriteBYTE(sp, (INT8U)(writelong >> 16));
    TOOL_STRM_WriteBYTE(sp, (INT8U)(writelong >> 24));
}

/*******************************************************************
** 函数名	: TOOL_STRM_WriteLF
** 函数描述	: 往数据流中写入换行符，即写入'\r'和'\n'。
** 参数		: [in] sp: 数据流
** 返回		: 无
********************************************************************/
void TOOL_STRM_WriteLF(STREAM_T *sp)
{
    TOOL_STRM_WriteBYTE(sp, '\r');
    TOOL_STRM_WriteBYTE(sp, '\n');
}

/*******************************************************************
** 函数名	: TOOL_STRM_WriteCR
** 函数描述	: 往数据流中写入回车符，即写入'\r'。
** 参数		: [in] sp: 数据流
** 返回		: 无
********************************************************************/
void TOOL_STRM_WriteCR(STREAM_T *sp)
{
    TOOL_STRM_WriteBYTE(sp, '\r');
}

/*******************************************************************
** 函数名	: TOOL_STRM_WriteSTR
** 函数描述	: 往数据流中写入字符串（不含结束符）。
** 参数		: [in] sp:  数据流
**			: [in] ptr: 写入的字符串指针
** 返回		: 无
********************************************************************/
void TOOL_STRM_WriteSTR(STREAM_T *sp, char *ptr)
{
    while ((ptr != 0) && (*ptr != 0)) {
        TOOL_STRM_WriteBYTE(sp, (INT8U)*ptr++);
    }
}

/*******************************************************************
** 函数名	: TOOL_STRM_WriteDATA
** 函数描述	: 往数据流中写入一块内存数据。
** 参数		: [in] sp:  数据流
**			: [in] ptr: 写入的数据块地址
**			: [in] len: 写入的数据块字节数
** 返回		: 无
********************************************************************/
void TOOL_STRM_WriteDATA(STREAM_T *sp, INT8U *ptr, INT32U len)
{
    while (len-- > 0) {
        TOOL_STRM_WriteBYTE(sp, *ptr++);
    }
}

/*******************************************************************
** 函数名	: TOOL_STRM_ReadBYTE
** 函数描述	: 从数据流中读取一个字节，读取越界返回0保护。
** 参数		: [in] sp: 数据流
** 返回		: 读取到的字节
********************************************************************/
INT8U TOOL_STRM_ReadBYTE(STREAM_T *sp)
{
    if (sp->curptr >= (sp->startptr + sp->maxlen)) {
        return 0;                                                              /* 读取越界保护 */
    }
    sp->len++;
    return (*sp->curptr++);
}

/*******************************************************************
** 函数名	: TOOL_STRM_ReadHWORD
** 函数描述	: 从数据流中读取一个半字(16位)数据，大端模式(先读为高字节)。
** 参数		: [in] sp: 数据流
** 返回		: 读取到的字
********************************************************************/
INT16U TOOL_STRM_ReadHWORD(STREAM_T *sp)
{
    INT16U temp;

    temp  = (INT16U)((INT16U)TOOL_STRM_ReadBYTE(sp) << 8);
    temp += TOOL_STRM_ReadBYTE(sp);
    return temp;
}

/*******************************************************************
** 函数名	: TOOL_STRM_LE_ReadHWORD
** 函数描述	: 从数据流中读取一个半字(16位)数据，小端模式(先读为低字节)。
** 参数		: [in] sp: 数据流
** 返回		: 读取到的字
********************************************************************/
INT16U TOOL_STRM_LE_ReadHWORD(STREAM_T *sp)
{
    INT16U temp;

    temp  = TOOL_STRM_ReadBYTE(sp);
    temp += (INT16U)((INT16U)TOOL_STRM_ReadBYTE(sp) << 8);
    return temp;
}

/*******************************************************************
** 函数名	: TOOL_STRM_ReadLONG
** 函数描述	: 从数据流中读取一个字(32位)数据，大端模式(先读为高字节)。
** 参数		: [in] sp: 数据流
** 返回		: 读取到的字
********************************************************************/
INT32U TOOL_STRM_ReadLONG(STREAM_T *sp)
{
    INT32U temp;

    temp  = (TOOL_STRM_ReadBYTE(sp) << 24);
    temp += (TOOL_STRM_ReadBYTE(sp) << 16);
    temp += (TOOL_STRM_ReadBYTE(sp) << 8);
    temp += TOOL_STRM_ReadBYTE(sp);
    return temp;
}

/*******************************************************************
** 函数名	: TOOL_STRM_LE_ReadLONG
** 函数描述	: 从数据流中读取一个字(32位)数据，小端模式(先读为低字节)。
** 参数		: [in] sp: 数据流
** 返回		: 读取到的字
********************************************************************/
INT32U TOOL_STRM_LE_ReadLONG(STREAM_T *sp)
{
    INT32U temp;

    temp  = TOOL_STRM_ReadBYTE(sp);
    temp += (TOOL_STRM_ReadBYTE(sp) << 8);
    temp += (TOOL_STRM_ReadBYTE(sp) << 16);
    temp += (TOOL_STRM_ReadBYTE(sp) << 24);
    return temp;
}

/*******************************************************************
** 函数名	: TOOL_STRM_ReadDATA
** 函数描述	: 从数据流中读取指定长度的数据内容。
** 参数		: [in]  sp:  数据流
**			: [out] ptr: 读取到的数据存放的内存地址
**			: [in]  len: 读取的数据长度
** 返回		: 无
********************************************************************/
void TOOL_STRM_ReadDATA(STREAM_T *sp, INT8U *ptr, INT32U len)
{
    while (len-- > 0) {
        *ptr++ = TOOL_STRM_ReadBYTE(sp);
    }
}

/*******************************************************************
** 函数名	: TOOL_STREAM_GetBufferStream
** 函数描述	: 获取全局共享流缓存（每次调用会重新初始化流）。
**			: 备注：用于临时缓存场景（如组帧发送），注意不要并发冲突使用。
** 参数		: 无
** 返回		: 返回流指针
********************************************************************/
STREAM_T *TOOL_STREAM_GetBufferStream(void)
{
    TOOL_STRM_Init(&s_wstrm, s_tempbuf, sizeof(s_tempbuf));
    return &s_wstrm;
}

/*******************************************************************
** 函数名	: TOOL_STREAM_GetStreamBufPtr
** 函数描述	: 获取全局共享流缓存地址。
** 参数		: 无
** 返回		: 缓存地址
********************************************************************/
INT8U *TOOL_STREAM_GetStreamBufPtr(void)
{
    return s_tempbuf;
}

/*******************************************************************
** 函数名	: TOOL_STREAM_GetStreamBufSize
** 函数描述	: 获取全局共享流缓存空间大小。
** 参数		: 无
** 返回		: 缓存空间大小
********************************************************************/
INT32U TOOL_STREAM_GetStreamBufSize(void)
{
    return MAX_STREAM_SIZE;
}
