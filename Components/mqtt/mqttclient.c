#include "mqttclient.h"
#include "onenet_token.h"
#include "transport.h"
#include "MQTTPacket.h"
#include "FreeRTOS.h"
#include "task.h"
#include "string.h"
#include "sockets.h"

#include "lwip/opt.h"

#include "lwip/sys.h"
#include "lwip/api.h"

#include "lwip/sockets.h"

#include "cJSON_Process.h"
#include "../../Platform/drv_led/bsp_led.h"

/******************************* 全局变量声明 ************************************/
/*
 * 当我们在写应用程序的时候,可能需要用到一些全局变量。
 */
extern QueueHandle_t MQTT_Data_Queue;


//定义用户消息结构体
MQTT_USER_MSG  mqtt_user_msg;

int32_t MQTT_Socket = 0;

/* MQTT在线标志（Client_Connect订阅成功后置1,断链/失败清0） */
static uint8_t s_connected = 0;


void deliverMessage(MQTTString *TopicName,MQTTMessage *msg,MQTT_USER_MSG *mqtt_user_msg);

/************************************************************************
** 函数名称: MQTT_Connect								
** 函数功能: 初始化客户端并登录服务器
** 入口参数: int32_t sock:网络描述符
** 出口参数: >=0:发送成功 <0:发送失败
** 备    注: 
************************************************************************/
uint8_t MQTT_Connect(void)
{
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    /* token静态缓冲: 避免登录路径栈峰值(局部200B+组帧512B逼近任务栈极限) */
    static char token[ONENET_TOKEN_MAX_LEN];
    uint8_t buf[512];
    int buflen = sizeof(buf);
    int len = 0;

    /* 生成OneNET设备鉴权token(HMAC-MD5签名, 未校时用固定过期时间戳) */
    if (onenet_token_build(ONENET_TOKEN_ET_FALLBACK, PRODUCT_ID, DEVICE_NAME,
                           ACCESS_KEY, token, sizeof(token)) != 0)
    {
        PRINT_DEBUG("OneNET token build failed\n");
        return Connect_NOK;
    }
    data.clientID.cstring = CLIENT_ID;              //设备名
    data.keepAliveInterval = KEEPLIVE_TIME;         //保持活跃
    data.username.cstring = USER_NAME;              //用户名(产品ID)
    data.password.cstring = token;                  //鉴权token
    data.MQTTVersion = MQTT_VERSION;                //3表示3.1版本,4表示3.11版本
    data.cleansession = 1;
    //组装消息
    len = MQTTSerialize_connect((unsigned char *)buf, buflen, &data);
    //发送消息
    transport_sendPacketBuffer(buf, len);

    /* 等待连接响应 */
    if (MQTTPacket_read(buf, buflen, transport_getdata) == CONNACK)
    {
        unsigned char sessionPresent, connack_rc;
        if (MQTTDeserialize_connack(&sessionPresent, &connack_rc, buf, buflen) != 1 || connack_rc != 0)
        {
          PRINT_DEBUG("无法连接,错误代码是: %d!\n", connack_rc);
            return Connect_NOK;
        }
        else 
        {
            PRINT_DEBUG("用户名与秘钥验证成功,MQTT连接成功!\n");
            return Connect_OK;
        }
    }
    else
        PRINT_DEBUG("MQTT连接无响应!\n");
        return Connect_NOTACK;
}


/************************************************************************
** 函数名称: MQTT_PingReq								
** 函数功能: 发送MQTT心跳包
** 入口参数: 无
** 出口参数: 0:发送成功 -1:5s超时无响应 -2:无fd可读 -3:非PINGRESP包 -4:发送失败
** 备    注: 
************************************************************************/
int32_t MQTT_PingReq(int32_t sock)
{
	  int32_t len;
		uint8_t buf[200];
		int32_t buflen = sizeof(buf);	 
		fd_set readfd;
	  struct timeval tv;
	  tv.tv_sec = 5;
	  tv.tv_usec = 0;
	
	  FD_ZERO(&readfd);
	  FD_SET(sock,&readfd);			
	
		len = MQTTSerialize_pingreq(buf, buflen);
		if(transport_sendPacketBuffer(buf, len) < 0)
			return -4;    /* 发送失败: socket已被断开 */
	
		//等待可读事件
		if(select(sock+1,&readfd,NULL,NULL,&tv) == 0)
			return -1;	/* 5s内服务器未响应PINGRESP */
		
	  //有可读事件
		if(FD_ISSET(sock,&readfd) == 0)
			return -2;
		
		if(MQTTPacket_read(buf, buflen, transport_getdata) != PINGRESP)
			return -3;	/* 读到的不是PINGRESP(连接被服务器断开等) */
		
		return 0;
	
}


