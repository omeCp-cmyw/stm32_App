/********************************************************************************
**
** 文件名:     ota_mmi.c
** 版权所有:   无
** 文件描述:   OneNET OTA远程升级状态机（LwIP同步socket版）
**             移植自 STM32F4\stm32_App 工程，与Ymodem互斥
**
*********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "../ntp/ntp_mmi.h"
#include "../mqtt/mqttclient.h"          /* PRODUCT_ID/DEVICE_NAME/ACCESS_KEY */
#include "../../App/app_config.h"
#include "../../FwUpgrade/fw_upgrade.h"
#include "../../FwUpgrade/ymodem/ymodem.h"
#include "../../Tools/md5.h"
#include "../../Tools/debug.h"
#include "../../OSAL/osal.h"
#include "ota_proto.h"
#include "ota_mmi.h"

/* token过期时间：NTP未同步用固定值 */
#define OTA_TOKEN_VALID_SEC     3600

/* 应答体上限 */
#define OTA_RESP_MAX_BODY       1024

/* OTA控制块放CCM */
typedef struct {
    ota_step_t step;
    ota_task_t task;            /* check应答解析结果 */
    int fw_size;                /* 固件总字节数 */
    int recv_size;              /* 已接收字节数 */
    int chunk_size;             /* 分片大小 */
    int content_length;         /* 当前应答Content-Length */
    int last_report_step;       /* 上次上报进度pct */
    MD5_CTX md5_ctx;            /* MD5增量校验上下文 */
    uint8_t md5_digest[16];     /* MD5摘要 */
    char md5_str[33];           /* MD5十六进制字符串 */
    char resp_buf[2048];        /* HTTP响应缓冲: 头+分片1024 */
    int resp_len;               /* 响应已收字节数 */
    char tx_buf[512];           /* HTTP请求缓冲 */
} ota_ctrl_t;

static ota_ctrl_t s_ota __attribute__((section("CCM_RAM"), zero_init));

/*******************************************************************
** 函数名	: OtaReset
** 函数描述	: 复位OTA状态机到空闲（缓冲不清理）
** 参数		: 无
** 返回		: 无
********************************************************************/
static void OtaReset(void)
{
    s_ota.step = OTA_STEP_IDLE;
    s_ota.fw_size = 0;
    s_ota.recv_size = 0;
    s_ota.chunk_size = 0;
    s_ota.content_length = 0;
    s_ota.last_report_step = -10;
    s_ota.resp_len = 0;
}

/*******************************************************************
** 函数名	: OtaGetExpireTs
** 函数描述	: 获取token过期时间戳（NTP同步则当前+1h，否则用固定值）
** 参数		: 无
** 返回		: 过期时间戳(unix秒)
********************************************************************/
static uint32_t OtaGetExpireTs(void)
{
    if (NTP_MMI_TimeValid()) {
        return NTP_MMI_NowUnix() + OTA_TOKEN_VALID_SEC;
    }
    return ONENET_TOKEN_ET_FALLBACK;
}

/*******************************************************************
** 函数名	: OtaTcpOpen
** 函数描述	: 解析OTA服务器域名并建立TCP连接，设置收发超时与
**             SO_LINGER{RST立即关链}
** 参数		: 无
** 返回		: socket fd, -1失败
********************************************************************/
static int OtaTcpOpen(void)
{
    struct sockaddr_in srv;
    ip_addr_t srv_ip;
    struct timeval tv;
    struct linger ling;
    int fd;
    int retry;
    const ip_addr_t *dns_srv;

    /* DNS首查易失败，重试5次 */
    for (retry = 0; retry < 5; retry++) {
        if (dns_gethostbyname(APP_OTA_HOST, &srv_ip, NULL, NULL) == ERR_OK) {
            break;
        }
        dns_srv = dns_getserver(0);
        DEBUG_INFO("[OTA] dns resolve %s fail (dns server %s), retry in 2 s",
                   APP_OTA_HOST, ipaddr_ntoa(dns_srv));
        osal_task_delay(2000);
    }
    if (retry >= 5) {
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        DEBUG_INFO("[OTA] socket create fail");
        return -1;
    }

    /* 收发超时 */
    tv.tv_sec = APP_OTA_TIMEOUT_MS / 1000;
    tv.tv_usec = (APP_OTA_TIMEOUT_MS % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* RST关链，同transport.c */
    ling.l_onoff = 1;
    ling.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));

    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(80);
    srv.sin_addr.s_addr = srv_ip.addr;

    if (connect(fd, (struct sockaddr *)&srv, sizeof(srv)) != 0) {
        DEBUG_INFO("[OTA] tcp connect fail");
        close(fd);
        return -1;
    }
    return fd;
}

