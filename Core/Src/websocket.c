#include "websocket.h"
#include "mbedtls.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
#include "utils.h"

/** This file variable ------------------------------------------------------ */
const char *head_ws = "HTTP/1.1 101 Switching Protocols\n\
Upgrade: websocket\n\
Connection: Upgrade\nSec-WebSocket-Accept: \0";

const char *ws_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
QueueHandle_t wsSendQueue;

/** Static functions prototypes --------------------------------------------- */
static void    ws_init_memory      ( struct ws_server *ws );
static uint8_t get_ws_key          ( char *buf, char *key );
static void    ws_server_task      ( void * argument );
static void    ws_send_task        ( void * argument );
static uint8_t create_binary_len   ( uint32_t len, uint8_t *outbuf );
static void    ws_client_task      ( void * argument );
static void    unmask_input_data   ( uint8_t *masked_buf, uint32_t size, 
                                     uint8_t *mask, uint8_t *out_buf );
static uint8_t get_binary_pack_len ( char *buf, uint32_t *len, 
                                     uint8_t *masked );


/**
 * Function of starting a websocket server and receiving incoming connections.
 * After starting listening on the port, it starts checking for free threads 
 * that can accept an incoming connection. 
 * When the stream of receiving messages is free, 
 * the structure of the received connection is passed to it.
 * 
 * @param argument ws_server structure (refer websockets.h)
 */
void ws_server_task( void * argument )
{
  struct ws_server *ws = (struct ws_server*)argument;
  struct netconn *ws_con;
  TaskHandle_t SendTaskHandle;

  /* Create a new TCP connection handle */
  ws_con = netconn_new(NETCONN_TCP);
  if (ws_con == NULL)
    vTaskDelete(NULL);

  /* Bind to port (WS) with default IP address */
  if (netconn_bind(ws_con, NULL, WS_PORT) != ERR_OK)
    vTaskDelete(NULL);

  ws_init_memory(ws);
  netconn_listen(ws_con);
  /** 
   * Create a task for sending messages to client
   * Accepted sockets is the argument for a task function
   */
  xTaskCreate(ws_send_task, "send_thread", 255, (void*)ws, 
              osPriorityNormal, &SendTaskHandle);
  
  struct ws_client *new_client;

  for(;;)
  {
    for (int iClient = 0; iClient < WS_MAX_CLIENTS; ++iClient)
    {
      new_client = &(ws->ws_clients[iClient]);
      if (new_client->established == 0)
      {
        if (netconn_accept(ws_con, &new_client->accepted_sock) == ERR_OK)
        {
          /** 
           * Resume the task that will handle the processing 
           * until the connection is broken
           */
          new_client->established = 1;
          vTaskResume(new_client->tHandle);
        }
      }
      else
      {
        osDelay(100);
      }
    }
  }
}

/**
 * Message sending function
 * 
 * @param cmd  - Command id
 * @param misc - Different payload in 1 byte
 * @param msg  - Message aray
 * @param len  - Length of message array
 * @param type - Message type (WS_MSGID_STRING or WS_MSGID_BINARY)
 * 
 * @return     - error (0 - ok, 
 *                      1 - message length is greater than the maximum possible, 
 *                      2 - queue is not free )
 */
uint8_t ws_send_message( uint16_t cmd, uint16_t dest,uint8_t misc, 
                         uint8_t *msg, uint32_t len, ws_msg_type type )
{
  static ws_msg msg_struct;

  if (len >= WS_MSG_BUFFER_SIZE)
    return 1;

  msg_struct.cmd = cmd;
  msg_struct.dest = dest;
  msg_struct.misc = (uint16_t)misc;
  msg_struct.len = len;
  msg_struct.type = type;
  if ( (msg != NULL) && (len != 0) )
    memcpy(msg_struct.msg, msg, len);

  if (wsSendQueue != NULL)
  {
    if (xQueueSend(wsSendQueue, (void*)&msg_struct, 500) != pdTRUE)
      return 2;
  }
  return 0;
}

/**
 * Initialization function for client connection structures and send buffer
 * 
 * @param ws - ws_server structure (refer websockets.h)
 */
static void ws_init_memory( struct ws_server *ws )
{
#if WS_USE_SDRAM == 1
  ws->send_buf = (char *)(WS_SEND_BUFFER_START_ADDR);
#endif

  ws->connected_clients = 0;
  memset((void*)ws->send_buf, 0x00, WS_SEND_BUFFER_SIZE);
  struct ws_client *wscl;

  for (int i = 0; i < NET_WS_MAX_CLIENTS; ++i)
  {
    wscl = &ws->ws_clients[i];
    memset((void*)(wscl), 0x00, sizeof(struct ws_client));
    wscl->parent = (void*)ws;
    xTaskCreate( ws_client_task, 
                 "ws_client", 
                 256, 
                 (void*)wscl, 
                 osPriorityNormal, 
                 &wscl->tHandle );
  }
}

