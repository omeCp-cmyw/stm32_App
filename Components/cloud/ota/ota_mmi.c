#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osal.h"
#include "drv_systick.h"
#include "link.h"
#include "ntp_mmi.h"
#include "cloud_agent.h"
#include "cloud_onenet_config.h"
#include "onenet_token.h"
#include "gateway_config.h"
#include "fw_upgrade.h"
#include "ota_proto.h"
#include "ota_mmi.h"
#include "md5.h"

/*
 * OneNET OTA远程升级状态机（移植自wifi_pro的onenet.c OTA部分）：
 * 链路2双实例异步HTTP客户端，10ms/100ms定时器驱动状态机，
 * 流式写备份分区FW_UPG_WriteData + MD5增量校验。
 *
 * 触发机制：app_gateway云上线边沿触发一次OTA检查，断线重连自动重试。
 * 双向互斥：OTA状态机入口检测FW_UPG_YM_IsArmed中止；
 *          ymodem_arm()检测FW_UPG状态非IDLE（OTA写flash中）拒绝开窗。
 * 升级中禁上报：FW_UPG_GetState() != IDLE时cloud_onenet三处业务上报入口跳过。
 */

/* OTA控制块 */
typedef struct {
    INT8U tmr;                  /* 定时器ID */
    ota_step_t step;            /* 当前步骤 */
    uint8_t triggered;          /* 触发标志 */
    uint8_t send_result;        /* 发送结果: 0等待,1成功,2失败/超时 */
    uint8_t link_open;          /* 链路已打开 */
    uint8_t tx_busy;            /* 发送事务占用 */
    uint16_t poll;              /* 轮询计数 */
    uint32_t last_tick;         /* 上次活动时间戳 */
    
    /* 任务信息 */
    ota_task_t task;            /* check应答解析结果 */
    int tid;                    /* 任务ID */
    int fw_size;                /* 固件总字节数 */
    int recv_size;              /* 已接收字节数 */
    int chunk_size;             /* 分片大小 */
    int total_chunks;           /* 总分片数 */
    int retry;                  /* 重试次数 */
    
    /* 进度上报 */
    int last_report_step;       /* 上次上报的进度步(0,10,20,...,90) */
    int chunks_downloaded;      /* 自上次上报以来下载的片数 */
    uint32_t download_start_ms; /* 下载开始时间戳(ms) */
    
    /* HTTP响应解析 */
    char resp_buf[2560];        /* HTTP响应缓冲 */
    int resp_len;               /* 响应长度 */
    int resp_body_offset;       /* 响应体偏移 */
    int resp_body_len;          /* 响应体长度 */
    int content_length;         /* Content-Length */
    int cr_start;               /* Content-Range起始字节, -1无 */
    int cr_end;                 /* Content-Range结束字节 */
    
    /* MD5校验 */
    MD5_CTX md5_ctx;            /* MD5上下文 */
    uint8_t md5_digest[16];     /* MD5摘要 */
    char md5_str[33];           /* MD5字符串 */
    
    /* 发送缓冲 */
    char tx_buf[512];           /* HTTP请求缓冲 */
    int tx_len;                 /* 请求长度 */
} ota_ctrl_t;

static ota_ctrl_t s_ota;

/* 链路事件回调 */
static void OtaLinkEvCb(link_event_t ev, const uint8_t *data, int len);

/* 发送完成回调 */
static void OtaTxDone(int ok);

/*******************************************************************
** 函数名	: OtaReset
** 函数描述	: 重置OTA状态机到空闲状态
** 参数		: 无
** 返回		: 无
********************************************************************/
static void OtaReset(void)
{
    s_ota.step = OTA_STEP_IDLE;
    s_ota.triggered = 0;
    s_ota.send_result = 0;
    s_ota.link_open = 0;
    s_ota.tx_busy = 0;
    s_ota.poll = 0;
    s_ota.last_tick = SYSTICK_GetMsTick();
    s_ota.tid = -1;
    s_ota.fw_size = 0;
    s_ota.recv_size = 0;
    s_ota.chunk_size = 0;
    s_ota.total_chunks = 0;
    s_ota.retry = 0;
    s_ota.last_report_step = -1;
    s_ota.chunks_downloaded = 0;
    s_ota.download_start_ms = 0;
    s_ota.resp_len = 0;
    s_ota.resp_body_offset = 0;
    s_ota.resp_body_len = 0;
    s_ota.content_length = 0;
    s_ota.tx_len = 0;
}

/*******************************************************************
** 函数名	: OtaCloseLink
** 函数描述	: 关闭OTA链路
** 参数		: 无
** 返回		: 无
********************************************************************/
static void OtaCloseLink(void)
{
    if (s_ota.link_open) {
        link_get_ota()->close();
        s_ota.link_open = 0;
    }
    s_ota.tx_busy = 0;
}

