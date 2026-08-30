#include "os_include.h"

/* 错误管理控制块 */
typedef struct {
    INT8U curindex;
    INT8U nused;
    INT8U nbackup;
    INT8U ct_reset;

    void (*diagproc[OS_MAX_DIAG])(void);
    void (*backproc[OS_MAX_DIAG])(void);

    void (*c_informer[RESET_PRI_MAX][RESET_REG_MAX])(INT8U event, char *filename, INT32U line);
    void (*b_informer[RESET_PRI_MAX][RESET_REG_MAX])(INT8U event, char *filename, INT32U line);
} ECB_T;

static ECB_T s_ecb;

/*******************************************************************
** 函数名	: OS_InitErrMan
** 函数描述	: 错误管理模块初始化。
** 参数		: 无
** 返回		: 无
********************************************************************/
void OS_InitErrMan(void)
{
    memset(&s_ecb, 0, sizeof(s_ecb));
}

/*******************************************************************
** 函数名	: OS_RegistResetInform
** 函数描述	: 注册复位前的通知回调。
** 参数		: [in] prior: 优先级，见RESET_PRI_E
** 参数		: [in] fp:    回调函数
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN OS_RegistResetInform(INT8U prior, void (*fp)(INT8U event, char *filename, INT32U line))
{
    INT8U i;

    if (fp == 0) {
        return false;
    }

    if (prior >= RESET_PRI_MAX) {
        return false;
    }

    for (i = 0; i < RESET_REG_MAX; i++) {
        if (s_ecb.c_informer[prior][i] == 0) {
            s_ecb.c_informer[prior][i] = fp;
            s_ecb.b_informer[prior][i] = fp;
            return true;
        }
    }
    OS_ASSERT(false, RETURN_FALSE);
}

/*******************************************************************
** 函数名	: OS_Reset
** 函数描述	: 复位设备，复位前依次调用已注册的通知回调。
** 参数		: [in] event:    复位事件
** 参数		: [in] filename: 触发文件名
** 参数		: [in] line:     触发行号
** 返回		: 无
********************************************************************/
void OS_Reset(INT8U event, char *filename, INT32U line)
{
    INT8U i, j, len;
    INT8U *ptr;
    INT32U count = 0xfffff;

    len = (INT8U)strlen(filename);
    ptr = (INT8U *)filename;
    for (i = len; i > 0; i--) {                                                /* 只保留文件名部分 */
        if (ptr[i - 1] == '\\') {
            break;
        }
    }
    ptr += i;

    #if DEBUG_ERR > 0
    printf("<assert:file(%s), line(%u), event(%u)>\r\n", (char *)ptr, (unsigned int)line, (unsigned int)event);
    #endif

    s_ecb.ct_reset++;
    if (event != RESET_EVENT_DIRECT && s_ecb.ct_reset <= 1) {
        for (i = 0; i < RESET_PRI_MAX; i++) {
            for (j = 0; j < RESET_REG_MAX; j++) {
                if (s_ecb.c_informer[i][j] == s_ecb.b_informer[i][j] && s_ecb.c_informer[i][j] != 0) {
                    s_ecb.c_informer[i][j](event, (char *)ptr, line);
                }
            }
        }
    }

    for(;;) {
        if (count > 0) {
            if (--count == 0) {
                ClearWatchdog();
                NVIC_SystemReset();
            }
        }
    }
}

/*******************************************************************
** 函数名	: OS_ErrTskEntry
** 函数描述	: 错误管理调度入口，轮询执行已注册的诊断函数。
** 参数		: 无
** 返回		: 无
********************************************************************/
void OS_ErrTskEntry(void)
{
    INT8U i;

    OS_ASSERT((s_ecb.nused == s_ecb.nbackup && s_ecb.nused <= OS_MAX_DIAG), RETURN_VOID);
    if (s_ecb.nused == 0) {
        return;
    }

    i = s_ecb.curindex++;
    if (i >= s_ecb.nused) {
        i = 0;
        s_ecb.curindex = 0;
    }
    OS_ASSERT((s_ecb.diagproc[i] == s_ecb.backproc[i] && s_ecb.diagproc[i] != 0), RETURN_VOID);
    s_ecb.diagproc[i]();
}

/*******************************************************************
** 函数名	: OS_RegistDiagnoseProc
** 函数描述	: 注册一个诊断函数，由OS_ErrTskEntry轮询执行。
** 参数		: [in] diagproc: 诊断函数
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN OS_RegistDiagnoseProc(void (*diagproc)(void))
{
    OS_ASSERT((s_ecb.nused < OS_MAX_DIAG && diagproc != 0), RETURN_FALSE);

    s_ecb.diagproc[s_ecb.nused++]   = diagproc;
    s_ecb.backproc[s_ecb.nbackup++] = diagproc;

    return true;
}