/*******************************************************************
** 函数名	: OtaHttpTransaction
** 函数描述	: 发送HTTP请求，按Content-Length收完整响应（头+体）
** 参数		: [in] fd: 已连接socket
**          : [in] req: 请求数据
**          : [in] req_len: 请求字节数
**          : [in] max_body: 应答体上限
**          : [in] dump: 1打印请求与响应全文, 0不打印
** 返回		: 应答体在resp_buf中的偏移, -1失败
********************************************************************/
static int OtaHttpTransaction(int fd, const char *req, int req_len,
                              int max_body, int dump)
{
    char *hdr_end;
    int body_off;
    int cl;

    if (dump) {
        DEBUG_INFO("[OTA] >>> request (%d bytes):\n%s", req_len, req);
    }
    if (send(fd, req, req_len, 0) < 0) {
        return -1;
    }

    s_ota.resp_len = 0;
    s_ota.content_length = 0;

    for (;;) {
        int r = recv(fd, s_ota.resp_buf + s_ota.resp_len,
                     (int)sizeof(s_ota.resp_buf) - 1 - s_ota.resp_len, 0);
        if (r <= 0) {
            /* 超时或对端关闭 */
            return -1;
        }
        s_ota.resp_len += r;
        s_ota.resp_buf[s_ota.resp_len] = '\0';

        /* HTTP头是否收全 */
        hdr_end = strstr(s_ota.resp_buf, "\r\n\r\n");
        if (hdr_end == NULL) {
            if (s_ota.resp_len >= (int)sizeof(s_ota.resp_buf) - 1) {
                return -1;      /* 头异常大 */
            }
            continue;
        }

        /* 解析Content-Length */
        if (s_ota.content_length == 0) {
            char *p = strstr(s_ota.resp_buf, "Content-Length:");
            if (p == NULL) {
                p = strstr(s_ota.resp_buf, "content-length:");
            }
            if (p == NULL) {
                return -1;      /* 不支持chunked */
            }
            p += 15;            /* 跳过"Content-Length:" */
            while (*p == ' ') p++;
            cl = atoi(p);
            if (cl <= 0 || cl > max_body) {
                return -1;
            }
            s_ota.content_length = cl;
        }

        body_off = (int)(hdr_end - s_ota.resp_buf) + 4;
        if (s_ota.resp_len - body_off >= s_ota.content_length) {
            if (dump) {
                DEBUG_INFO("[OTA] <<< response (%d bytes):\n%s",
                           s_ota.resp_len, s_ota.resp_buf);
            }
            return body_off;    /* 体收全 */
        }
        /* 继续接收体数据 */
    }
}

