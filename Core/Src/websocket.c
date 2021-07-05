#include "websocket.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
#include <string.h>

const char *head_ws = "HTTP/1.1 101 Switching Protocols\n\
Upgrade: websocket\n\
Connection: Upgrade\nSec-WebSocket-Accept: \0";

static void       ws_init_client_structs ( ws_server_t *ws );
static void       ws_client_task         ( void * arg );
static char*      create_ws_key_accept   ( char *inbuf );
static char*      get_ws_key             ( char *buf, size_t *len );
static uint32_t   get_message_len        ( uint8_t *msg );
static uint8_t*   get_payload_ptr        ( uint8_t *msg );
static uint8_t    is_masked_msg          ( uint8_t *msg );
static uint8_t*   get_mask               ( uint8_t *msg );
static void       unmask_message_payload ( uint8_t *payload, uint32_t len, uint8_t *mask );
static uint8_t*   ws_set_size_to_frame   ( uint32_t size, uint8_t *out_frame );
static uint8_t*   ws_set_data_to_frame   ( uint8_t *data, uint8_t size, uint8_t *out_frame );

/**
 * Function of starting a websocket server_ptr and receiving incoming connections.
 * When the stream of receiving messages is free, 
 * the structure of the received connection is passed to it.
 * 
 * @param arg ws_server_ptr structure (refer websockets.h)
 */
void ws_server_task( void * arg )
{
  ws_server_t *ws = (ws_server_t*)arg;
  ws_client_t *new_client;

  struct netconn *ws_con = netconn_new(NETCONN_TCP);
  if (ws_con == NULL)
    vTaskDelete(NULL);
  if (netconn_bind(ws_con, NULL, WS_PORT) != ERR_OK)
    vTaskDelete(NULL);
  netconn_listen(ws_con);

#if WS_USE_SDRAM == 1
  ws->send_buf = (char *)(WS_SEND_BUFFER_START_ADDR);
#endif
  memset((void*)ws->send_buf, 0x00, WS_SEND_BUFFER_SIZE);

  ws_init_client_structs(ws);

  for(;;)
  {
    for (int iClient = 0; iClient < WS_MAX_CLIENTS; ++iClient)
    {
      new_client = &ws->ws_clients[iClient];
      if (new_client->established == 0)
      {
        if (netconn_accept(ws_con, &new_client->accepted_sock) == ERR_OK)
        {
          // Resume the task that will handle the processing
          new_client->established = 1;
          vTaskResume(new_client->task_handle);
        }
      }
      else
      {
        osDelay(100);
      }
    }
  }
}

static void ws_init_client_structs( ws_server_t *ws )
{
  ws_client_t *client;

  ws->connected_clients_cnt = 0;
  for (int i = 0; i < WS_MAX_CLIENTS; ++i)
  {
    client = &ws->ws_clients[i];
    memset((void*)(client), 0x00, sizeof(ws_client_t));
    client->server_ptr = (void*)ws;
    xTaskCreate( ws_client_task, "ws_client", 256, (void*)client, 
                 osPriorityNormal, &client->task_handle );
  }
}


static void ws_client_task( void * arg )
{
  ws_client_t *client = (ws_client_t*)arg;
  ws_server_t *server_ptr = (ws_server_t*)client->server_ptr;
  
  struct netbuf *inbuf = NULL;
  uint16_t size_inbuf = 0;
  uint8_t *inbuf_ptr = NULL;

  for (;;)
  {
    // The created task is in standby mode 
    // until an incoming connection unblocks it
    vTaskSuspend(NULL);

    server_ptr->connected_clients_cnt++;

    while (netconn_recv(client->accepted_sock, &inbuf) == ERR_OK)
    {
      memset(client->recv_buf, 0x00, WS_CLIENT_RECV_BUFFER_SIZE);
      netbuf_data(inbuf, (void**)&inbuf_ptr, &size_inbuf);
      memcpy(client->recv_buf, (void*)inbuf_ptr, size_inbuf);
      inbuf_ptr = client->recv_buf;

      // If is handshake
      if (strncmp((char*)inbuf_ptr, "GET /", 5) == 0)
      {
        char *ws_key_accept = create_ws_key_accept( (char*)inbuf_ptr );
        sprintf((char*)server_ptr->send_buf, "%s%s%s", head_ws, 
                                                   ws_key_accept, 
                                                   "\r\n\r\n");
        netconn_write( client->accepted_sock, 
                       server_ptr->send_buf, 
                       strlen((char*)server_ptr->send_buf), 
                       NETCONN_NOCOPY );
      }
      // If is a message
      else if ( (inbuf_ptr[0] & 0x80) == 0x80 )
      {
        uint32_t len = get_message_len( inbuf_ptr );
        uint8_t *payload = get_payload_ptr( inbuf_ptr );
        if (is_masked_msg( inbuf_ptr ) == 1)
        {
          uint8_t *mask = get_mask( inbuf_ptr );
          unmask_message_payload( payload, len, mask );
        }
        server_ptr->msg_handler( payload, len, (ws_type_t)inbuf_ptr[0] );
      }
      netbuf_delete(inbuf);
    }
    client->established = 0;
    server_ptr->connected_clients_cnt--;
    netconn_close(client->accepted_sock);
    netconn_delete(client->accepted_sock);
    osDelay(1000);
  }
}