/*******************************************************************
** 函数名	: OtaSendRequest
** 函数描述	: 发送HTTP请求
** 参数		: [in] data: 请求数据
**          : [in] len: 请求长度
** 返回		: 1成功, 0失败
********************************************************************/
static uint8_t OtaSendRequest(const char *data, int len)
{
    if (s_ota.tx_busy) {
        return 0;
    }
    
    /* 重置响应状态，准备接收新响应 */
    s_ota.send_result = 0;
    s_ota.resp_len = 0;
    s_ota.resp_body_offset = 0;
    s_ota.content_length = 0;
    memset(s_ota.resp_buf, 0, sizeof(s_ota.resp_buf));
    
    if (link_get_ota()->send((const uint8_t *)data, len, OtaTxDone)) {
        s_ota.tx_busy = 1;
        return 1;
    }
    return 0;
}

/*******************************************************************
** 函数名	: OtaParseHttpResponse
** 函数描述	: 解析HTTP响应，分离头和体
** 参数		: 无
** 返回		: 0成功, -1失败
********************************************************************/
static int OtaParseHttpResponse(void)
{
    char *p;
    
    /* 查找空行分隔头和体 */
    p = strstr(s_ota.resp_buf, "\r\n\r\n");
    if (p == NULL) {
        return -1;
    }
    s_ota.resp_body_offset = (int)(p - s_ota.resp_buf) + 4;
    s_ota.resp_body_len = s_ota.resp_len - s_ota.resp_body_offset;
    
    /* 解析Content-Length */
    p = strstr(s_ota.resp_buf, "Content-Length:");
    if (p == NULL) {
        p = strstr(s_ota.resp_buf, "content-length:");
    }
    if (p != NULL) {
        p += 15; /* 跳过"Content-Length:" */
        while (*p == ' ') p++;
        s_ota.content_length = atoi(p);
    }

    /* 解析Content-Range: bytes N-M/T，校验分片字节范围 */
    s_ota.cr_start = -1;
    s_ota.cr_end = -1;
    p = strstr(s_ota.resp_buf, "Content-Range:");
    if (p != NULL) {
        p = strstr(p, "bytes ");
        if (p != NULL) {
            p += 6; /* 跳过"bytes " */
            s_ota.cr_start = atoi(p);
            p = strchr(p, '-');
            if (p != NULL) {
                s_ota.cr_end = atoi(p + 1);
            }
        }
    }
    
    return 0;
}

/*******************************************************************
** 函数名	: OtaGetExpireTs
** 函数描述	: 获取token过期时间戳
** 参数		: 无
** 返回		: 过期时间戳(unix秒)
********************************************************************/
static uint32_t OtaGetExpireTs(void)
{
    if (NTP_MMI_TimeValid()) {
        return NTP_MMI_NowUnix() + ONENET_TOKEN_VALID_SEC;
    }
    return ONENET_TOKEN_ET_FALLBACK;
}

/*******************************************************************
** 函数名	: OtaLinkEvCb
** 函数描述	: 链路事件回调：open结果驱动状态机，IPD数据喂入响应缓冲
** 参数		: [in] ev: 事件类型
**          : [in] data: 负载数据
**          : [in] len: 数据长度
** 返回		: 无
********************************************************************/
static void OtaLinkEvCb(link_event_t ev, const uint8_t *data, int len)
{
    switch (ev) {
    case LINK_EV_CONNECTED:
        s_ota.send_result = 1;
        s_ota.link_open = 1;
        printf("[ota] tcp connected\r\n");
        break;

    case LINK_EV_OPEN_FAIL:
        s_ota.send_result = 2;
        printf("[ota] tcp open failed\r\n");
        break;

    case LINK_EV_DATA:
        /* 按Content-Length期望长度截断, 超量投递取尾部 */
        if (s_ota.resp_body_offset > 0) {
            int need = s_ota.resp_body_offset + s_ota.content_length;
            if (s_ota.resp_len + len > need) {
                int take = need - s_ota.resp_len;
                if (take <= 0) {
                    break; /* 已收满, 丢弃多余 */
                }
                data += len - take; /* 跳垃圾前缀 */
                len = take;
            }
        }
        if (s_ota.resp_len + len <= (int)sizeof(s_ota.resp_buf)) {
            memcpy(s_ota.resp_buf + s_ota.resp_len, data, len);
            s_ota.resp_len += len;
            s_ota.resp_buf[s_ota.resp_len] = '\0';
        }
        break;

    case LINK_EV_CLOSED:
        printf("[ota] link closed\r\n");
        s_ota.link_open = 0;
        s_ota.tx_busy = 0;
        if (s_ota.step == OTA_STEP_FINISH) {
            /* 主动关链后CLOSED异步到达, 此处直接复位, 不回IDLE */
            FW_UPG_Finish();
            /* 不返回 */
        } else if (s_ota.step != OTA_STEP_IDLE) {
            /* 非正常关闭，重置状态机 */
            OtaReset();
        }
        break;

    default:
        break;
    }
}