/************************************************************************
** 函数名称: MQTTSubscribe								
** 函数功能: 订阅消息
** 入口参数: int32_t sock：套接字
**           int8_t *topic：主题
**           enum QoS pos：消息质量
** 出口参数: >=0:发送成功 <0:发送失败
** 备    注: 
************************************************************************/
int32_t MQTTSubscribe(int32_t sock,char *topic,enum QoS pos)
{
	  static uint32_t PacketID = 1;
	  uint16_t packetidbk = 0;
	  int32_t conutbk = 0;
		uint8_t buf[100];
		int32_t buflen = sizeof(buf);
	  MQTTString topicString = MQTTString_initializer;  
		int32_t len;
	  int32_t req_qos,qosbk;
	
		fd_set readfd;
	  struct timeval tv;
	  tv.tv_sec = 2;
	  tv.tv_usec = 0;
	
	  FD_ZERO(&readfd);
	  FD_SET(sock,&readfd);		
	
	  //复制主题
    topicString.cstring = (char *)topic;
		//订阅质量
	  req_qos = pos;
	
	  //串行化订阅消息
    len = MQTTSerialize_subscribe(buf, buflen, 0, PacketID++, 1, &topicString, &req_qos);
		//发送TCP数据
	  if(transport_sendPacketBuffer(buf, len) < 0)
				return -1;
	  
    //等待可读事件--等待超时
		if(select(sock+1,&readfd,NULL,NULL,&tv) == 0)
				return -2;
		//有可读事件--没有可读事件
		if(FD_ISSET(sock,&readfd) == 0)
				return -3;

		//等待订阅返回--未收到订阅返回
		if(MQTTPacket_read(buf, buflen, transport_getdata) != SUBACK)
				return -4;	
		
		//拆订阅回应包
		if(MQTTDeserialize_suback(&packetidbk,1, &conutbk, &qosbk, buf, buflen) != 1)
				return -5;
		
		//检测返回数据的正确性
		if((qosbk == 0x80)||(packetidbk != (PacketID-1)))
				return -6;
		
    //订阅成功
		return 0;
}


/************************************************************************
** 函数名称: CloudLedApply
** 函数功能: 应用LED开关期望值: 控灯(绿灯LED2, 避开LED1闪烁任务)+上报led事件
** 入口参数: int sw: 1开 0关
** 出口参数: 无
** 备    注: set下发与desired期望值共用此入口
************************************************************************/
static void CloudLedApply(int sw)
{
    if (sw)
    {
        LED2_ON;
        PRINT_DEBUG("开启led灯\n");
    }
    else
    {
        LED2_OFF;
        PRINT_DEBUG("关闭led灯\n");
    }

    /* 上报led信息型事件(与平台物模型事件定义一致) */
    if (mqtt_post_event("led", sw ? "{\"switch\":1}" : "{\"switch\":0}") != 0)
    {
        PRINT_DEBUG("led event post failed\n");
    }
}

