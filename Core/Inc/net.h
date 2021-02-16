#ifndef _NET_H
#define _NET_H

#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdbool.h>

#define NET_MAX_CONNECTIONS   5

struct webworker
{
  bool head_auth;
  bool auth;
  char token[32];
  char crnt_user[32];
  char *(*getHandler) (char *params);
  char *(*postHandler) (char* url, char *json, struct webworker *web);
};

struct website_file
{
  char file_name[32];
  int offset;
  int page_size;
};

struct website_file_system
{
  __IO uint8_t *flash_addr;
  uint32_t file_cnt;
  struct website_file *files;
};

void http_server_netconn_thread(void * argument);

#endif