static char* create_ws_key_accept( char *inbuf )
{
  static char concat_key[64] = {0};
  static char hash[22] = {0}, hash_base64[64] = {0};
  size_t len = 0, baselen = 0;

  memset(concat_key, 0x00, 64);
  memset(hash, 0x00, 22);
  memset(hash_base64, 0x00, 64);

  char *key = get_ws_key(inbuf, &len);
  strncpy(concat_key, key, len);
  strcat(concat_key, WS_GUID);
  mbedtls_sha1((uint8_t*)concat_key, 60, (uint8_t*)hash);
  mbedtls_base64_encode((uint8_t*)hash_base64, 64, &baselen, (uint8_t*)hash, 20);

  return hash_base64;
}

static char* get_ws_key( char *buf, size_t *len )
{
  char *p = strstr(buf, "Sec-WebSocket-Key: ");
  if (p)
  {
    p = p + strlen("Sec-WebSocket-Key: ");
    *len = strchr(p, '\r') - p;
  }
  return p;
}

static uint32_t get_message_len( uint8_t *msg )
{
  uint32_t len = 0;

  if ( (msg[1] & 0x7F) == 126 )
    len = msg[2] | msg[3];
  else
    len = msg[1] & 0x7F;

  return len;
}

static uint8_t* get_payload_ptr( uint8_t *msg )
{
  uint8_t *p = ( (msg[1] & 0x7F) == 126 ) ? (msg + 4) : (msg + 2);
  if (is_masked_msg(msg))
    p += 4;
  return p;
}

static uint8_t is_masked_msg( uint8_t *msg )
{
  if ( (msg[1] & 0x80) == 0x80 )
    return 1;
  return 0;
}

static uint8_t* get_mask( uint8_t *msg )
{
  if ( (msg[1] & 0x7F) == 126 )
    return &msg[4];
  else
    return &msg[2];
}

static void unmask_message_payload( uint8_t *pld, uint32_t len, uint8_t *mask )
{
  for (int i = 0; i < len; ++i)
    pld[i] = mask[i%4] ^ pld[i];
}

void ws_send_message( ws_server_t *ws, ws_msg_t *msg )
{
  uint8_t *outbuf_ptr = ws->send_buf;
  int packet_size = msg->prtcl_size + msg->msg_size;
  ws_client_t *client;

  if (packet_size+7 < WS_SEND_BUFFER_SIZE)
    return;

  memset(outbuf_ptr, 0x00, WS_SEND_BUFFER_SIZE);
  outbuf_ptr[0] = (uint8_t)msg->msg_type;
  outbuf_ptr = ws_set_size_to_frame(packet_size, &outbuf_ptr[1]);
  outbuf_ptr = ws_set_data_to_frame(msg->protocol, msg->prtcl_size, outbuf_ptr);
  outbuf_ptr = ws_set_data_to_frame(msg->message, msg->msg_size, outbuf_ptr);
  packet_size = outbuf_ptr - ws->send_buf;

  for (int iClient = 0; iClient < WS_MAX_CLIENTS; ++iClient)
  {
    client = &(ws->ws_clients[iClient]);
    if (client->established == 1)
    {
      netconn_write( client->accepted_sock, 
                     ws->send_buf, 
                     packet_size, 
                     NETCONN_NOCOPY );
    }
  }
}

static uint8_t* ws_set_size_to_frame( uint32_t size, uint8_t *out_frame )
{
  uint8_t *out_frame_ptr = out_frame;
  if (size < 126)
  {
    *out_frame_ptr = size;
    out_frame_ptr++;
  }
  else
  {
    out_frame_ptr[0] = 126;
    out_frame_ptr[1] = ( ( ((uint16_t)size) >> 8) & 0xFF );
    out_frame_ptr[2] = ( ( (uint16_t)size) & 0xFF );
    out_frame_ptr += 3;
  }
  return out_frame_ptr;
}

static uint8_t* ws_set_data_to_frame( uint8_t *data, uint8_t size, uint8_t *out_frame )
{
  memcpy(out_frame, data, size);
  return (out_frame+size);
}