/*******************************************************************
** 函数名	: OtaTxDone
** 函数描述	: 发送完成回调
** 参数		: [in] ok: 1成功, 0失败
** 返回		: 无
********************************************************************/
static void OtaTxDone(int ok)
{
    s_ota.tx_busy = 0;
    s_ota.send_result = ok ? 1 : 2;
}

/*******************************************************************
** 函数名	: OtaTmrProc
** 函数描述	: OTA状态机定时器回调：10ms/100ms步进各阶段
** 参数		: [in] pdata: 回调参数(未用)
** 返回		: 无
********************************************************************/
static void OtaTmrProc(void *pdata)
{
    pdata = pdata;

    switch (s_ota.step) {
    case OTA_STEP_IDLE:
        /* 等待触发 */
        if (s_ota.triggered) {
            s_ota.triggered = 0;
            s_ota.step = OTA_STEP_OPEN;
            osal_timer_start(s_ota.tmr, 100, 1);
        } else {
            osal_timer_start(s_ota.tmr, 100, 1);
        }
        break;

    case OTA_STEP_OPEN:
        /* 打开TCP连接 */
        if (!link_get_ota()->is_ready()) {
            osal_timer_start(s_ota.tmr, 100, 1);
            break;
        }
        s_ota.send_result = 0;
        if (!link_get_ota()->open(GW_OTA_HOST, 80, OtaLinkEvCb)) {
            printf("[ota] open rejected\r\n");
            OtaReset();
            break;
        }
        s_ota.step = OTA_STEP_OPEN_WAIT;
        s_ota.poll = 0;
        osal_timer_start(s_ota.tmr, 10, 1);
        break;

    case OTA_STEP_OPEN_WAIT:
        /* 等待TCP连接结果 */
        if (s_ota.send_result == 1) {
            /* 连接成功，发送版本上报 */
            s_ota.tx_len = ota_version_request(OtaGetExpireTs(),
                                               GW_ONENET_PRODUCT_ID,
                                               GW_ONENET_DEVICE_NAME,
                                               GW_ONENET_ACCESS_KEY,
                                               s_ota.tx_buf,
                                               sizeof(s_ota.tx_buf));
            if (s_ota.tx_len > 0) {
                if (OtaSendRequest(s_ota.tx_buf, s_ota.tx_len)) {
                    s_ota.step = OTA_STEP_VERSION_WAIT;
                    s_ota.poll = 0;
                } else {
                    OtaCloseLink();
                    OtaReset();
                }
            } else {
                OtaCloseLink();
                OtaReset();
            }
        } else if (s_ota.send_result == 2) {
            /* 连接失败 */
            OtaReset();
        } else if (++s_ota.poll >= 1000) {
            /* 超时 */
            printf("[ota] open timeout\r\n");
            OtaCloseLink();
            OtaReset();
        } else {
            osal_timer_start(s_ota.tmr, 10, 1);
        }
        break;

    case OTA_STEP_VERSION_WAIT:
        /* 等待版本上报应答 */
        if (s_ota.send_result == 2) {
            printf("[ota] version send failed\r\n");
            OtaCloseLink();
            OtaReset();
        } else if (s_ota.resp_len > 0 && strstr(s_ota.resp_buf, "\r\n\r\n") != NULL) {
            /* 收到HTTP头，解析Content-Length */
            if (s_ota.resp_body_offset == 0) {
                OtaParseHttpResponse();
            }
            /* 检查是否收够Content-Length指定的数据量 */
            if (s_ota.resp_body_offset > 0 && 
                s_ota.resp_len >= s_ota.resp_body_offset + s_ota.content_length) {
                /* 发送升级检查 */
                s_ota.tx_len = ota_check_request(OtaGetExpireTs(),
                                                 GW_ONENET_PRODUCT_ID,
                                                 GW_ONENET_DEVICE_NAME,
                                                 GW_ONENET_ACCESS_KEY,
                                                 GW_OTA_F_VERSION,
                                                 s_ota.tx_buf,
                                                 sizeof(s_ota.tx_buf));
                if (s_ota.tx_len > 0) {
                    if (OtaSendRequest(s_ota.tx_buf, s_ota.tx_len)) {
                        s_ota.step = OTA_STEP_CHECK_WAIT;
                        s_ota.poll = 0;
                    } else {
                        OtaCloseLink();
                        OtaReset();
                    }
                } else {
                    OtaCloseLink();
                    OtaReset();
                }
            } else {
                /* 继续等待数据 */
                osal_timer_start(s_ota.tmr, 10, 1);
            }
        } else if (++s_ota.poll >= 1000) {
            printf("[ota] version timeout\r\n");
            OtaCloseLink();
            OtaReset();
        } else {
            osal_timer_start(s_ota.tmr, 10, 1);
        }
        break;

    case OTA_STEP_CHECK_WAIT:
        /* 等待升级检查应答 */
        if (s_ota.send_result == 2) {
            printf("[ota] check send failed\r\n");
            OtaCloseLink();
            OtaReset();
        } else if (s_ota.resp_len > 0 && strstr(s_ota.resp_buf, "\r\n\r\n") != NULL) {
            /* 收到HTTP头，解析Content-Length */
            if (s_ota.resp_body_offset == 0) {
                OtaParseHttpResponse();
            }
            /* 检查是否收够Content-Length指定的数据量 */
            if (s_ota.resp_body_offset > 0 && 
                s_ota.resp_len >= s_ota.resp_body_offset + s_ota.content_length) {
                int parse_result = ota_check_parse(s_ota.resp_buf + s_ota.resp_body_offset, &s_ota.task);
                if (parse_result == 0) {
                    if (s_ota.task.code == 0 && s_ota.task.tid > 0) {
                        /* 有升级任务 */
                        printf("[ota] upgrade task: tid=%d, size=%d, md5=%s\r\n",
                               s_ota.task.tid, s_ota.task.size, s_ota.task.md5);
                        s_ota.tid = s_ota.task.tid;
                        s_ota.fw_size = s_ota.task.size;
                        s_ota.recv_size = 0;
                        s_ota.chunk_size = GW_OTA_CHUNK_SIZE;
                        s_ota.retry = 0;
                        s_ota.last_report_step = -1;
                        s_ota.chunks_downloaded = 0;
                        s_ota.download_start_ms = SYSTICK_GetMsTick();
                        
                        /* 计算分片数 */
                        s_ota.total_chunks = (s_ota.fw_size + s_ota.chunk_size - 1) / s_ota.chunk_size;
                        printf("[ota] download plan: size=%d, %d chunks x %d bytes\r\n",
                               s_ota.fw_size, s_ota.total_chunks, s_ota.chunk_size);
                        
                        /* 初始化MD5 */
                        MD5_Init(&s_ota.md5_ctx);
                        
                        /* 启动固件升级 */
                        if (FW_UPG_StartUpdate(s_ota.fw_size)) {
                            /* 发送下载请求 */
                            long range_end = s_ota.chunk_size - 1;
                            if (range_end >= s_ota.fw_size) {
                                range_end = s_ota.fw_size - 1;
                            }
                            s_ota.tx_len = ota_download_request(OtaGetExpireTs(),
                                                               GW_ONENET_PRODUCT_ID,
                                                               GW_ONENET_DEVICE_NAME,
                                                               GW_ONENET_ACCESS_KEY,
                                                               s_ota.tid,
                                                               0, range_end,
                                                               s_ota.tx_buf,
                                                               sizeof(s_ota.tx_buf));
                            if (s_ota.tx_len > 0) {
                                if (OtaSendRequest(s_ota.tx_buf, s_ota.tx_len)) {
                                    s_ota.step = OTA_STEP_DOWNLOAD_WAIT;
                                    s_ota.poll = 0;
                                } else {
                                    FW_UPG_StopUpdate();
                                    OtaCloseLink();
                                    OtaReset();
                                }
                            } else {
                                FW_UPG_StopUpdate();
                                OtaCloseLink();
                                OtaReset();
                            }
                        } else {
                            printf("[ota] start update failed\r\n");
                            OtaCloseLink();
                            OtaReset();
                        }
                    } else {
                        /* 无升级任务 */
                        printf("[ota] no upgrade task\r\n");
                        OtaCloseLink();
                        OtaReset();
                    }
                } else {
                    printf("[ota] check parse failed\r\n");
                    OtaCloseLink();
                    OtaReset();
                }
            } else {
                /* 继续等待数据 */
                osal_timer_start(s_ota.tmr, 10, 1);
            }
        } else if (++s_ota.poll >= 1000) {
            printf("[ota] check timeout\r\n");
            OtaCloseLink();
            OtaReset();
        } else {
            osal_timer_start(s_ota.tmr, 10, 1);
        }
        break;

    case OTA_STEP_DOWNLOAD_WAIT:
        /* 等待固件下载应答 */
        if (s_ota.send_result == 2) {
            printf("[ota] download send failed, wait and retry\r\n");
            s_ota.tx_busy = 0;
            link_get_ota()->reset_busy();
            if (++s_ota.retry < GW_OTA_RETRY) {
                /* 等待500ms再重试 */
                s_ota.step = OTA_STEP_DOWNLOAD_FAIL_WAIT;
                s_ota.poll = 0;
                osal_timer_start(s_ota.tmr, 10, 1);
                break;
            }
            FW_UPG_StopUpdate();
            OtaCloseLink();
            OtaReset();
        } else if (s_ota.resp_len > 0 && strstr(s_ota.resp_buf, "\r\n\r\n") != NULL && 
                   s_ota.resp_body_offset == 0) {
            /* 首次收到HTTP头，解析Content-Length */
            OtaParseHttpResponse();
            
            /* 检查HTTP状态码：分片下载必须返回206 */
            if (strstr(s_ota.resp_buf, "206 Partial Content") == NULL) {
                printf("[ota] download error: expect 206, got %s\r\n", s_ota.resp_buf);
                FW_UPG_StopUpdate();
                OtaCloseLink();
                OtaReset();
                break;
            }
            osal_timer_start(s_ota.tmr, 10, 1);
        } else if (s_ota.resp_body_offset > 0 && 
                   s_ota.resp_len >= s_ota.resp_body_offset + s_ota.content_length) {
            /* 收够Content-Length指定的数据量 */
            s_ota.resp_body_len = s_ota.content_length;

            /* 检查HTTP状态码：分片下载必须返回206 */
            if (strstr(s_ota.resp_buf, "206 Partial Content") == NULL) {
                printf("[ota] download error: expect 206, got %s\r\n", s_ota.resp_buf);
                FW_UPG_StopUpdate();
                OtaCloseLink();
                OtaReset();
                break;
            }

            /* body按Content-Length取数, 超量部分丢弃 */

            /* 写入固件数据 */
            if (s_ota.resp_body_len > 0) {
                if (!FW_UPG_WriteData((uint8_t *)s_ota.resp_buf + s_ota.resp_body_offset,
                                      s_ota.resp_body_len)) {
                    printf("[ota] write data failed\r\n");
                    FW_UPG_StopUpdate();
                    OtaCloseLink();
                    OtaReset();
                    break;
                }
                
                /* 更新MD5 */
                MD5_Update(&s_ota.md5_ctx, 
                          s_ota.resp_buf + s_ota.resp_body_offset,
                          s_ota.resp_body_len);
                
                s_ota.recv_size += s_ota.resp_body_len;
                s_ota.chunks_downloaded++;
            }
            
            /* 检查是否下载完成 */
            if (s_ota.recv_size >= s_ota.fw_size) {
                /* 下载完成，计算MD5 */
                MD5_Final(s_ota.md5_digest, &s_ota.md5_ctx);
                sprintf(s_ota.md5_str, "%02x%02x%02x%02x%02x%02x%02x%02x"
                                       "%02x%02x%02x%02x%02x%02x%02x%02x",
                        s_ota.md5_digest[0], s_ota.md5_digest[1],
                        s_ota.md5_digest[2], s_ota.md5_digest[3],
                        s_ota.md5_digest[4], s_ota.md5_digest[5],
                        s_ota.md5_digest[6], s_ota.md5_digest[7],
                        s_ota.md5_digest[8], s_ota.md5_digest[9],
                        s_ota.md5_digest[10], s_ota.md5_digest[11],
                        s_ota.md5_digest[12], s_ota.md5_digest[13],
                        s_ota.md5_digest[14], s_ota.md5_digest[15]);
                
                /* 校验MD5 */
                if (strcmp(s_ota.md5_str, s_ota.task.md5) == 0) {
                    uint32_t elapsed = SYSTICK_GetMsTick() - s_ota.download_start_ms;
                    printf("[ota] md5 verify ok\r\n");
                    printf("[ota] download ok: target=%s size=%d (%u ms)\r\n",
                           s_ota.task.target, s_ota.fw_size, (unsigned int)elapsed);
                    /* 上报step 100 */
                    s_ota.tx_len = ota_status_request(OtaGetExpireTs(),
                                                     GW_ONENET_PRODUCT_ID,
                                                     GW_ONENET_DEVICE_NAME,
                                                     GW_ONENET_ACCESS_KEY,
                                                     s_ota.tid, 100,
                                                     s_ota.tx_buf,
                                                     sizeof(s_ota.tx_buf));
                    if (s_ota.tx_len > 0) {
                        if (OtaSendRequest(s_ota.tx_buf, s_ota.tx_len)) {
                            s_ota.step = OTA_STEP_STATUS_100_WAIT;
                            s_ota.poll = 0;
                        } else {
                            FW_UPG_StopUpdate();
                            OtaCloseLink();
                            OtaReset();
                        }
                    } else {
                        FW_UPG_StopUpdate();
                        OtaCloseLink();
                        OtaReset();
                    }
                } else {
                    printf("[ota] md5 verify failed: expect=%s, got=%s\r\n",
                           s_ota.task.md5, s_ota.md5_str);
                    FW_UPG_StopUpdate();
                    OtaCloseLink();
                    OtaReset();
                }
            } else {
                /* 下载完成，统一进入等待状态，让ESP8266稳定后再决定下一步 */
                s_ota.step = OTA_STEP_DOWNLOAD_CONTINUE_WAIT;
                s_ota.poll = 0;
                osal_timer_start(s_ota.tmr, 10, 1);
                break;
            }
        } else if (++s_ota.poll >= GW_OTA_TIMEOUT_MS / 10) {
            printf("[ota] download timeout, wait and retry\r\n");
            s_ota.tx_busy = 0;
            link_get_ota()->reset_busy();
            if (++s_ota.retry < GW_OTA_RETRY) {
                /* 等待500ms再重试 */
                s_ota.step = OTA_STEP_DOWNLOAD_FAIL_WAIT;
                s_ota.poll = 0;
                osal_timer_start(s_ota.tmr, 10, 1);
                break;
            }
            FW_UPG_StopUpdate();
            OtaCloseLink();
            OtaReset();
        } else {
            osal_timer_start(s_ota.tmr, 10, 1);
        }
        break;

    case OTA_STEP_DOWNLOAD_CONTINUE_WAIT:
        /* 每片下载完成后等待100ms，让ESP8266稳定 */
        if (++s_ota.poll >= 10) {
            /* 检查是否需要上报进度(第一片后上报step0，之后每2片上报一次) */
            int progress_pct = (s_ota.recv_size * 100) / s_ota.fw_size;
            int report_step = (progress_pct / 10) * 10;
            int should_report = 0;
            
            if (s_ota.last_report_step < 0 && s_ota.chunks_downloaded >= 1) {
                /* 第一片下载完成，上报step 0 */
                report_step = 0;
                should_report = 1;
            } else if (s_ota.chunks_downloaded >= 2 && 
                       report_step > s_ota.last_report_step && 
                       report_step < 100) {
                /* 每2片上报一次进度 */
                should_report = 1;
            }
            
            if (should_report) {
                /* 上报下载进度 */
                s_ota.tx_len = ota_status_request(OtaGetExpireTs(),
                                                 GW_ONENET_PRODUCT_ID,
                                                 GW_ONENET_DEVICE_NAME,
                                                 GW_ONENET_ACCESS_KEY,
                                                 s_ota.tid, report_step,
                                                 s_ota.tx_buf,
                                                 sizeof(s_ota.tx_buf));
                if (s_ota.tx_len > 0) {
                    s_ota.last_report_step = report_step;
                    s_ota.chunks_downloaded = 0;
                    if (OtaSendRequest(s_ota.tx_buf, s_ota.tx_len)) {
                        s_ota.step = OTA_STEP_STATUS_PROGRESS_WAIT;
                        s_ota.poll = 0;
                        osal_timer_start(s_ota.tmr, 10, 1);
                        break;
                    }
                }
                /* 上报失败继续下载(非关键错误) */
            }
            
            /* 继续下载下一片 */
            long range_start = s_ota.recv_size;
            long range_end = s_ota.recv_size + s_ota.chunk_size - 1;
            if (range_end >= s_ota.fw_size) {
                range_end = s_ota.fw_size - 1;
            }
            s_ota.tx_len = ota_download_request(OtaGetExpireTs(),
                                               GW_ONENET_PRODUCT_ID,
                                               GW_ONENET_DEVICE_NAME,
                                               GW_ONENET_ACCESS_KEY,
                                               s_ota.tid,
                                               range_start, range_end,
                                               s_ota.tx_buf,
                                               sizeof(s_ota.tx_buf));
            if (s_ota.tx_len > 0) {
                s_ota.retry = 0;
                if (OtaSendRequest(s_ota.tx_buf, s_ota.tx_len)) {
                    s_ota.step = OTA_STEP_DOWNLOAD_WAIT;
                    s_ota.poll = 0;
                    osal_timer_start(s_ota.tmr, 10, 1);
                } else {
                    FW_UPG_StopUpdate();
                    OtaCloseLink();
                    OtaReset();
                }
            } else {
                FW_UPG_StopUpdate();
                OtaCloseLink();
                OtaReset();
            }
        } else {
            osal_timer_start(s_ota.tmr, 10, 1);
        }
        break;

    case OTA_STEP_DOWNLOAD_FAIL_WAIT:
        /* 下载失败后等待500ms再重试，让ESP8266稳定 */
        if (++s_ota.poll >= 50) {
            if (++s_ota.retry >= GW_OTA_RETRY) {
                printf("[ota] chunk retry exhausted, abort\r\n");
                FW_UPG_StopUpdate();
                OtaCloseLink();
                OtaReset();
                break;
            }
            printf("[ota] download fail wait done, retry %d\r\n", s_ota.retry);
            if (OtaSendRequest(s_ota.tx_buf, s_ota.tx_len)) {
                s_ota.step = OTA_STEP_DOWNLOAD_WAIT;
                s_ota.poll = 0;
                osal_timer_start(s_ota.tmr, 10, 1);
            } else {
                FW_UPG_StopUpdate();
                OtaCloseLink();
                OtaReset();
            }
        } else {
            osal_timer_start(s_ota.tmr, 10, 1);
        }
        break;

    case OTA_STEP_STATUS_PROGRESS_WAIT:
        /* 等待进度上报应答 */
        if (s_ota.send_result == 2) {
            printf("[ota] progress report failed, wait and retry\r\n");
            s_ota.tx_busy = 0;
            link_get_ota()->reset_busy();
            s_ota.step = OTA_STEP_PROGRESS_FAIL_WAIT;
            s_ota.poll = 0;
            osal_timer_start(s_ota.tmr, 10, 1);
            break;
        } else if (s_ota.resp_len > 0 && strstr(s_ota.resp_buf, "\r\n\r\n") != NULL) {
            /* 收到HTTP头，解析Content-Length */
            if (s_ota.resp_body_offset == 0) {
                OtaParseHttpResponse();
            }
            /* 检查是否收够Content-Length指定的数据量 */
            if (s_ota.resp_body_offset > 0 && 
                s_ota.resp_len >= s_ota.resp_body_offset + s_ota.content_length) {
            } else {
                /* 继续等待数据 */
                osal_timer_start(s_ota.tmr, 10, 1);
                break;
            }
        } else if (++s_ota.poll >= 1000) {
            printf("[ota] progress timeout, wait and retry\r\n");
            s_ota.tx_busy = 0;
            link_get_ota()->reset_busy();
            s_ota.step = OTA_STEP_PROGRESS_FAIL_WAIT;
            s_ota.poll = 0;
            osal_timer_start(s_ota.tmr, 10, 1);
            break;
        } else {
            osal_timer_start(s_ota.tmr, 10, 1);
            break;
        }
        
        /* 进度上报完成/失败/超时，继续下载下一片 */
        {
            long range_start = s_ota.recv_size;
            long range_end = s_ota.recv_size + s_ota.chunk_size - 1;
            if (range_end >= s_ota.fw_size) {
                range_end = s_ota.fw_size - 1;
            }
            s_ota.tx_len = ota_download_request(OtaGetExpireTs(),
                                               GW_ONENET_PRODUCT_ID,
                                               GW_ONENET_DEVICE_NAME,
                                               GW_ONENET_ACCESS_KEY,
                                               s_ota.tid,
                                               range_start, range_end,
                                               s_ota.tx_buf,
                                               sizeof(s_ota.tx_buf));
            if (s_ota.tx_len > 0) {
                s_ota.retry = 0;
                if (OtaSendRequest(s_ota.tx_buf, s_ota.tx_len)) {
                    s_ota.step = OTA_STEP_DOWNLOAD_WAIT;
                    s_ota.poll = 0;
                    osal_timer_start(s_ota.tmr, 10, 1);
                    break;
                }
            }
            /* 发送失败才停止升级 */
            FW_UPG_StopUpdate();
            OtaCloseLink();
            OtaReset();
        }
        break;

    case OTA_STEP_PROGRESS_FAIL_WAIT:
        /* 进度上报失败后等待500ms再继续下载，让ESP8266稳定 */
        if (++s_ota.poll >= 50) {
            printf("[ota] progress fail wait done, continue download\r\n");
            /* 继续下载下一片 */
            {
                long range_start = s_ota.recv_size;
                long range_end = s_ota.recv_size + s_ota.chunk_size - 1;
                if (range_end >= s_ota.fw_size) {
                    range_end = s_ota.fw_size - 1;
                }
                s_ota.tx_len = ota_download_request(OtaGetExpireTs(),
                                                   GW_ONENET_PRODUCT_ID,
                                                   GW_ONENET_DEVICE_NAME,
                                                   GW_ONENET_ACCESS_KEY,
                                                   s_ota.tid,
                                                   range_start, range_end,
                                                   s_ota.tx_buf,
                                                   sizeof(s_ota.tx_buf));
                if (s_ota.tx_len > 0) {
                    s_ota.retry = 0;
                    if (OtaSendRequest(s_ota.tx_buf, s_ota.tx_len)) {
                        s_ota.step = OTA_STEP_DOWNLOAD_WAIT;
                        s_ota.poll = 0;
                        osal_timer_start(s_ota.tmr, 10, 1);
                        break;
                    }
                }
                /* 发送失败才停止升级 */
                FW_UPG_StopUpdate();
                OtaCloseLink();
                OtaReset();
            }
        } else {
            osal_timer_start(s_ota.tmr, 10, 1);
        }
        break;

    case OTA_STEP_STATUS_100_WAIT:
        /* 等待step 100应答 */
        if (s_ota.send_result == 2) {
            printf("[ota] status 100 send failed\r\n");
            OtaCloseLink();
            OtaReset();
        } else if (s_ota.resp_len > 0 && strstr(s_ota.resp_buf, "\r\n\r\n") != NULL) {
            /* 收到HTTP头，解析Content-Length */
            if (s_ota.resp_body_offset == 0) {
                OtaParseHttpResponse();
            }
            /* 检查是否收够Content-Length指定的数据量 */
            if (s_ota.resp_body_offset > 0 && 
                s_ota.resp_len >= s_ota.resp_body_offset + s_ota.content_length) {
                
                /* 上报step 201 */
                s_ota.tx_len = ota_status_request(OtaGetExpireTs(),
                                                 GW_ONENET_PRODUCT_ID,
                                                 GW_ONENET_DEVICE_NAME,
                                                 GW_ONENET_ACCESS_KEY,
                                                 s_ota.tid, 201,
                                                 s_ota.tx_buf,
                                                 sizeof(s_ota.tx_buf));
                if (s_ota.tx_len > 0) {
                    s_ota.send_result = 0;
                    s_ota.resp_len = 0;
                    if (OtaSendRequest(s_ota.tx_buf, s_ota.tx_len)) {
                        s_ota.step = OTA_STEP_STATUS_201_WAIT;
                        s_ota.poll = 0;
                    } else {
                        OtaCloseLink();
                        OtaReset();
                    }
                } else {
                    OtaCloseLink();
                    OtaReset();
                }
            } else {
                /* 继续等待数据 */
                osal_timer_start(s_ota.tmr, 10, 1);
            }
        } else if (++s_ota.poll >= 1000) {
            printf("[ota] status 100 timeout\r\n");
            OtaCloseLink();
            OtaReset();
        } else {
            osal_timer_start(s_ota.tmr, 10, 1);
        }
        break;

    case OTA_STEP_STATUS_201_WAIT:
        /* 等待step 201应答 */
        if (s_ota.send_result == 2) {
            printf("[ota] status 201 send failed\r\n");
            OtaCloseLink();
            OtaReset();
        } else if (s_ota.resp_len > 0 && strstr(s_ota.resp_buf, "\r\n\r\n") != NULL) {
            /* 收到HTTP头，解析Content-Length */
            if (s_ota.resp_body_offset == 0) {
                OtaParseHttpResponse();
            }
            /* 检查是否收够Content-Length指定的数据量 */
            if (s_ota.resp_body_offset > 0 && 
                s_ota.resp_len >= s_ota.resp_body_offset + s_ota.content_length) {
                
                /* 关闭链路，准备复位 */
                OtaCloseLink();
                s_ota.step = OTA_STEP_FINISH;
                osal_timer_start(s_ota.tmr, 100, 1);
            } else {
                /* 继续等待数据 */
                osal_timer_start(s_ota.tmr, 10, 1);
            }
        } else if (++s_ota.poll >= 1000) {
            printf("[ota] status 201 timeout\r\n");
            OtaCloseLink();
            OtaReset();
        } else {
            osal_timer_start(s_ota.tmr, 10, 1);
        }
        break;

    case OTA_STEP_FINISH:
        /* 调用FW_UPG_Finish复位（CLOSED事件未及时到达时的兜底路径） */
        printf("[ota] upgrade finish, reset\r\n");
        FW_UPG_Finish();
        /* 不会执行到这里，FW_UPG_Finish会复位 */
        break;

    default:
        OtaReset();
        break;
    }
}