/************************************************************************
** 函数名称: CloudSetReply						
** 函数功能: 应答平台物模型属性设置下发(set_reply, id回填平台下发id)
** 入口参数: MQTT_USER_MSG  *msg：消息结构体指针
** 出口参数: 无
** 备    注: 
************************************************************************/
static void CloudSetReply(MQTT_USER_MSG *msg)
{
    cJSON *root = cJSON_Parse((char *)msg->msg);
    cJSON *json_id;
    char reply[160];
    int len;

    if (root == NULL)
    {
        PRINT_DEBUG("set json parse failed\n");
        return;
    }
    json_id = cJSON_GetObjectItem(root, "id");
    if (json_id == NULL || !cJSON_IsString(json_id))
    {
        cJSON_Delete(root);
        PRINT_DEBUG("set json no id field\n");
        return;
    }

    /* 解析params中的led_switch属性(布尔型, 平台直接下发true/false):
     * {"params":{"led_switch":true}} → 控灯+上报事件 */
    {
        cJSON *params = cJSON_GetObjectItem(root, "params");
        cJSON *led_node = (params != NULL) ? cJSON_GetObjectItem(params, "led_switch") : NULL;

        if (led_node != NULL && (cJSON_IsBool(led_node) || cJSON_IsNumber(led_node)))
        {
            CloudLedApply((led_node->valueint != 0) ? 1 : 0);
        }
    }

    /* 应答格式: {id回填, code=200成功} */
    len = snprintf(reply, sizeof(reply),
                   "{\"id\":\"%s\",\"code\":200,\"msg\":\"success\"}",
                   json_id->valuestring);
    cJSON_Delete(root);
    if (len <= 0 || len >= (int)sizeof(reply))
    {
        return;
    }
    if (MQTTMsgPublish(MQTT_Socket, (char *)TOPIC_SET_REPLY, QOS0,
                       (uint8_t *)reply, (uint16_t)len) < 0)
    {
        PRINT_DEBUG("set_reply publish failed\n");
    }
    else
    {
        PRINT_DEBUG("set_reply publish ok: %s\n", reply);
    }
}

/************************************************************************
** 函数名称: CloudDesiredReply
** 函数功能: 解析desired/get/reply应答, 应用属性期望值
** 入口参数: MQTT_USER_MSG  *msg：消息结构体指针
** 出口参数: 无
** 备    注: 应答格式 {"id":"123","code":200,"data":{"led_switch":true,...}}
************************************************************************/
static void CloudDesiredReply(MQTT_USER_MSG *msg)
{
    cJSON *root = cJSON_Parse((char *)msg->msg);
    cJSON *code_node;
    cJSON *data;

    if (root == NULL)
    {
        PRINT_DEBUG("desired reply parse failed\n");
        return;
    }

    code_node = cJSON_GetObjectItem(root, "code");
    if (code_node != NULL && cJSON_IsNumber(code_node) &&
        code_node->valueint != 0 && code_node->valueint != 200)
    {
        PRINT_DEBUG("desired get refused, code=%d\n", code_node->valueint);
        cJSON_Delete(root);
        return;
    }

    /* 期望值在data字段(布尔型属性直接给值): {"data":{"led_switch":true}} */
    data = cJSON_GetObjectItem(root, "data");
    if (data != NULL)
    {
        cJSON *led_node = cJSON_GetObjectItem(data, "led_switch");

        if (led_node != NULL && (cJSON_IsBool(led_node) || cJSON_IsNumber(led_node)))
        {
            CloudLedApply((led_node->valueint != 0) ? 1 : 0);
        }
        else
        {
            PRINT_DEBUG("desired data has no led_switch value\n");
        }
    }
    cJSON_Delete(root);
}

/************************************************************************
** 函数名称: UserMsgCtl						
** 函数功能: 用户消息处理函数
** 入口参数: MQTT_USER_MSG  *msg：消息结构体指针
** 出口参数: 无
** 备    注: 
************************************************************************/
void UserMsgCtl(MQTT_USER_MSG  *msg)
{
		//这里处理数据只是打印,用户可以在这里添加自己的处理方式
    PRINT_DEBUG("*****收到订阅的消息!******\n");

    if(msg->msglenth > 2)    //只有当消息长度大于2 "{}" 的时候才去处理它 
    {
      switch(msg->msgqos)
      {
        case 0:
              PRINT_DEBUG("MQTT>>消息质量：QoS0\n");
              break;
        case 1:
              PRINT_DEBUG("MQTT>>消息质量：QoS1\n");
              break;
        case 2:
              PRINT_DEBUG("MQTT>>消息质量：QoS2\n");
              break;
        default:
              PRINT_DEBUG("MQTT>>错误的消息质量\n");
              break;
      }
      PRINT_DEBUG("MQTT>>消息主题：%s\n",msg->topic);	
      PRINT_DEBUG("MQTT>>消息类容：%s\n",msg->msg);	
      PRINT_DEBUG("MQTT>>消息长度：%d\n",msg->msglenth);	 

      /* 按物模型topic分发 */
      if (strcmp((char *)msg->topic, TOPIC_SET) == 0)
      {
          /* 平台属性设置下发: 解析id并回set_reply应答 */
          CloudSetReply(msg);
      }
      else if (strcmp((char *)msg->topic, TOPIC_POST_REPLY) == 0)
      {
          /* 属性上报应答 */
          PRINT_DEBUG("property post reply: %s\n", msg->msg);
      }
      else if (strcmp((char *)msg->topic, TOPIC_DESIRED_REPLY) == 0)
      {
          /* desired期望值回应: 解析data并应用 */
          CloudDesiredReply(msg);
      }
      else if (strcmp((char *)msg->topic, TOPIC_EVENT_REPLY) == 0)
      {
          /* 事件上报应答(平台校验结果, 失败时会带错误码) */
          PRINT_DEBUG("event post reply: %s\n", msg->msg);
      }
    }
	  //处理完后销毁数据
	  msg->valid  = 0;
}

