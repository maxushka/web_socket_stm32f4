#ifndef __WEBSOCKET_H
#define __WEBSOCKET_H

#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdbool.h>

/** Websockets configuration ------------------------------------------------ */
#define WS_USE_SDRAM                 0
#define WS_PORT                      8765
#define WS_MAX_CLIENTS               1
#define WS_SEND_BUFFER_SIZE          1024
#define WS_MSG_BUFFER_SIZE           512
#define WS_CLIENT_RECV_BUFFER_SIZE   1024

typedef enum
{
  WS_MSGID_STRING = 0x81,
  WS_MSGID_BINARY = 0x82
} ws_msg_type;

typedef struct
{
  uint16_t cmd;
  uint16_t type;
  uint16_t dest;
  uint16_t misc;
  uint32_t len;
  uint8_t msg[WS_MSG_BUFFER_SIZE];
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
  uint8_t raw_data[WS_CLIENT_RECV_BUFFER_SIZE];
  uint8_t recv_buf[WS_CLIENT_RECV_BUFFER_SIZE];
  uint32_t established;
};

struct ws_server
{
  struct ws_client ws_clients[WS_MAX_CLIENTS];
#if WS_USE_SDRAM == 1
  char *send_buf;
#else
  char send_buf[WS_SEND_BUFFER_SIZE];
#endif
  void (*string_callback)(uint8_t *data, uint32_t len);
  void (*binary_callback)(uint8_t *data, uint32_t len);
  uint32_t connected_clients;
};

/** ----------------------------------------------------- Addresses WebSocket */
#define WS_SEND_BUFFER_START_ADDR   (0xC0000000)
#define WS_SEND_BUFFER_END_ADDR     (WS_SEND_BUFFER_START_ADDR + WS_SEND_BUFFER_SIZE)

#define WS_CLIENTS_START_ADDR       (WS_SEND_BUFFER_END_ADDR)
#define WS_CLIENTS_SIZE             (sizeof(struct ws_client) * WS_MAX_CLIENTS)
#define WS_CLIENTS_END_ADDR         (WS_CLIENTS_START_ADDR + WS_CLIENTS_SIZE)

/** --------------------------------------------- Public functions prototypes */
void    ws_server_task  (void * argument);
uint8_t ws_send_message ( uint16_t cmd, uint16_t dest, uint8_t misc, 
                          uint8_t *msg, uint32_t len, ws_msg_type type );


#endif

/***************************** END OF FILE ************************************/
