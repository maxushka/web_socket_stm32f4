#ifndef _NET_H
#define _NET_H

#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdbool.h>

#define NET_MAX_CONNECTIONS   5

struct websocket
{
  char key[32];
  char concat_key[128];
  char hash[512];
  char hash_base64[512];
  char send_buf[1024];
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

uint8_t net_create_filesystem(struct website_file_system *wsfs);
void net_http_server_thread(void * argument);

#endif
