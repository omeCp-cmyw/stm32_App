#include "transport.h"
#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "string.h"

static int mysock;

/************************************************************************
** ��������: transport_sendPacketBuffer									
** ��������: ��TCP��ʽ��������
** ��ڲ���: unsigned char* buf�����ݻ�����
**           int buflen�����ݳ���
** ���ڲ���: <0��������ʧ��							
************************************************************************/
int32_t transport_sendPacketBuffer( uint8_t* buf, int32_t buflen)
{
	int32_t rc;
	rc = write(mysock, buf, buflen);
	return rc;
}

/************************************************************************
** ��������: transport_getdata									
** ��������: �������ķ�ʽ����TCP����
** ��ڲ���: unsigned char* buf�����ݻ�����
**           int count�����ݳ���
** ���ڲ���: <=0��������ʧ��									
************************************************************************/
int32_t transport_getdata(uint8_t* buf, int32_t count)
{
	int32_t rc;
	//������������ﲻ����
  rc = recv(mysock, buf, count, 0);
	return rc;
}



/************************************************************************
** ��������: transport_open									
** ��������: ��һ���ӿڣ����Һͷ����� ��������
** ��ڲ���: char* servip:����������
**           int   port:�˿ں�
** ���ڲ���: <0������ʧ��										
************************************************************************/
int32_t transport_open(int8_t* servip, int32_t port)
{
	int32_t *sock = &mysock;
	int32_t ret;
//	int32_t opt;
	struct sockaddr_in addr;
	
	//��ʼ����������Ϣ
	memset(&addr,0,sizeof(addr));
	addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;
	//��д�������˿ں�
	addr.sin_port = PP_HTONS(port);
	//��д������IP��ַ
	addr.sin_addr.s_addr = inet_addr((const char*)servip);
	
	//����SOCK
	*sock = socket(AF_INET,SOCK_STREAM,0);
	//���ӷ����� 
	ret = connect(*sock,(struct sockaddr*)&addr,sizeof(addr));
	if(ret != 0)
	{
		 //�ر�����
		 close(*sock);
		 //����ʧ��
		 return -1;
	}
	//���ӳɹ�,���ó�ʱʱ��1000ms
//	opt = 1000;
//	setsockopt(*sock,SOL_SOCKET,SO_RCVTIMEO,&opt,sizeof(int));
	
	//�����׽���
	return *sock;
}


/************************************************************************
** ��������: transport_close									
** ��������: �ر��׽���
** ��ڲ���: unsigned char* buf�����ݻ�����
**           int buflen�����ݳ���
** ���ڲ���: <0��������ʧ��							
************************************************************************/
int transport_close(void)
{
	struct linger ling;

	/* RST立即关闭: 对端无响应时lwIP的close会阻塞等FIN重传超时(分钟级),
	 * 期间mqtt线程卡死无法重连, SO_LINGER{1,0}强制RST立即释放socket */
	ling.l_onoff = 1;
	ling.l_linger = 0;
	setsockopt(mysock, SOL_SOCKET, SO_LINGER, (const void *)&ling, sizeof(ling));

	return close(mysock);
}
