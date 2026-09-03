#ifndef GATEWAY_CONFIG_H
#define GATEWAY_CONFIG_H

/*
 * 网关参数集中管理：网络/云端/周期等产品级配置统一在此登记，
 * 组件层通过cloud_init(cfg)接收参数，不直接引用本文件，
 * 由App层(app_config/app_gateway)组装后传入。
 */

/* ---------- 网络接入(ESP8266 STA) ---------- */
#define GW_WIFI_SSID            "123"
#define GW_WIFI_PASS            "yw22334455"

/* ---------- NTP校时 ---------- */
#define GW_NTP_SERVER           "ntp.aliyun.com"
#define GW_NTP_PORT             123

/* ---------- OneNET MQTT接入 ---------- */
#define GW_ONENET_BROKER_HOST   "mqtts.heclouds.com"
#define GW_ONENET_BROKER_PORT   1883
#define GW_ONENET_CLIENT_ID     "smartdap"
#define GW_ONENET_USERNAME      "X9Dcio5cI0"
#define GW_ONENET_ACCESS_KEY    "THNpOEhkcFVXb1VBSFE5b0JnWmZDbFBBcmJYUUtyQkM="
#define GW_ONENET_PRODUCT_ID    "X9Dcio5cI0"
#define GW_ONENET_DEVICE_NAME   "smartdap"

/* ---------- 数据上报周期(ms) ---------- */
#define GW_REPORT_PROPERTY_MS   10000   /* 属性上报周期 */
#define GW_REPORT_EVENT_MS      30000   /* 事件上报周期 */

#endif /* GATEWAY_CONFIG_H */
