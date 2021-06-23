#ifndef _NET_H
#define _NET_H

#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdbool.h>

/** HTTP configuration -------------------------------------------------------------------------- */
#define HTTP_USE_SDRAM                    0
#define HTTP_USE_AUTH                     0

/** HTTP Memory sizes configuration ------------------------------------------------------------- */
#define HTTP_MAX_CONNECTIONS              6
#define HTTP_REQ_URL_BUFF_SIZE            64
#define HTTP_REQ_DATA_BUFF_SIZE           40960
#define HTTP_RESP_DATA_BUFF_SIZE          40960
#define HTTP_WEBSITE_SIZE                 307200

/** Defining structures ------------------------------------------------------------------------- */
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
  __IO char* (*getHandler) ( char *url );
  __IO char* (*postHandler) ( char* url, char *json );
};

typedef struct 
{
  xSemaphoreHandle semaphore;
  struct netconn *netconn;
  struct webworker *web;
  __IO char request_data[HTTP_REQ_DATA_BUFF_SIZE];
  __IO char resp_js_buff[HTTP_RESP_DATA_BUFF_SIZE];
  __IO char request_url[HTTP_REQ_URL_BUFF_SIZE];
  uint8_t isopen;
  uint32_t resp_js_buff_len;
} net_connection;

/** Addresses HTTP ------------------------------------------------------------------------------ */
#define HTTP_CONNECTION_START_ADDR       (0xC0000000)
#define HTTP_CONNECTION_SIZE             (sizeof(net_connection) * HTTP_MAX_CONNECTIONS)
#define HTTP_CONNECTION_END_ADDR         (HTTP_CONNECTION_START_ADDR + HTTP_CONNECTION_SIZE)

/** Website Addresses --------------------------------------------------------------------------- */
#define HTTP_SDRAM_WEB_SITE_ADDRESS      (HTTP_CONNECTION_END_ADDR)
#define HTTP_SDRAM_WEB_SITE_END_ADDRESS  (HTTP_SDRAM_WEB_SITE_ADDRESS + HTTP_WEBSITE_SIZE)

/** Public macroses ----------------------------------------------------------------------------- */
#define sdram_memcpy(dest,src,size)     {for (int i = 0; i < size; i++) dest[i] = src[i];}

/** Public functions prototypes ----------------------------------------------------------------- */
uint8_t http_create_filesystem ( struct website_file_system *wsfs );
void    http_server_task       ( void * argument );


#endif

/*************************************** END OF FILE **********************************************/