/**
 * Connected client input data processing function.
 * 
 * @param argument ws_client structure (refer websocket.h)
 */
static void ws_client_task( void * argument )
{
  struct ws_client *client = (struct ws_client*)argument;
  struct ws_server *parent = (struct ws_server*)client->parent;
  struct netbuf *inbuf = NULL;
  ws_msg_type msg_type = 0;
  uint16_t size_inbuf = 0;
  uint32_t len = 0;
  uint8_t msk = 0, index_data = 0;
  char* pInbuf = NULL;

  for (;;)
  {
    /** 
     * The created task is in standby mode 
     * until an incoming connection unblocks it
     */
    vTaskSuspend(NULL);

    parent->connected_clients++;
    /** Receive incomming messages */
    while (netconn_recv(client->accepted_sock, &inbuf) == ERR_OK)
    {
      memset(client->raw_data, 0x00, WS_CLIENT_RECV_BUFFER_SIZE);
      netbuf_data(inbuf, (void**)&pInbuf, &size_inbuf);
      memcpy(client->raw_data, (void*)pInbuf, size_inbuf);
      pInbuf = (char*)client->raw_data;

      /** If is handshake */
      if (strncmp((char*)pInbuf, "GET /", 5) == 0)
      {
        /** Get client Sec-WebSocket-Key */
        get_ws_key((char*)pInbuf, client->key);
        /** Concat with const GUID and make answer*/
        sprintf(client->concat_key, "%s%s", client->key, ws_guid);
        /** Calculate binary hash SHA-1 */
        mbedtls_sha1((unsigned char *)client->concat_key, 60, 
                     (unsigned char*)client->hash);
        int baselen = 0;
        /** Make Base64 encode */
        mbedtls_base64_encode((unsigned char*)client->hash_base64, 100, 
                              (size_t*)&baselen, 
                              (unsigned char*)client->hash, 20);
        /** Send response handshake */
        sprintf((char*)parent->send_buf, "%s%s%s", head_ws, 
                                                   client->hash_base64, 
                                                   "\r\n\r\n");

        netconn_write( client->accepted_sock, 
                       (uint8_t*)parent->send_buf, 
                       strlen((char*)parent->send_buf), 
                       NETCONN_NOCOPY );
      }
      /** If is a message */
      else if ( (pInbuf[0] == (char)WS_MSGID_STRING) || 
                (pInbuf[0] == (char)WS_MSGID_BINARY) )
      {
        msg_type = pInbuf[0];
        index_data = get_binary_pack_len( (char*)pInbuf, &len, &msk );
        pInbuf += index_data;
        if (index_data != 0)
        {
          memset(client->recv_buf, 0x00, WS_CLIENT_RECV_BUFFER_SIZE);
          if (msk == 1)
          {
            memcpy(client->mask, pInbuf, sizeof(uint32_t));
            pInbuf += sizeof(uint32_t);
            /** If message is masked */
            unmask_input_data( (uint8_t*)pInbuf, len, client->mask, client->recv_buf);
          }
          else
          {
            memcpy(client->recv_buf, (char*)pInbuf, len);
          }
          /** Calling callback functions depending on the type of message */
          if (msg_type == (uint8_t)WS_ID_STRING)
          {
            parent->string_callback(client->recv_buf, len);
          }
          else if (msg_type == (uint8_t)WS_ID_BINARY)
          {
            parent->binary_callback(client->recv_buf, len);
          }
        }
      }
      netbuf_delete(inbuf);
    }
    client->established = 0;
    parent->connected_clients--;
    netconn_close(client->accepted_sock);
    netconn_delete(client->accepted_sock);
    osDelay(1000);
  }
}

/**
 * Streaming function for sending messages over WebSocket. 
 * The function takes data from the queue, formats it for the protocol 
 * and sends it to all connected clients.
 * 
 * @param argument - ws_server structure (refer to <websockets.h>)
 */