/*******************************************************************
** 函数名	: ota_init
** 函数描述	: OTA状态机初始化，注册链路2回调
** 参数		: 无
** 返回		: 无
********************************************************************/
void ota_init(void)
{
    OtaReset();
    s_ota.tmr = osal_timer_create((void *)0, OtaTmrProc);
    if (s_ota.tmr != 0xff) {
        osal_timer_start(s_ota.tmr, 100, 1);
    }
}

/*******************************************************************
** 函数名	: ota_trigger
** 函数描述	: 触发一次OTA检查（云上线边沿调用）
** 参数		: 无
** 返回		: 无
********************************************************************/
void ota_trigger(void)
{
    if (s_ota.step == OTA_STEP_IDLE) {
        s_ota.triggered = 1;
    }
}

/*******************************************************************
** 函数名	: ota_get_step
** 函数描述	: 查询当前OTA状态机步骤
** 参数		: 无
** 返回		: ota_step_t步骤
********************************************************************/
ota_step_t ota_get_step(void)
{
    return s_ota.step;
}

/*******************************************************************
** 函数名	: ota_is_busy
** 函数描述	: 查询OTA是否正在进行（非IDLE）
** 参数		: 无
** 返回		: 1忙, 0空闲
********************************************************************/
uint8_t ota_is_busy(void)
{
    return s_ota.step != OTA_STEP_IDLE;
}
