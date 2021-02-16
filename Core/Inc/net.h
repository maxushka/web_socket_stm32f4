#ifndef _NET_H
#define _NET_H

#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"
#include "fs.h"
#include "fsdata.h"
#include <string.h>
#include <stdbool.h>

struct webworker
{
  bool head_auth;
  bool auth;
  char token[32];
  char crnt_user[32];
  char *(*getHandler) (char *params);
  char *(*postHandler) (char* url, char *json, struct webworker *web);
};

void http_server_netconn_thread(void * argument);

#endif