static void ws_send_task( void * argument )
{
  struct ws_server *ws = (struct ws_server*)argument;
  ws_msg msg;
  SysPkg_Typedef pkg = {
    .pack_cnt = 0,
    .misc = 0,
    .dest_id = 0
  };
  uint8_t *payload = NULL;

  wsSendQueue = xQueueCreate( 5, sizeof(ws_msg) );
  if (wsSendQueue == NULL)
    vTaskDelete(NULL);

  struct ws_client *client;

  for (;;)
  {
    if (xQueueReceive( wsSendQueue, &( msg ), pdMS_TO_TICKS(100)) == pdTRUE)
    {
      memset((void*)ws->send_buf, 0x00, WS_SEND_BUFFER_SIZE);

      ws->send_buf[0] = (msg.type == WS_STRING) ? 0x81 : 0x82;
      /** Записываем длину отправляемого пакета в массив */
      uint8_t byte_cnt = create_binary_len(msg.len+sizeof(SysPkg_Typedef), 
                                              (uint8_t*)ws->send_buf+1);

      pkg.cmd = msg.cmd;
      pkg.misc = msg.misc;
      pkg.dest_id = msg.dest;

      payload = (msg.len == 0) ? NULL:msg.msg;
      Utils_CmdCreate(&pkg, payload, msg.len);

      uint8_t *pWriteBuf = (uint8_t*)(ws->send_buf+byte_cnt+1);
      uint8_t *pPkg = (uint8_t*)&pkg;

      memcpy(pWriteBuf, pPkg, sizeof(SysPkg_Typedef));
      pWriteBuf += sizeof(SysPkg_Typedef);
      
      if (payload != NULL)
        memcpy(pWriteBuf, payload, msg.len);

      msg.len = msg.len + byte_cnt + 1 + sizeof(SysPkg_Typedef);

      for (int iClient = 0; iClient < WS_MAX_CLIENTS; ++iClient)
      {
        client = &(ws->ws_clients[iClient]);
        if (client->established == 1)
        {
          netconn_write( client->accepted_sock, 
                         (uint8_t*)ws->send_buf, 
                         msg.len, 
                         NETCONN_NOCOPY );
        }
      }
    }
    else
    {
      osDelay(10);
    }
  }
}

/**
 * Function for getting message length
 * 
 * @param  buf      - Input data array
 * @param  len      - Pointer to length variable 
 * @param  masked   - Pointer to masked flag
 * 
 * @return          - Payload data start index
 */
static uint8_t get_binary_pack_len( char *buf, uint32_t *len, uint8_t *masked )
{
  uint8_t index_data = 2;

  if ( (buf[1] & 0x80) == 0x80 )
    *masked = 1;

  if ( (buf[1] & 0x7F) == 126 )
  {
    index_data = 4;
    *len = buf[2] | buf[3];
  }
  else
  {
    *len = buf[1] & 0x7F;
  }

  return index_data;
}

/**
 * Function for creating an array of the length of the transmit packet
 * 
 * @param  len     - Length number to convert
 * @param  outbuf  - Output array
 * 
 * @return         - The size of the filled array
 */
static uint8_t create_binary_len( uint32_t len, uint8_t *outbuf )
{
  uint8_t bytecnt = 0;
  
  if (len < 126)
  {
    outbuf[bytecnt] = len;
    bytecnt++;
  }
  else
  {
    outbuf[bytecnt] = 126;
    bytecnt++;
    outbuf[bytecnt] = ( ( ((uint16_t)len) >> 8) & 0xFF );
    bytecnt++;
    outbuf[bytecnt] = ( ( (uint16_t)len) & 0xFF );
    bytecnt++;
  }
  
  return bytecnt;
}

/**
 * Function to get Sec-WebSocket-Key string 
 * from WebSocket handshake header
 * 
 * @param  buf - Input data array
 * @param  key - Pointer to key array
 * 
 * @return     - 0 - if get key success,
 *               1 - key not found
 */
static uint8_t get_ws_key( char *buf, char *key )
{
  int err = 1;
  char *p = strstr(buf, "Sec-WebSocket-Key: ");
  if (p)
  {
    p = p + strlen("Sec-WebSocket-Key: ");
    size_t size = strchr(p, '\r') - p;
    strncpy(key, p, size);
    err = 0;
  }
  return err;
}

/**
 * Unmasking function of the input data stream 
 * based on the known array of mask bytes
 * 
 * @param masked_buf - Masked data array
 * @param size       - Size masked data array
 * @param mask       - Pointer to the mask array
 * @param out_buf    - Pointer to unmusking data array
 */
static void unmask_input_data( uint8_t *masked_buf, uint32_t size, 
                               uint8_t *mask, uint8_t *out_buf )
{
  uint8_t mbyte = 0, bmask = 0;
  for (int i = 0; i < size; ++i)
  {
    mbyte = masked_buf[i];
    bmask = mask[i%4];
    out_buf[i] = bmask^mbyte;
  }
}

/***************************** END OF FILE ************************************/
