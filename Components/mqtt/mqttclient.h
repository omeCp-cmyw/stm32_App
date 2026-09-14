#ifndef __MALLOC_H
#define __MALLOC_H
#include "stm32f4xx.h"
#include "lwipopts.h"


#define   MSG_MAX_LEN     500
#define   MSG_TOPIC_LEN   64
#define   KEEPLIVE_TIME   60
#define   MQTT_VERSION    4

#if    LWIP_DNS
#define   HOST_NAME       "mqtts.heclouds.com"     //服务器域名
#else
#define   HOST_NAME       "183.230.40.96"     //服务器IP地址
#endif


#define   HOST_PORT     1883

/* OneNET物模型设备接入信息(与参考工程gateway_config.h一致) */
#define   CLIENT_ID     "smartdap"                             //设备名
#define   USER_NAME     "X9Dcio5cI0"                           //用户名(产品ID)
#define   ACCESS_KEY    "THNpOEhkcFVXb1VBSFE5b0JnWmZDbFBBcmJYUUtyQkM="   //产品访问密钥
#define   PRODUCT_ID    "X9Dcio5cI0"                           //产品ID
#define   DEVICE_NAME   "smartdap"                             //设备名

/* 设备鉴权token参数(token由onenet_token_build动态生成) */
#define   ONENET_TOKEN_ET_FALLBACK   1872999428   /* 未校时的固定过期时间戳(2029年) */

/* 物模型topic(与OneNET平台约定, 由产品ID/设备名拼接) */
#define   TOPIC_POST           "$sys/X9Dcio5cI0/smartdap/thing/property/post"
#define   TOPIC_POST_REPLY     "$sys/X9Dcio5cI0/smartdap/thing/property/post/reply"
#define   TOPIC_SET            "$sys/X9Dcio5cI0/smartdap/thing/property/set"
#define   TOPIC_SET_REPLY      "$sys/X9Dcio5cI0/smartdap/thing/property/set_reply"
#define   TOPIC_DESIRED_REPLY  "$sys/X9Dcio5cI0/smartdap/thing/property/desired/get/reply"
#define   TOPIC_DESIRED_GET    "$sys/X9Dcio5cI0/smartdap/thing/property/desired/get"
#define   TOPIC_EVENT_POST     "$sys/X9Dcio5cI0/smartdap/thing/event/post"
#define   TOPIC_EVENT_REPLY    "$sys/X9Dcio5cI0/smartdap/thing/event/post/reply"
#define   TEST_MESSAGE  "test_message"  //发送测试消息

enum TopicType 
{
  TopicType1 = 1, 
  TopicType3 = 3, 
  TopicType5 = 5 
};


enum QoS 
{ 
  QOS0 = 0, 
  QOS1, 
  QOS2 
};

enum MQTT_Connect
{
  Connect_OK = 0,
  Connect_NOK,
  Connect_NOTACK
};

//数据交互结构体
typedef struct __MQTTMessage
{
    uint32_t qos;
    uint8_t retained;
    uint8_t dup;
    uint16_t id;
	  uint8_t type;
    void *payload;
    int32_t payloadlen;
}MQTTMessage;
//用户接收消息结构体
typedef struct __MQTT_MSG
{
	  uint8_t  msgqos;                 //消息质量
		uint8_t  msg[MSG_MAX_LEN];       //消息
	  uint32_t msglenth;               //消息长度
	  uint8_t  topic[MSG_TOPIC_LEN];   //主题    
	  uint16_t packetid;               //消息ID
	  uint8_t  valid;                  //标明消息是否有效
}MQTT_USER_MSG;

//发送消息结构体
typedef struct
{
    int8_t topic[MSG_TOPIC_LEN];
    int8_t qos;
    int8_t retained;

    uint8_t msg[MSG_MAX_LEN];
    uint8_t msglen;
} mqtt_recv_msg_t, *p_mqtt_recv_msg_t, mqtt_send_msg_t, *p_mqtt_send_msg_t;

/* $dp数据上报类型（旧OneNET MQTT套件协议, 物模型接入后不再使用） */
#define MQTT_DP_TYPE_JSON1      0x01    /* json1: 字符串格式 */
#define MQTT_DP_TYPE_JSON2      0x03    /* json2: 自定义JSON格式 */
#define MQTT_DP_TYPE_BIN        0x05    /* bin:   二进制格式 */

/* 上报JSON负载最大长度 */
#define MQTT_REPORT_JSON_MAX    256

/* 上报类型 */
typedef enum
{
    MQTT_REPORT_PROPERTY = 0,   /* 物模型属性上报(thing/property/post) */
    MQTT_REPORT_EVENT           /* 物模型事件上报(thing/event/post) */
} MQTT_REPORT_TYPE_E;

/* 云平台上报请求结构（cloud_manager组物模型JSON后入队，mqtt_send消费发布） */
typedef struct
{
    uint8_t  type;                         /* 上报类型, 见MQTT_REPORT_TYPE_E */
    uint16_t json_len;                      /* JSON负载长度 */
    char     json[MQTT_REPORT_JSON_MAX];    /* 物模型上报JSON文本 */
} mqtt_report_t;


