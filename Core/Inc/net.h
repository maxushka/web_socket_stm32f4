#ifndef _NET_H
#define _NET_H

#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdbool.h>

#define NET_HTTP_MAX_CONNECTIONS   2
#define NET_REQDATA_BUFF_SIZE      1024

#define WS_ID_STRING               0x81
#define WS_ID_BINARY               0x82

typedef enum
{
  WS_STRING = 1,
  WS_BINARY = 2
} ws_msg_type;

typedef struct
{
  ws_msg_type type;
  uint8_t *msg;
  uint32_t len;
} ws_msg;

struct websocket
{
  struct netconn *accepted_sock;
  char key[32];
  char concat_key[128];
  char hash[512];
  char hash_base64[512];
  uint8_t send_buf[1024];
  void (*string_callback)(uint8_t *data, uint32_t len);
  void (*binary_callback)(uint8_t *data, uint32_t len);
  uint8_t established;
};

struct website_file
{
  char file_name[64];
  int offset;
  int page_size;
};

struct website_file_system
{
  __IO uint8_t *flash_addr;
  uint32_t files_cnt;
  struct website_file *files;
};

struct webworker
{
  struct website_file_system wsfs;
  uint8_t head_auth;
  uint8_t auth;
  char token[32];
  char crnt_user[32];
  struct websocket ws;
  char *(*getHandler) (char *params);
  char *(*postHandler) (char* url, char *json, struct webworker *web);
};

typedef struct 
{
  struct netconn *netconn;
  struct webworker *web;
  char request_data[NET_REQDATA_BUFF_SIZE];
  char request_url[32];
  char resp_js_buff[32];
  uint8_t isopen;
  uint32_t resp_js_buff_len;
} net_connection;

// struct new_connection
// {
//   struct webworker *web;
//   struct netconn *netconn;
//   char request_data[NET_REQDATA_BUFF_SIZE];
//   char request_url[32];
//   char resp_js_buff[32];
//   uint32_t resp_js_buff_len;
// };



uint8_t net_create_filesystem(struct website_file_system *wsfs);
void net_http_server_thread(void * argument);
void netws_send_message(uint8_t *msg, uint32_t len, ws_msg_type type);

#endif
