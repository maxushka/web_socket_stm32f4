#ifndef _NET_H
#define _NET_H

#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdbool.h>

/** HTTP configuration */
#define HTTP_USE_SDRAM                    0
#define HTTP_USE_AUTH                     0

/** HTTP Memory sizes configuration */
#define HTTP_MAX_CONNECTIONS              2
#define HTTP_REQ_URL_BUFF_SIZE            64
#define HTTP_REQ_DATA_BUFF_SIZE           4096
#define HTTP_RESP_DATA_BUFF_SIZE          4096
#define HTTP_WEBSITE_SIZE                 307200

typedef struct
{
  char file_name[64];
  int offset;
  int page_size;
} httpFile_t;

typedef struct
{
  uint8_t *flash_addr;
  uint32_t files_cnt;
  httpFile_t *files;
} httpFileSystem_t;

typedef struct 
{
  TaskHandle_t task_handle;
  struct netconn *accepted_sock;
  void *server_ptr;
  char request_data[HTTP_REQ_DATA_BUFF_SIZE];
  char response_data[HTTP_RESP_DATA_BUFF_SIZE];
  uint8_t isopen;
  uint32_t resp_js_buff_len;
} httpConnection_t;

typedef struct
{
  httpFileSystem_t file_system;
#if HTTP_USE_SDRAM == 1
  httpConnection_t *connections_pool[HTTP_MAX_CONNECTIONS];
#else
  httpConnection_t connections_pool[HTTP_MAX_CONNECTIONS];
#endif

  char token[32];
  char crnt_user[32];
  char crnt_hash[32];
  void (*getHandler) ( char *url, char *outbuf );
  char* (*postHandler) ( char* url, char *json, char *outbuf );
} httpServer_t;

/** Addresses HTTP ------------------------------------------------------------------------------ */
#define HTTP_CONNECTION_START_ADDR       (0xC0000000)
#define HTTP_CONNECTION_SIZE             (sizeof(httpConnection_t) * HTTP_MAX_CONNECTIONS)
#define HTTP_CONNECTION_END_ADDR         (HTTP_CONNECTION_START_ADDR + HTTP_CONNECTION_SIZE)

/** Website Addresses --------------------------------------------------------------------------- */
#define HTTP_SDRAM_WEB_SITE_ADDRESS      (HTTP_CONNECTION_END_ADDR)
#define HTTP_SDRAM_WEB_SITE_END_ADDRESS  (HTTP_SDRAM_WEB_SITE_ADDRESS + HTTP_WEBSITE_SIZE)

uint8_t http_create_filesystem( httpFileSystem_t *fs );
void    http_server_task( void * arg );


#endif