/************************************************************************
** 函数名称: GetNextPackID						
** 函数功能: 产生下一个数据包ID
** 入口参数: 无
** 出口参数: uint16_t packetid:产生的ID
** 备    注: 
************************************************************************/
uint16_t GetNextPackID(void)
{
	 static uint16_t pubpacketid = 0;
	 return pubpacketid++;
}

/************************************************************************
** 函数名称: mqtt_msg_publish						
** 函数功能: 用户推送消息
** 入口参数: MQTT_USER_MSG  *msg：消息结构体指针
** 出口参数: >=0:发送成功 <0:发送失败
** 备    注: 
************************************************************************/
int32_t MQTTMsgPublish(int32_t sock, char *topic, int8_t qos, uint8_t* msg, uint16_t msg_len)
{
    int8_t retained = 0;      //保留标志位
    // uint32_t msg_len;         //数据长度
		uint8_t buf[MSG_MAX_LEN];
		int32_t buflen = sizeof(buf),len;
		MQTTString topicString = MQTTString_initializer;
	  uint16_t packid = 0,packetidbk;
	
		//填充主题
	  topicString.cstring = (char *)topic;

	  //填充数据包ID
	  if((qos == QOS1)||(qos == QOS2))
		{ 
			packid = GetNextPackID();
		}
		else
		{
			  qos = QOS0;
			  retained = 0;
			  packid = 0;
		}
     
    // msg_len = strlen((char *)msg);
		//推送消息
		len = MQTTSerialize_publish(buf, buflen, 0, qos, retained, packid, topicString, (unsigned char*)msg, msg_len);
		if(len <= 0)
				return -1;
		if(transport_sendPacketBuffer(buf, len) < 0)	
				return -2;	
		
		//质量等级0,不需要返回
		if(qos == QOS0)
		{
				return 0;
		}
		
		//等级1
		if(qos == QOS1)
		{
				//等待PUBACK
			  if(WaitForPacket(sock,PUBACK,5) < 0)
					 return -3;
				return 1;
			  
		}
		//等级2
		if(qos == QOS2)	
		{
			  //等待PUBREC
			  if(WaitForPacket(sock,PUBREC,5) < 0)
					 return -3;
			  //发送PUBREL
        len = MQTTSerialize_pubrel(buf, buflen,0, packetidbk);
				if(len == 0)
					return -4;
				if(transport_sendPacketBuffer(buf, len) < 0)	
					return -6;			
			  //等待PUBCOMP
			  if(WaitForPacket(sock,PUBREC,5) < 0)
					 return -7;
				return 2;
		}
		//等级错误
		return -8;
}

/************************************************************************
** 函数名称: ReadPacketTimeout					
** 函数功能: 阻塞读取MQTT数据
** 入口参数: int32_t sock:网络描述符
**           uint8_t *buf:数据缓存区
**           int32_t buflen:缓冲区大小
**           uint32_t timeout:超时时间--0-表示直接查询,没有数据立即返回
** 出口参数: -1：错误,其他--包类型
** 备    注: 
************************************************************************/
int32_t ReadPacketTimeout(int32_t sock,uint8_t *buf,int32_t buflen,uint32_t timeout)
{
		fd_set readfd;
	  struct timeval tv;
	  if(timeout != 0)
		{
				tv.tv_sec = timeout;
				tv.tv_usec = 0;
				FD_ZERO(&readfd);
				FD_SET(sock,&readfd); 

				//等待可读事件--等待超时
				if(select(sock+1,&readfd,NULL,NULL,&tv) == 0)
						return -1;
				//有可读事件--没有可读事件
				if(FD_ISSET(sock,&readfd) == 0)
						return -1;
	  }
		//读取TCP/IP事件
		return MQTTPacket_read(buf, buflen, transport_getdata);
}


