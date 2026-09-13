/********************************************************************************
**
** 文件名:     cJSON_Process.c
** 版权所有:   无
** 文件描述:   该模块主要实现JSON数据的封装与解析
**
*********************************************************************************/

#include "cJSON_Process.h"
#include "main.h"

/*******************************************************************************
** 函数名称    cJSON_Data_Init
** 函数说明    初始化JSON数据
** 输入参数    无
** 输出参数    无
** 返回参数    cJSON根节点指针, NULL: 失败
*******************************************************************************/
cJSON* cJSON_Data_Init(void)
{
  cJSON* cJSON_Root = NULL;    //json根节点
  
  cJSON_Root = cJSON_CreateObject();   /*创建对象*/
  if(NULL == cJSON_Root)
  {
      return NULL;
  }
  cJSON_AddStringToObject(cJSON_Root, NAME, DEFAULT_NAME);  /*添加元素"名称"*/
  cJSON_AddNumberToObject(cJSON_Root, TEMP_NUM, DEFAULT_TEMP_NUM);
  cJSON_AddNumberToObject(cJSON_Root, HUM_NUM, DEFAULT_HUM_NUM);
  
  char* p = cJSON_Print(cJSON_Root);  /*p 指向一串以json格式的数据*/
  
//  PRINT_DEBUG("%s\n",p);
  
  vPortFree(p);
  p = NULL;
  
  return cJSON_Root;
  
}

/*******************************************************************************
** 函数名称    cJSON_Update
** 函数说明    更新JSON数据
** 输入参数    object: cJSON对象指针
**             string: 键名字符串
**             d: 数据指针
** 输出参数    无
** 返回参数    1: 成功
*******************************************************************************/
uint8_t cJSON_Update(const cJSON * const object,const char * const string,void *d)
{
  cJSON* node = NULL;    //json根节点
  node = cJSON_GetObjectItem(object,string);
  if(node == NULL)
    return NULL;
  if(cJSON_IsBool(node))
  {
    int *b = (int*)d;
//    printf ("d = %d",*b);
    cJSON_GetObjectItem(object,string)->type = *b ? cJSON_True : cJSON_False;
//    char* p = cJSON_Print(object);    /*p 指向一串以json格式的数据*/
    return 1;
  }
  else if(cJSON_IsString(node))
  {
    cJSON_GetObjectItem(object,string)->valuestring = (char*)d;
//    char* p = cJSON_Print(object);    /*p 指向一串以json格式的数据*/
    return 1;
  }
  else if(cJSON_IsNumber(node))
  {
    double *num = (double*)d;
//    printf ("num = %f",*num);
//    cJSON_GetObjectItem(object,string)->valueint = (double)*num;
    cJSON_GetObjectItem(object,string)->valuedouble = (double)*num;
//    char* p = cJSON_Print(object);    /*p 指向一串以json格式的数据*/
    return 1;
  }
  else
    return 1;
}

/*******************************************************************************
** 函数名称    Proscess
** 函数说明    解析JSON数据并打印
** 输入参数    data: JSON数据指针
** 输出参数    无
** 返回参数    无
*******************************************************************************/
void Proscess(void* data)
{
  PRINT_DEBUG("开始解析JSON数据");
  cJSON *root,*json_name,*json_temp_num,*json_hum_num;
  root = cJSON_Parse((char*)data); //解析为json格式

  json_name = cJSON_GetObjectItem( root , NAME);  //获取键值对
  json_temp_num = cJSON_GetObjectItem( root , TEMP_NUM );
  json_hum_num = cJSON_GetObjectItem( root , HUM_NUM );

  PRINT_DEBUG("name:%s\n temp_num:%f\n hum_num:%f\n",
              json_name->valuestring,
              json_temp_num->valuedouble,
              json_hum_num->valuedouble);

  cJSON_Delete(root);  //释放内存 
}