void mqtt_thread( void *pvParameters);

/************************************************************************
** 函数名称: my_mqtt_send_pingreq								
** 函数功能: 发送MQTT心跳包
** 入口参数: 无
** 出口参数: >=0:发送成功 <0:发送失败
** 备    注: 
************************************************************************/
int32_t MQTT_PingReq(int32_t sock);

/************************************************************************
** 函数名称: MQTT_Connect								
** 函数功能: 登录服务器
** 入口参数: int32_t sock:网络描述符
** 出口参数: Connect_OK:登陆成功 其他:登陆失败
** 备    注: 
************************************************************************/
uint8_t MQTT_Connect(void);

/************************************************************************
** 函数名称: MQTTSubscribe								
** 函数功能: 订阅消息
** 入口参数: int32_t sock：套接字
**           int8_t *topic：主题
**           enum QoS pos：消息质量
** 出口参数: >=0:发送成功 <0:发送失败
** 备    注: 
************************************************************************/
int32_t MQTTSubscribe(int32_t sock,char *topic,enum QoS pos);

/************************************************************************
** 函数名称: UserMsgCtl						
** 函数功能: 用户消息处理函数
** 入口参数: MQTT_USER_MSG  *msg：消息结构体指针
** 出口参数: 无
** 备    注: 
************************************************************************/
void UserMsgCtl(MQTT_USER_MSG  *msg);

/************************************************************************
** 函数名称: GetNextPackID						
** 函数功能: 产生下一个数据包ID
** 入口参数: 无
** 出口参数: uint16_t packetid:产生的ID
** 备    注: 
************************************************************************/
uint16_t GetNextPackID(void);

/************************************************************************
** 函数名称: mqtt_msg_publish						
** 函数功能: 用户推送消息
** 入口参数: MQTT_USER_MSG  *msg：消息结构体指针
** 出口参数: >=0:发送成功 <0:发送失败
** 备    注: 
************************************************************************/
//int32_t MQTTMsgPublish(int32_t sock, char *topic, int8_t qos, int8_t retained,uint8_t* msg,uint32_t msg_len);
int32_t MQTTMsgPublish(int32_t sock, char *topic, int8_t qos, uint8_t* msg,uint16_t msg_len);
/************************************************************************
** 函数名称: ReadPacketTimeout					
** 函数功能: 阻塞读取MQTT数据
** 入口参数: int32_t sock:网络描述符
**           uint8_t *buf:数据缓存区
**           int32_t buflen:缓冲区大小
**           uint32_t timeout:超时时间--0-表示直接查询，没有数据立即返回
** 出口参数: -1：错误,其他--包类型
** 备    注: 
************************************************************************/
int32_t ReadPacketTimeout(int32_t sock,uint8_t *buf,int32_t buflen,uint32_t timeout);

/************************************************************************
** 函数名称: mqtt_pktype_ctl						
** 函数功能: 根据包类型进行处理
** 入口参数: uint8_t packtype:包类型
** 出口参数: 无
** 备    注: 
************************************************************************/
void mqtt_pktype_ctl(uint8_t packtype,uint8_t *buf,uint32_t buflen);

/************************************************************************
** 函数名称: WaitForPacket					
** 函数功能: 等待特定的数据包
** 入口参数: int32_t sock:网络描述符
**           uint8_t packettype:包类型
**           uint8_t times:等待次数
** 出口参数: >=0:等到了特定的包 <0:没有等到特定的包
** 备    注: 
************************************************************************/
int32_t WaitForPacket(int32_t sock,uint8_t packettype,uint8_t times);


void
mqtt_thread_init(void);

/************************************************************************
** 函数名称: mqtt_is_connected							
** 函数功能: 查询MQTT是否已连接云平台
** 入口参数: 无
** 出口参数: 1:已连接在线 0:未连接
** 备    注: 
************************************************************************/
uint8_t mqtt_is_connected(void);

/************************************************************************
** 函数名称: mqtt_post_event
** 函数功能: 上报物模型事件(thing/event/post, 信息型事件)
** 入口参数: const char *event_id: 事件标识符(如"led")
**           const char *params_json: 事件参数JSON(如"{\"switch\":1}")
** 出口参数: 0:入队成功 <0:失败
** 备    注: 消息体由本函数组为 {"id":"xx","params":{...}} 后入队,
**           由mqtt_send线程择机发布
************************************************************************/
int mqtt_post_event(const char *event_id, const char *params_json);

/************************************************************************
** 函数名称: mqtt_desired_get
** 函数功能: 获取属性期望值(发布thing/property/desired/get)
** 入口参数: const char *props_json: 属性名数组JSON(如"[\"led\"]")
** 出口参数: 0:发送成功 <0:失败
** 备    注: 平台通过desired/get/reply回应期望值, 由UserMsgCtl处理
************************************************************************/
int mqtt_desired_get(const char *props_json);

#endif