/************************************************************************
** 函数名称: deliverMessage						
** 函数功能: 接受服务器发来的消息
** 入口参数: MQTTMessage *msg:MQTT消息结构体
**           MQTT_USER_MSG *mqtt_user_msg:用户接受结构体
**           MQTTString  *TopicName:主题
** 出口参数: 无
** 备    注: 
************************************************************************/
void deliverMessage(MQTTString  *TopicName,MQTTMessage *msg,MQTT_USER_MSG *mqtt_user_msg)
{
		//消息质量
		mqtt_user_msg->msgqos = msg->qos;
		//保存消息
		memcpy(mqtt_user_msg->msg,msg->payload,msg->payloadlen);
		mqtt_user_msg->msg[msg->payloadlen] = 0;
		//保存消息长度
		mqtt_user_msg->msglenth = msg->payloadlen;
		//消息主题
		memcpy((char *)mqtt_user_msg->topic,TopicName->lenstring.data,TopicName->lenstring.len);
		mqtt_user_msg->topic[TopicName->lenstring.len] = 0;
		//消息ID
		mqtt_user_msg->packetid = msg->id;
		//标明消息合法
		mqtt_user_msg->valid = 1;		
}


/************************************************************************
** 函数名称: mqtt_pktype_ctl						
** 函数功能: 根据包类型进行处理
** 入口参数: uint8_t packtype:包类型
** 出口参数: 无
** 备    注: 
************************************************************************/
void mqtt_pktype_ctl(uint8_t packtype,uint8_t *buf,uint32_t buflen)
{
	  MQTTMessage msg;
		int32_t rc;
	  MQTTString receivedTopic;
	  uint32_t len;
		switch(packtype)
		{
			case PUBLISH:
        //拆析PUBLISH消息
        if(MQTTDeserialize_publish(&msg.dup,(int*)&msg.qos, &msg.retained, &msg.id, &receivedTopic,
          (unsigned char **)&msg.payload, &msg.payloadlen, buf, buflen) != 1)
            return;	
        //接受消息
        deliverMessage(&receivedTopic,&msg,&mqtt_user_msg);
        
        //消息质量不同,处理不同
        if(msg.qos == QOS0)
        {
           //QOS0-不需要ACK
           //直接处理数据
           UserMsgCtl(&mqtt_user_msg);
           return;
        }
        //发送PUBACK消息
        if(msg.qos == QOS1)
        {
            len =MQTTSerialize_puback(buf,buflen,mqtt_user_msg.packetid);
            if(len == 0)
              return;
            //发送返回
            if(transport_sendPacketBuffer(buf,len)<0)
               return;	
            //返回后处理消息
            UserMsgCtl(&mqtt_user_msg); 
            return;												
        }

        //对于质量2,只需要发送PUBREC就可以了
        if(msg.qos == QOS2)
        {
           len = MQTTSerialize_ack(buf, buflen, PUBREC, 0, mqtt_user_msg.packetid);			                
           if(len == 0)
             return;
           //发送返回
           transport_sendPacketBuffer(buf,len);	
        }		
        break;
			case  PUBREL:				           
        //解析包数据,必须包ID相同才可以
        rc = MQTTDeserialize_ack(&msg.type,&msg.dup, &msg.id, buf,buflen);
        if((rc != 1)||(msg.type != PUBREL)||(msg.id != mqtt_user_msg.packetid))
          return ;
        //收到PUBREL,需要处理并抛弃数据
        if(mqtt_user_msg.valid == 1)
        {
           //返回后处理消息
           UserMsgCtl(&mqtt_user_msg);
        }      
        //串行化PUBCMP消息
        len = MQTTSerialize_pubcomp(buf,buflen,msg.id);	                   	
        if(len == 0)
          return;									
        //发送返回--PUBCOMP
        transport_sendPacketBuffer(buf,len);										
        break;
			case   PUBACK://等级1客户端推送数据后,服务器返回
				break;
			case   PUBREC://等级2客户端推送数据后,服务器返回
				break;
			case   PUBCOMP://等级2客户端推送PUBREL后,服务器返回
        break;
			default:
				break;
		}
}