/*******************************************************************
** 函数名	: ota_task_proc
** 函数描述	: OTA一轮完整流程（同步阻塞）：连接→版本上报→检查
**             →分片下载→MD5校验→状态上报→复位。
**             升级进行中（Ymodem/OTA）则放弃本轮。
** 参数		: 无
** 返回		: 无
********************************************************************/
void ota_task_proc(void)
{
    int fd;
    int body_off;
    int tx_len;

    /* Ymodem窗口开或FW_UPG非空闲，放弃本轮 */
    if (FW_UPG_YM_IsArmed() || FW_UPG_GetState() != FW_UPG_STATE_IDLE) {
        DEBUG_INFO("[OTA] mutual exclusion, skip this round");
        return;
    }

    /* 1. TCP连接 */
    s_ota.step = OTA_STEP_OPEN;
    fd = OtaTcpOpen();
    if (fd < 0) {
        OtaReset();
        return;
    }
    DEBUG_INFO("[OTA] tcp connected");

    /* 2. 版本上报 */
    s_ota.step = OTA_STEP_VERSION;
    tx_len = ota_version_request(OtaGetExpireTs(), PRODUCT_ID, DEVICE_NAME,
                                 ACCESS_KEY, s_ota.tx_buf, sizeof(s_ota.tx_buf));
    if (tx_len <= 0) {
        goto ota_fail;
    }
    body_off = OtaHttpTransaction(fd, s_ota.tx_buf, tx_len, OTA_RESP_MAX_BODY, 1);
    if (body_off < 0) {
        DEBUG_INFO("[OTA] version request fail");
        goto ota_fail;
    }

    /* 3. 升级检查 */
    s_ota.step = OTA_STEP_CHECK;
    tx_len = ota_check_request(OtaGetExpireTs(), PRODUCT_ID, DEVICE_NAME,
                               ACCESS_KEY, APP_OTA_F_VERSION,
                               s_ota.tx_buf, sizeof(s_ota.tx_buf));
    if (tx_len <= 0) {
        goto ota_fail;
    }
    body_off = OtaHttpTransaction(fd, s_ota.tx_buf, tx_len, OTA_RESP_MAX_BODY, 1);
    if (body_off < 0) {
        DEBUG_INFO("[OTA] check request fail");
        goto ota_fail;
    }
    if (ota_check_parse(s_ota.resp_buf + body_off, &s_ota.task) != 0) {
        DEBUG_INFO("[OTA] check parse fail");
        goto ota_fail;
    }
    if (s_ota.task.code != 0 || s_ota.task.tid <= 0) {
        /* 无升级任务，正常关链退出 */
        DEBUG_INFO("[OTA] no upgrade task");
        goto ota_done;
    }
    DEBUG_INFO("[OTA] upgrade task: tid=%d size=%d target=%s",
               s_ota.task.tid, s_ota.task.size, s_ota.task.target);

    /* 4. 启动写flash并初始化MD5 */
    if (!FW_UPG_StartUpdate((uint32_t)s_ota.task.size)) {
        DEBUG_INFO("[OTA] start update failed");
        goto ota_fail;
    }
    s_ota.fw_size = s_ota.task.size;
    s_ota.recv_size = 0;
    s_ota.chunk_size = APP_OTA_CHUNK_SIZE;
    s_ota.last_report_step = -10;
    MD5_Init(&s_ota.md5_ctx);

    /* 5. 分片下载 */
    s_ota.step = OTA_STEP_DOWNLOAD;
    while (s_ota.recv_size < s_ota.fw_size) {
        long range_start;
        long range_end;
        int retry;
        int body_len;
        int pct;

        /* 下载中Ymodem开窗则中止 */
        if (FW_UPG_YM_IsArmed()) {
            DEBUG_INFO("[OTA] ymodem window opened, abort");
            FW_UPG_StopUpdate();
            goto ota_fail;
        }

        range_start = s_ota.recv_size;
        range_end = range_start + s_ota.chunk_size - 1;
        if (range_end >= s_ota.fw_size) {
            range_end = s_ota.fw_size - 1;
        }

        tx_len = ota_download_request(OtaGetExpireTs(), PRODUCT_ID,
                                      DEVICE_NAME, ACCESS_KEY,
                                      s_ota.task.tid,
                                      range_start, range_end,
                                      s_ota.tx_buf, sizeof(s_ota.tx_buf));
        if (tx_len <= 0) {
            FW_UPG_StopUpdate();
            goto ota_fail;
        }
        DEBUG_INFO("[OTA] download request: tid=%d range=%ld-%ld",
                   s_ota.task.tid, range_start, range_end);

        /* 单片重试，必须收到206 */
        body_off = -1;
        for (retry = 0; retry < APP_OTA_RETRY; retry++) {
            body_off = OtaHttpTransaction(fd, s_ota.tx_buf, tx_len,
                                          s_ota.chunk_size + 512, 0);
            if (body_off >= 0 &&
                strstr(s_ota.resp_buf, "206 Partial Content") != NULL) {
                break;
            }
            DEBUG_INFO("[OTA] chunk fail, retry %d", retry + 1);
        }
        if (body_off < 0) {
            DEBUG_INFO("[OTA] download failed after retry");
            FW_UPG_StopUpdate();
            goto ota_fail;
        }

        /* 写备份分区+MD5 */
        body_len = s_ota.content_length;
        if (!FW_UPG_WriteData((uint8_t *)s_ota.resp_buf + body_off,
                              (uint32_t)body_len)) {
            DEBUG_INFO("[OTA] write flash failed");
            FW_UPG_StopUpdate();
            goto ota_fail;
        }
        MD5_Update(&s_ota.md5_ctx, s_ota.resp_buf + body_off,
                   (uint32_t)body_len);
        s_ota.recv_size += body_len;

        /* 每10%上报一次进度 */
        pct = (int)(((int64_t)s_ota.recv_size * 100) / s_ota.fw_size);
        if (pct >= s_ota.last_report_step + 10) {
            s_ota.last_report_step = pct;
            tx_len = ota_status_request(OtaGetExpireTs(), PRODUCT_ID,
                                        DEVICE_NAME, ACCESS_KEY,
                                        s_ota.task.tid, pct,
                                        s_ota.tx_buf, sizeof(s_ota.tx_buf));
            if (tx_len > 0) {
                (void)OtaHttpTransaction(fd, s_ota.tx_buf, tx_len,
                                         OTA_RESP_MAX_BODY, 1);
            }
        }
        DEBUG_INFO("[OTA] progress %d/%d", s_ota.recv_size, s_ota.fw_size);
    }

    /* 6. MD5校验 */
    s_ota.step = OTA_STEP_VERIFY;
    MD5_Final(s_ota.md5_digest, &s_ota.md5_ctx);
    sprintf(s_ota.md5_str,
            "%02x%02x%02x%02x%02x%02x%02x%02x"
            "%02x%02x%02x%02x%02x%02x%02x%02x",
            s_ota.md5_digest[0], s_ota.md5_digest[1],
            s_ota.md5_digest[2], s_ota.md5_digest[3],
            s_ota.md5_digest[4], s_ota.md5_digest[5],
            s_ota.md5_digest[6], s_ota.md5_digest[7],
            s_ota.md5_digest[8], s_ota.md5_digest[9],
            s_ota.md5_digest[10], s_ota.md5_digest[11],
            s_ota.md5_digest[12], s_ota.md5_digest[13],
            s_ota.md5_digest[14], s_ota.md5_digest[15]);
    if (strcmp(s_ota.md5_str, s_ota.task.md5) != 0) {
        DEBUG_INFO("[OTA] md5 mismatch: expect=%s got=%s",
                   s_ota.task.md5, s_ota.md5_str);
        FW_UPG_StopUpdate();
        goto ota_fail;
    }
    DEBUG_INFO("[OTA] md5 verify ok");

    /* 7. 状态上报 step 100（升级中）→ step 201（成功） */
    s_ota.step = OTA_STEP_STATUS;
    tx_len = ota_status_request(OtaGetExpireTs(), PRODUCT_ID, DEVICE_NAME,
                                ACCESS_KEY, s_ota.task.tid, 100,
                                s_ota.tx_buf, sizeof(s_ota.tx_buf));
    if (tx_len > 0) {
        (void)OtaHttpTransaction(fd, s_ota.tx_buf, tx_len, OTA_RESP_MAX_BODY, 1);
    }
    tx_len = ota_status_request(OtaGetExpireTs(), PRODUCT_ID, DEVICE_NAME,
                                ACCESS_KEY, s_ota.task.tid, 201,
                                s_ota.tx_buf, sizeof(s_ota.tx_buf));
    if (tx_len > 0) {
        (void)OtaHttpTransaction(fd, s_ota.tx_buf, tx_len, OTA_RESP_MAX_BODY, 1);
    }

    /* 8. 完成复位 */
    s_ota.step = OTA_STEP_FINISH;
    close(fd);
    DEBUG_INFO("[OTA] upgrade done, reboot now");
    FW_UPG_Finish();            /* 软复位不返回 */
    return;

ota_fail:
    close(fd);
    OtaReset();
    return;

ota_done:
    close(fd);
    OtaReset();
    return;
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
