#ifndef _NET_H
#define _NET_H

#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdbool.h>


#define SDRAM_START_ADDRESS             (0xC0000000)

// ------------------------------------------------ HTTP Size
#define HTTP_MAX_CONNECTIONS         6
#define HTTP_REQ_URL_BUFF_SIZE       64
#define HTTP_REQ_DATA_BUFF_SIZE      40960
#define HTTP_RESP_DATA_BUFF_SIZE     40960
// ------------------------------------------------ WEBSOCKETS Size
#define NET_WS_MAX_CLIENTS               2
#define NET_WS_SEND_BUFFER_SIZE          2048
#define NET_WS_CLIENT_RECV_BUFFER_SIZE   1024
// ------------------------------------------------ Website Size
#define NET_WEBSITE_SIZE                 307200 //300 kB
// ------------------------------------------------ Temp Size
#define NET_TEMP_JSON_LIB_SIZE           1024
#define NET_TEMP_GET_HANDL_SIZE          40960
#define NET_TEMP_POST_HANDL_SIZE         40960
// ------------------------------------------------

#define WS_ID_STRING               0x81
#define WS_ID_BINARY               0x82

typedef enum
{
  WS_STRING = 1,
  WS_BINARY = 2
} ws_msg_type;

typedef struct
{
  uint16_t cmd;
  uint16_t type;
  uint16_t dest;
  uint16_t misc;
  uint32_t len;
  uint8_t msg[512];
} ws_msg;

struct ws_client
{
  TaskHandle_t tHandle;
  struct netconn *accepted_sock;
  void *parent;
  char key[32];
  char concat_key[128];
  char hash[128];
  char hash_base64[128];
  uint8_t mask[4];
  uint8_t raw_data[NET_WS_CLIENT_RECV_BUFFER_SIZE];
  uint8_t recv_buf[NET_WS_CLIENT_RECV_BUFFER_SIZE];
  uint32_t established;
};

struct ws_server
{
  struct ws_client ws_clients[NET_WS_MAX_CLIENTS];
  char *send_buf;
  xSemaphoreHandle semaphore;
  void (*string_callback)(uint8_t *data, uint32_t len);
  void (*binary_callback)(uint8_t *data, uint32_t len);
  uint32_t connected_clients;
};

struct website_file
{
  __IO char file_name[64];
  int offset;
  int page_size;
};

struct website_file_system
{
  __IO uint8_t *flash_addr;
  uint32_t files_cnt;
  __IO struct website_file *files;
};

struct webworker
{
  struct website_file_system wsfs;
  char token[32];
  char crnt_user[32];
  char crnt_hash[32];
  struct ws_server ws;
  __IO char* (*getHandler) ( char *url );
  __IO char* (*postHandler) ( char* url, char *json );
};

typedef struct 
{
  xSemaphoreHandle semaphore;
  struct netconn *netconn;
  struct webworker *web;
  __IO char request_data[NET_HTTP_REQ_DATA_BUFF_SIZE];
  __IO char resp_js_buff[NET_HTTP_RESP_DATA_BUFF_SIZE];
  __IO char request_url[NET_HTTP_REQ_URL_BUFF_SIZE];
  uint8_t isopen;
  uint32_t resp_js_buff_len;
} net_connection;


// ------------------------------------------------ Addresses HTTP
#define NET_HTTP_CONNECTION_START_ADDR  (SDRAM_START_ADDRESS)
#define NET_HTTP_CONNECTION_SIZE        (sizeof(net_connection)*NET_HTTP_MAX_CONNECTIONS)
#define NET_HTTP_CONNECTION_END_ADDR    (NET_HTTP_CONNECTION_START_ADDR+NET_HTTP_CONNECTION_SIZE)
// ------------------------------------------------ Addresses WebSocket
#define NET_WS_SEND_BUFFER_START_ADDR   (NET_HTTP_CONNECTION_END_ADDR)
#define NET_WS_SEND_BUFFER_END_ADDR     (NET_WS_SEND_BUFFER_START_ADDR+NET_WS_SEND_BUFFER_SIZE)

#define NET_WS_CLIENTS_START_ADDR       (NET_WS_SEND_BUFFER_END_ADDR)
#define NET_WS_CLIENTS_SIZE             (sizeof(struct ws_client)*NET_WS_MAX_CLIENTS)
#define NET_WS_CLIENTS_END_ADDR         (NET_WS_CLIENTS_START_ADDR+NET_WS_CLIENTS_SIZE)
// ------------------------------------------------ Website Addresses
#define NET_SDRAM_WEB_SITE_ADDRESS      (NET_WS_CLIENTS_END_ADDR)
#define NET_SDRAM_WEB_SITE_END_ADDRESS  (NET_SDRAM_WEB_SITE_ADDRESS+NET_WEBSITE_SIZE)
// ------------------------------------------------ Temp Addresses
#define NET_TEMP_JSON_LIB_START_ADDR    (NET_SDRAM_WEB_SITE_END_ADDRESS)
#define NET_TEMP_JSON_LIB_END_ADDR      (NET_TEMP_JSON_LIB_START_ADDR+NET_TEMP_JSON_LIB_SIZE)

#define NET_TEMP_GET_HANDL_START_ADDR   (NET_TEMP_JSON_LIB_END_ADDR)
#define NET_TEMP_GET_HANDL_END_ADDR     (NET_TEMP_GET_HANDL_START_ADDR+NET_TEMP_GET_HANDL_SIZE)

#define NET_TEMP_POST_HANDL_START_ADDR  (NET_TEMP_GET_HANDL_END_ADDR)
#define NET_TEMP_POST_HANDL_END_ADDR    (NET_TEMP_POST_HANDL_START_ADDR+NET_TEMP_POST_HANDL_SIZE)


#define sdram_memcpy(dest,src,size)     {for (int i = 0; i < size; i++) dest[i] = src[i];}


uint8_t net_create_filesystem(struct website_file_system *wsfs);
void net_http_server_thread(void * argument);
void netws_send_message(uint16_t cmd, uint16_t dest, uint8_t misc, uint8_t *msg, uint32_t len, ws_msg_type type);

#endif