/************************************************************************
** 函数名称: WaitForPacket					
** 函数功能: 等待特定的数据包
** 入口参数: int32_t sock:网络描述符
**           uint8_t packettype:包类型
**           uint8_t times:等待次数
** 出口参数: >=0:等到了特定的包 <0:没有等到特定的包
** 备    注: 
************************************************************************/
int32_t WaitForPacket(int32_t sock,uint8_t packettype,uint8_t times)
{
	  int32_t type;
		uint8_t buf[MSG_MAX_LEN];
	  uint8_t n = 0;
		int32_t buflen = sizeof(buf);
		do
		{
				//读取数据包
				type = ReadPacketTimeout(sock,buf,buflen,2);
			  if(type != -1)
					mqtt_pktype_ctl(type,buf,buflen);
				n++;
		}while((type != packettype)&&(n < times));
		//收到期望的包
		if(type == packettype)
			 return 0;
		else 
			 return -1;		
}



/************************************************************************
** 函数名称: mqtt_is_connected							
** 函数功能: 查询MQTT是否已连接云平台
** 入口参数: 无
** 出口参数: 1:已连接在线 0:未连接
** 备    注: 
************************************************************************/
uint8_t mqtt_is_connected(void)
{
    return s_connected;
}

/************************************************************************
** 函数名称: mqtt_desired_get
** 函数功能: 获取属性期望值(发布thing/property/desired/get)
** 入口参数: const char *props_json: 属性名数组JSON(如"[\"led\"]")
** 出口参数: 0:发送成功 <0:失败
** 备    注: 平台通过desired/get/reply回应, 由CloudDesiredReply解析应用
************************************************************************/
int mqtt_desired_get(const char *props_json)
{
    static uint32_t s_desired_id = 0;
    char body[128];
    int len;

    if (props_json == NULL)
    {
        return -1;
    }

    /* 请求格式: {"id":"123","version":"1.0","params":["led"]} */
    len = snprintf(body, sizeof(body),
                   "{\"id\":\"%u\",\"version\":\"1.0\",\"params\":%s}",
                   (unsigned)(++s_desired_id), props_json);
    if (len <= 0 || len >= (int)sizeof(body))
    {
        return -1;
    }

    PRINT_DEBUG("desired get publish: %s\n", body);
    if (MQTTMsgPublish(MQTT_Socket, (char *)TOPIC_DESIRED_GET, QOS0,
                       (uint8_t *)body, (uint16_t)len) < 0)
    {
        PRINT_DEBUG("desired get publish failed\n");
        return -1;
    }
    return 0;
}


