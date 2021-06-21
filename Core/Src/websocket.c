#include "websocket.h"

  // /** Create WebSocket thread*/
  // if ( sys_thread_new( "websocket", 
  //                      ws_server_thread, 
  //                      (void*)&web->ws, 1024, osPriorityNormal) == NULL )
  //   vTaskDelete(NULL);
  //   
const char *head_ws = "HTTP/1.1 101 Switching Protocols\n\
Upgrade: websocket\n\
Connection: Upgrade\nSec-WebSocket-Accept: \0";
const char *ws_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

xSemaphoreHandle smpr_WsInMsg;
QueueHandle_t wsSendQueue;


static uint8_t get_ws_key                 ( char *buf, char *key );
static void    ws_server_thread           ( void * argument );
static void    ws_send_thread             ( void * argument );
static uint8_t ws_create_binary_len       ( uint32_t len, uint8_t *outbuf );
static void    ws_client_thread           ( void * argument );
static void    ws_init_memory             ( struct ws_server *ws );

void           ws_unmask_input_data       ( uint8_t *masked_buf, 
                                            uint32_t size, 
                                            uint8_t *mask, 
                                            uint8_t *out_buf );
uint8_t        ws_get_binary_pack_len     ( char *buf, 
                                            uint32_t *len, 
                                            uint8_t *masked );