/************************************************************************
** 函数名称: Client_Connect						
** 函数功能: 连接云平台服务器并完成MQTT登录与订阅
** 入口参数: 无
** 出口参数: 无
** 备    注: 内部循环重试直至连接成功
************************************************************************/
void Client_Connect(void)
{
#if  LWIP_DNS
    ip4_addr_t dns_ip;
    char* host_ip;
#else
    char* host_ip = HOST_NAME;
#endif
  
MQTT_START: 
    s_connected = 0;
  
		//创建网络连接
		PRINT_DEBUG("1.开始连接对应云平台的服务器...\n");
		while(1)
		{
#if  LWIP_DNS
				/* 每次重试重新解析: DHCP未就绪/DNS失败时不能拿到0.0.0.0去连,
				 * 而是等待后重试, 避免用错误IP死循环 */
				if (netconn_gethostbyname(HOST_NAME, &dns_ip) != ERR_OK ||
				    ip_addr_isany(&dns_ip))
				{
						PRINT_DEBUG("DNS解析失败,等待3秒再尝试...\n");
						vTaskDelay(3000);
						continue;
				}
				host_ip = ip_ntoa(&dns_ip);
				PRINT_DEBUG("host name : %s , host_ip : %s\n",HOST_NAME,host_ip);
#endif
				PRINT_DEBUG("服务器IP地址: %s,端口号：%0d!\n",host_ip,HOST_PORT);
				//连接服务器
				MQTT_Socket = transport_open((int8_t*)host_ip,HOST_PORT);
				//如果连接服务器成功
				if(MQTT_Socket >= 0)
				{
						PRINT_DEBUG("连接云平台服务器成功!\n");
						break;
				}
				PRINT_DEBUG("连接云平台服务器失败,等待3秒再尝试重新连接!\n");
				//等待3秒
				vTaskDelay(3000);
		}
    
    PRINT_DEBUG("2.MQTT用户名与秘钥验证登陆...\n");
    //MQTT用户名与秘钥验证登陆
    if(MQTT_Connect() != Connect_OK)
    {
         //重连服务器
         PRINT_DEBUG("MQTT用户名与秘钥验证登陆失败...\n");
          //关闭链接
         transport_close();
         goto MQTT_START;	 
    }
    
		//订阅消息
		PRINT_DEBUG("3.开始订阅消息...\n");
//    //订阅消息

    /* 物模型接入订阅3个topic: 属性设置下发/上报应答/desired回应 */
    if(MQTTSubscribe(MQTT_Socket,(char *)TOPIC_SET,QOS1) < 0)
    {
         //重连服务器
         PRINT_DEBUG("客户端订阅消息失败...\n");
          //关闭链接
         transport_close();
         goto MQTT_START;	   
    }	

    if(MQTTSubscribe(MQTT_Socket,(char *)TOPIC_POST_REPLY,QOS1) < 0)
    {
         //重连服务器
         PRINT_DEBUG("客户端订阅消息失败...\n");
          //关闭链接
         transport_close();
         goto MQTT_START;	   
    }	

    if(MQTTSubscribe(MQTT_Socket,(char *)TOPIC_DESIRED_REPLY,QOS1) < 0)
    {
         //重连服务器
         PRINT_DEBUG("客户端订阅消息失败...\n");
          //关闭链接
         transport_close();
         goto MQTT_START;	   
    }	

    if(MQTTSubscribe(MQTT_Socket,(char *)TOPIC_EVENT_REPLY,QOS1) < 0)
    {
         //重连服务器
         PRINT_DEBUG("客户端订阅消息失败...\n");
          //关闭链接
         transport_close();
         goto MQTT_START;	   
    }	
    
    /* 4个topic均收到SUBACK, 订阅成功 */
    PRINT_DEBUG("物模型topic订阅成功(property/set+post/reply+desired/get/reply+event/post/reply)\n");
    
    //连接并订阅成功,置在线标志
    s_connected = 1;

    /* 上线后主动拉取一次led_switch期望值(desired/get, 由reply异步应用) */
    (void)mqtt_desired_get("[\"led_switch\"]");

		//无限循环
		PRINT_DEBUG("4.开始循环接收订阅的消息...\n");

}

/************************************************************************
** 函数名称: mqtt_thread								
** 函数功能: MQTT任务
** 入口参数: void *pvParameters：任务参数
** 出口参数: 无
** 备    注: MQTT连云步骤：
**           1.连接对应云平台的服务器
**           2.MQTT用户与秘钥验证登陆
**           3.订阅指定主题
**           4.等待接收主题的数据与上报主题数据
************************************************************************/
void mqtt_thread(void *pvParameters)
{
	  uint32_t curtick;
		uint8_t no_mqtt_msg_exchange = 1;
		uint8_t buf[MSG_MAX_LEN];
		int32_t buflen = sizeof(buf);
    int32_t type;
    int32_t ping_ret;
    fd_set readfd;
	  struct timeval tv;      //等待时间
	  tv.tv_sec = 1;
	  tv.tv_usec = 0;

  
MQTT_START: 
    //开始连接
    Client_Connect();
    //获取当前滴答,作为心跳包起始时间
		curtick = xTaskGetTickCount();
		while(1)
		{
				//表明无数据交换
				no_mqtt_msg_exchange = 1;
			
				//推送消息
				FD_ZERO(&readfd);
				FD_SET(MQTT_Socket,&readfd);						  

				//等待可读事件
				select(MQTT_Socket+1,&readfd,NULL,NULL,&tv);
				
//				//判断MQTT服务器是否有数据
				if(FD_ISSET(MQTT_Socket,&readfd) != 0)
				{
						//读取数据包--注意这里参数为0,不阻塞
						type = ReadPacketTimeout(MQTT_Socket,buf,buflen,0);
						if(type != -1)
						{
								mqtt_pktype_ctl(type,buf,buflen);
								//表明有数据交换
								no_mqtt_msg_exchange = 0;
								//获取当前滴答,作为心跳包起始时间
								curtick = xTaskGetTickCount();
						}
						else
						{
								//连接已被服务器断开, 立即重连而不是等心跳失败才发现
								PRINT_DEBUG("服务器断开连接,准备重连...\n");
								goto CLOSE;
						}
				}

        //这里主要目的是定时向服务器发送PING保活命令
        //心跳周期取keepalive的1/3(20s): 平台要求60s内必须发PINGREQ,
        //30s是判定边界无余量, 20s留出安全余量
        if((xTaskGetTickCount() - curtick) >(KEEPLIVE_TIME/3*1000))
        {
            curtick = xTaskGetTickCount();
            //判断是否有数据交换
            if(no_mqtt_msg_exchange == 0)
            {
               //如果有数据交换,这次就不需要发送PING消息
               continue;
            }
            
            ping_ret = MQTT_PingReq(MQTT_Socket);
            if(ping_ret < 0)
            {
               //重连服务器
               PRINT_DEBUG("发送保持活性ping失败, code=%d....\n", (int)ping_ret);
               goto CLOSE;	 
            }
            
            //心跳成功
            PRINT_DEBUG("发送保持活性ping作为心跳成功....\n");
            //表明有数据交换
            no_mqtt_msg_exchange = 0;
        }   
		}

CLOSE:
   //关闭链接
   s_connected = 0;
   transport_close();
   //重新链接服务器
   goto MQTT_START;	
}

/************************************************************************
** 函数名称: mqtt_post_event
** 函数功能: 上报物模型事件(thing/event/post, 信息型事件)
** 入口参数: const char *event_id: 事件标识符(如"led")
**           const char *params_json: 事件参数JSON(如"{\"switch\":1}")
** 出口参数: 0:入队成功 <0:失败
** 备    注: 消息体组为 {"id":"xx","params":{...}} 后入队, 由mqtt_send发布
************************************************************************/
int mqtt_post_event(const char *event_id, const char *params_json)
{
    mqtt_report_t *report;
    int len;

    if (event_id == NULL || params_json == NULL || MQTT_Data_Queue == NULL)
    {
        return -1;
    }

    report = (mqtt_report_t *)pvPortMalloc(sizeof(mqtt_report_t));
    if (report == NULL)
    {
        return -1;
    }

    report->type = MQTT_REPORT_EVENT;
    len = snprintf(report->json, sizeof(report->json),
                   "{\"id\":\"%s\",\"params\":%s}", event_id, params_json);
    if (len <= 0 || len >= (int)sizeof(report->json))
    {
        vPortFree(report);
        return -1;
    }
    report->json_len = (uint16_t)len;

    if (xQueueSend(MQTT_Data_Queue, &report, 0) != pdTRUE)
    {
        /* 队列满丢弃 */
        vPortFree(report);
        return -1;
    }

    PRINT_DEBUG("event post queued: %s\n", report->json);
    return 0;
}

void mqtt_send(void *pvParameters)
{
    mqtt_report_t *report;

    (void)pvParameters;

    PRINT_DEBUG("mqtt send thread started\n");

    for (;;)
    {
        /* 等待cloud_manager上报请求 */
        if (xQueueReceive( MQTT_Data_Queue,    /* 消息队列的句柄 */
                           &report,            /* 收到的上报请求 */
                           portMAX_DELAY) == pdTRUE)
        {
            if (report != NULL)
            {
                /* 注意: lwIP第一个socket的fd是0, 必须用>=0判断,
                 * 否则fd=0时所有上报都会被误丢弃 */
                if (mqtt_is_connected() && MQTT_Socket >= 0)
                {
                    /* 按上报类型选择topic: 属性→property/post, 事件→event/post */
                    char *topic = (report->type == MQTT_REPORT_EVENT) ?
                                   (char *)TOPIC_EVENT_POST : (char *)TOPIC_POST;

                    PRINT_DEBUG("report to %s, len=%d\n", topic, report->json_len);
                    PRINT_DEBUG("report json: %.*s\n",
                                report->json_len, (char *)report->json);
                    if (MQTTMsgPublish(MQTT_Socket, topic, QOS0,
                                       (uint8_t*)report->json, report->json_len) < 0)
                    {
                        PRINT_DEBUG("publish failed\n");
                    }
                }
                else
                {
                    PRINT_DEBUG("mqtt offline, drop report\n");
                }
                vPortFree(report);
            }
        }
    }
}

void
mqtt_thread_init(void)
{
  sys_thread_new("mqtt_thread", mqtt_thread, NULL, 512, 6);
  sys_thread_new("mqtt_send", mqtt_send, NULL, 1024, 7);
}

