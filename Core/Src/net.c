#include "net.h"
#include "mbedtls.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
#include "utils.h"

extern uint8_t SELF_NET_ID;

char *head_js_resp = "HTTP/1.1 200 OK\n\
                      Connection: close\n\
                      Access-Control-Allow-Origin: *\n\
                      Content-Type: application/json; charset=utf-8\n\n";
char *head_ok_resp = "HTTP/1.1 200 OK\n\
                      Connection: close\r\n\r\n";
char *head_not_found = "HTTP/1.1 404 Not Found\r\n\r\n";

char *head_ws = "HTTP/1.1 101 Switching Protocols\nUpgrade: websocket\nConnection: Upgrade\nSec-WebSocket-Accept: ";

const char *ws_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

net_connection net_conn_pool[NET_HTTP_MAX_CONNECTIONS] = {0};
QueueHandle_t wsSendQueue;
QueueHandle_t http_recv_queue;

static uint8_t net_create_receive_threads(void);
static uint8_t cmp_cookie_token(char *pBuffer, char *token);
static void http_receive_handler(void * argument);

static void ws_server_thread(void * argument);
static uint8_t get_ws_key(char *buf, char *key);
static void ws_send_thread(void * argument);
static uint8_t ws_create_binary_len(uint32_t len, uint8_t *outbuf);
static uint8_t ws_get_binary_pack_len(uint8_t *buf, uint32_t *len, uint8_t **pPayload);


static void ws_unmask_input_data(uint8_t *masked_buf, uint32_t size, uint8_t *mask, uint8_t *out_buf);
static void ws_client_thread(void * argument);

/**
 * The function of creating a file structure of the site
 * @param  wsfs web site structure (refer to <net.h>)
 * @return      0 - errors none
 *              1 - http file system not created
 */
uint8_t net_create_filesystem(struct website_file_system *wsfs)
{
  uint8_t err = 1;
  /** Get a count files of website and make site catalogue */
  memcpy(&wsfs->files_cnt, (uint8_t*)wsfs->flash_addr, sizeof(uint32_t));
  if ( (wsfs->files_cnt > 0) && (wsfs->files_cnt < 0xFFFFFFFF))
  {
    int size_files = sizeof(struct website_file)*wsfs->files_cnt;
    wsfs->files = pvPortMalloc(size_files);
    if (wsfs->files != NULL)
    {
      memcpy(wsfs->files, (uint8_t*)wsfs->flash_addr+sizeof(uint32_t), size_files);
      err = 0;
    }
  }
  return err;
}

/**
 * [net_create_receive_threads description]
 * 
 * @return  0 - Errors none
 *          1 - xTaskCreate fail
 *          2 - xQueueCreate fail
 */
static uint8_t net_create_receive_threads(void)
{
  uint8_t err = 0;
  http_recv_queue = xQueueCreate( NET_HTTP_MAX_CONNECTIONS , sizeof( net_connection* ));
  if (http_recv_queue == NULL)
    return 1;
  for (int i = 0; i < NET_HTTP_MAX_CONNECTIONS; ++i)
  {
    sys_thread_new("http_recv", http_receive_handler, NULL, 128, osPriorityNormal);
  }
  return err;
}

/**
 * Function for comparing the "token" parameter in the Cookie field.
 * When creating an http stream, a random number is generated. 
 * The unauthorized client does not know it and 
 * therefore will be redirected to the authorization page. 
 * After authorization, the "token" parameter will appear in the Cookie field.
 * 
 * @param  pBuffer Pointer to array of request header
 * @param  token   Token generated on the server side
 * @return         0 - User is not logged in
 *                 1 - Success login
 */
static uint8_t cmp_cookie_token(char *pBuffer, char *token)
{
  uint8_t auth = 0;
  char *pAuthHead = strstr(pBuffer, "Cookie:");
  if (pAuthHead)
  {
    /** Detaching field <token=> */
    char *pToken = strstr(pAuthHead, "token=");
    if (pToken)
    {
      /** Allocating a separate space for the strtok function */
      char *tmp = pvPortMalloc(32);
      if (tmp)
      {
        strncpy(tmp, pToken+strlen("token="), 30);
        /** Get a token value */
        pToken = strtok(tmp, "\r");
        /** Client token validation with token issued by server */
        if (strcmp(pToken, token) == 0)
          auth = 1;
        else
          auth = 0;
        vPortFree(tmp);
      }
    }
  }
  return auth;
}

/**
 * Function of starting an http server and receiving incoming connections.
 * After starting listening on the port, it starts checking for free threads 
 * that can accept an incoming connection. 
 * When the stream of receiving messages is free, 
 * the structure of the received connection is passed to it.
 * 
 * @param argument webworker structure (refer net.h)
 */
void net_http_server_thread(void * argument)
{ 
  struct netconn *conn;
  struct webworker *web = (struct webworker*)argument;
  err_t err;

  /* Create a new TCP connection handle */
  conn = netconn_new(NETCONN_TCP);
  if (conn == NULL)
    vTaskDelete(NULL);

  /* Bind to port 80 (HTTP) with default IP address */
  err = netconn_bind(conn, NULL, 80);
  if (err != ERR_OK)
    vTaskDelete(NULL);

  if (net_create_receive_threads() != 0)
    vTaskDelete(NULL);

  /** Create WebSocket thread*/
  sys_thread_new("websocket", ws_server_thread, (void*)&web->ws, 512, osPriorityNormal);
  /* Put the connection into LISTEN state */
  netconn_listen(conn);

  for(;;) 
  {
    /** Check available task and newconn */
    for (int iTask = 0; iTask < NET_HTTP_MAX_CONNECTIONS; iTask++)
    {
      net_connection *nct = &net_conn_pool[iTask];
      if (nct->web == NULL)
        nct->web = web;
      /** If connection currently not work */
      if (nct->isopen == 0)
      {
        /* Accept icoming connection and create task for it */
        if (netconn_accept(conn, &nct->netconn) == ERR_OK)
        {
          nct->isopen = 1;
          if (xQueueSend(http_recv_queue, (void*)&nct, 10) != pdTRUE)
          {
            nct->isopen = 0;
            netconn_close(nct->netconn);
            netconn_delete(nct->netconn);
          }
        }
      }
    }
  }
}

/**
 * [http_receive_handler description]
 * 
 * @param argument [description]
 */
static void http_receive_handler(void * argument)
{
  net_connection *net;
  struct netbuf *inbuf = NULL;
  char* pBuffer = NULL;
  uint16_t buflen = 0;
  uint8_t isAuth = 0;

  for(;;)
  {
    xQueueReceive(http_recv_queue, &( net ), portMAX_DELAY);
    
    if (netconn_recv(net->netconn, &inbuf) == ERR_OK)
    {
      netbuf_data(inbuf, (void**)&pBuffer, &buflen);
      isAuth = cmp_cookie_token(pBuffer, net->web->token);
      
      isAuth = 1; //Заглушка!!!

      if (strncmp(pBuffer, "GET /", 5) == 0)
      {
        /** Copy request body */
        /** TODO: check request_data length buffer and buflen */
        memset(net->request_data, 0x00, NET_REQDATA_BUFF_SIZE);
        uint8_t get_offset = strlen("GET ");
        strncpy(net->request_data, pBuffer+get_offset, buflen-get_offset);
        /** Get the page name */
        char *pPage = strtok((char*)net->request_data, " ");
        if (pPage)
        {
          if (strcmp(pPage, "/") == 0)
            strcpy((char*)net->request_url, "/index.html");
          else
            strcpy((char*)net->request_url, pPage);
        }

        /* If the request does not start with .api, 
         * then this is a page request */
        if (strstr((char*)net->request_url, "api") == NULL)
        {
          int file_not_found = 1;
          /** If the authorization is not successful, 
          the client is redirected to the authorization page */
          if (isAuth == 0)
          {
            sprintf((char*)net->request_url, "/auth.html");
          }
          /** Search for file name in structure website_file_system */
          for (int iFile = 0; iFile < net->web->wsfs.files_cnt; iFile++)
          {
            if ( strcmp((char*)net->request_url+1, 
                        net->web->wsfs.files[iFile].file_name) == 0 )
            {
              file_not_found = 0;
              /** Returning the contents of the requested file */
              netconn_write(net->netconn, (unsigned char*)net->web->wsfs.flash_addr+net->web->wsfs.files[iFile].offset, 
                           net->web->wsfs.files[iFile].page_size, NETCONN_COPY);
              osDelay(10);
              break;
            }
          }
          if (file_not_found == 1)
            netconn_write(net->netconn, head_not_found, strlen(head_not_found), NETCONN_COPY);
        }
        /** If this is a custom GET request */
        else
        {
          memset((char*)net->resp_js_buff, 0, net->resp_js_buff_len);
          char *resp_json = net->web->getHandler((char*)net->request_url);
          strcat((char*)net->resp_js_buff, head_js_resp);
          strcat((char*)net->resp_js_buff, resp_json);
          netconn_write(net->netconn, net->resp_js_buff, 
                        strlen((char*)net->resp_js_buff), NETCONN_NOCOPY);
        }
      }

      /** Если это POST-запрос */
     //  else if( (strncmp(pBuffer, "POST /", 6) == 0) )
     //  {
     //    memset(net->request_data, 0x00, NET_REQDATA_BUFF_SIZE)
     //    uint8_t post_offset = strlen("POST ");
     //    strncpy(net->request_data, pBuffer+post_offset, buflen-post_offset);

     //   // struct netbuf *in;
     //   // strcpy((char*)data, );
     //   char *url = strtok((char*)net->request_data, " ");
     //   /** Ищем указатель на входящую JSON-строку */
     //   char *pContLen = strstr((char*)net->request_data, "Content-Length: ");
     //   pContLen = pContLen+strlen("Content-Length: ");
     //   json_input_size = strchr(pContLen, ' ') - pContLen;
     //   char __IO *ContentLenArr = (char __IO*)(0xC050F000);//pvPortMalloc(4);
     //   memset((char*)ContentLenArr, 0x00, 4);
     //   //if (ContentLenArr != NULL)
     //   {
     //     memcpy((char*)ContentLenArr, pContLen, json_input_size);
     //     json_input_size = atoi((char*)ContentLenArr);
     //   }
     //   //else
     //     //json_input_size = 0;
     //   printf("json_input_size: %d\n", json_input_size);
     //   char *pContent = strstr((char*)tmp_post, "{\"json\":");
     //   /** Вычитываем всю полезную нагрузку в массив <tmp_post> */
     //   //if (strlen(pContent) < json_input_size)
     //   {
     //     while (strlen(pContent) < json_input_size)//do
     //     {
     //       err_t res = netconn_recv(net->netconn, &in);
     //       netbuf_data(in, (void**)&pBuffer, &buflen);
     //       strncat((char*)tmp_post, pBuffer, buflen);
     //       netbuf_delete(in);
     //       pContent = strstr((char*)tmp_post, "{\"json\":");
     //     } 
     //   }
     //   /** Вызываем обработчик POST-запроса и передаем ему URL и JSON-строку */
     //   char *response = web->postHandler(url, pContent+strlen("{\"json\":"), web);
     //   /** Если postHandler возвращает указатель на данные */
     //   if (response != NULL)
     //   {
     //     /** То совмещаем их с заголовком и возвращаем клиенту */
     //     memset((char*)tmp_post, 0x00, 2048);
     //     strcat((char*)tmp_post, head_ok_resp);
     //     strcat((char*)tmp_post, response);
     //     size_t size_send = strlen((char*)tmp_post);
     //     netconn_write(net->netconn, (const unsigned char*)(tmp_post), size_send, NETCONN_NOCOPY);
     //     printf("return post netcon\n");
     //     osDelay(10);
     //   }
     //   /** Иначе отправляем просто заголовок OK */
     //   else
     //   {
     //     netconn_write(net->netconn, (const unsigned char*)(head_ok_resp), (size_t)strlen(head_ok_resp), NETCONN_COPY);
     //   }
     // }
      
    }
    /** Close connection, clear netbuf and connection */
    netconn_close(net->netconn);
    netbuf_delete(inbuf);
    netconn_delete(net->netconn);
    /** Now the task not work */
    net->isopen = 0;
  }
}

/**
 * [ws_server_netconn_thread description]
 * @param argument [description]
 */
static void ws_server_thread(void * argument)
{
  struct ws_server *ws = (struct ws_server*)argument;
  struct netconn *ws_con;
  TaskHandle_t SendTaskHandle;

  /* Create a new TCP connection handle */
  ws_con = netconn_new(NETCONN_TCP);
  if (ws_con == NULL)
    vTaskDelete(NULL);

  /* Bind to port (WS) with default IP address */
  if (netconn_bind(ws_con, NULL, 8765) != ERR_OK)
    vTaskDelete(NULL);

  netconn_listen(ws_con);

  /** 
   * Create a task for sending messages to client
   * Accepted socket is the argument for a task function
   */
  xTaskCreate(ws_send_thread, "send_thread", 255, 
   (void*)ws, osPriorityNormal, &SendTaskHandle);
  
  struct ws_client *new_client;

  for(;;)
  {
    for (int iClient = 0; iClient < 2; ++iClient)
    {
      new_client = &ws->ws_client[iClient];
      if (new_client->established == 0)
      {
        if (netconn_accept(ws_con, &new_client->accepted_sock) == ERR_OK)
        {
          new_client->established = 1;
          ws->connected_clients++;
          new_client->parent = (void*)ws;
          sys_thread_new("ws_client", ws_client_thread, (void*)new_client, 128, osPriorityNormal);
        }
      }
      else
      {
        osDelay(100);
      }
    }
  }
}


static void ws_client_thread(void * argument)
{
  struct ws_client *client = (struct ws_client*)argument;
  struct ws_server *parent = (struct ws_server*)client->parent;
  struct netbuf *inbuf = NULL;
  char* pInbuf = NULL;
  uint16_t size_inbuf = 0;

  for (;;)
  {
    /** Receive incomming messages */
    while (netconn_recv(client->accepted_sock, &inbuf) == ERR_OK)
    {
      netbuf_data(inbuf, (void**)&pInbuf, &size_inbuf);
      if (strncmp(pInbuf, "GET /", 5) == 0)
      {
        /** Get client Sec-WebSocket-Key */
        get_ws_key(pInbuf, client->key);
        /** Concat with const GUID and make answer*/
        sprintf(client->concat_key, "%s%s", client->key, ws_guid);
        mbedtls_sha1((unsigned char *)client->concat_key, 60, 
                     (unsigned char*)client->hash);
        int len = 0;
        mbedtls_base64_encode((unsigned char*)client->hash_base64, 100, 
                              (size_t*)&len, (unsigned char*)client->hash, 20);
        /** Send response handshake */
        sprintf((char*)parent->send_buf, "%s%s%s", head_ws, client->hash_base64, "\r\n\r\n");
        netconn_write(client->accepted_sock, parent->send_buf, strlen((char*)parent->send_buf), NETCONN_NOCOPY);
        //client->established = 1;
      }
      else
      {
        uint8_t *pPayload;
        uint32_t len = 0;
        if (ws_get_binary_pack_len((uint8_t*)pInbuf, &len, &pPayload) == 1)
        {
          memcpy(client->mask, pPayload, sizeof(uint32_t));
          pPayload += sizeof(uint32_t);
          ws_unmask_input_data(pPayload, len, client->mask, client->recv_buf);
        }
        else
        {
          memcpy(client->recv_buf, pPayload, len);
        }

        if (pInbuf[0] == (char)WS_ID_STRING)
        {
          parent->string_callback(client->recv_buf, len);
        }
        else if (pInbuf[0] == (char)WS_ID_BINARY)
        {
          parent->binary_callback(client->recv_buf, len);
        }
      }
      netbuf_delete(inbuf);
    }
    client->established = 0;
    parent->connected_clients--;
    netconn_close(client->accepted_sock);
    netconn_delete(client->accepted_sock);
    /** Delete tcp send thread */
    vTaskDelete(NULL);
  }
}


/**
 * [netws_send_string_message description]
 * @param msg [description]
 */
void netws_send_message(uint16_t cmd, uint8_t *msg, uint32_t len, ws_msg_type type)
{
  static ws_msg msg_struct;
  
  msg_struct.cmd = cmd;
  msg_struct.len = len;
  msg_struct.type = type;
  memcpy(msg_struct.msg, msg, len);

  xQueueSend(wsSendQueue, (void*)&msg_struct, 0);
}

/**
 * [ws_send_thread description]
 * @param argument [description]
 */
static void ws_send_thread(void * argument)
{
  struct ws_server *ws = (struct ws_server*)argument;
  ws_msg msg;
  SysPkg_Typedef pkg = {
    .misc = 0,
    .dest_id = 0xAA
  };
  wsSendQueue = xQueueCreate( 5, sizeof(ws_msg) );
  if (wsSendQueue == NULL)
    vTaskDelete(NULL);

  struct ws_client *client;

  for (;;)
  {
    if (xQueueReceive( wsSendQueue, &( msg ), pdMS_TO_TICKS(100)) == pdTRUE)
    {
      memset(ws->send_buf, 0x00, 1024);
      
      ws->send_buf[0] = (msg.type == WS_STRING) ? 0x81 : 0x82;
      uint8_t byte_cnt = ws_create_binary_len(msg.len+sizeof(SysPkg_Typedef), ws->send_buf+1);
      
      pkg.cmd = msg.cmd;
      Utils_CmdCreate(&pkg, msg.msg, msg.len);
      
      uint8_t *pWriteBuf = ws->send_buf+byte_cnt+1;
      memcpy(pWriteBuf, (uint8_t*)&pkg, msg.len);
      pWriteBuf += sizeof(SysPkg_Typedef);
      memcpy(pWriteBuf, msg.msg, msg.len);

      msg.len = msg.len + byte_cnt + 1 + sizeof(SysPkg_Typedef);

      for (int iClient = 0; iClient < ws->connected_clients; ++iClient)
      {
        client = &ws->ws_client[iClient];
        if (client->established == 1)
        {
          netconn_write(client->accepted_sock, ws->send_buf, msg.len, NETCONN_NOCOPY);
        }
      }
    }
  }
}

/**
 * [ws_get_binary_pack_len description]
 * @param  buf      [description]
 * @param  pPayload [description]
 * @return          [description]
 */
static uint8_t ws_get_binary_pack_len(uint8_t *buf, uint32_t *len, uint8_t **pPayload)
{
  uint8_t index_data = 2;
  uint8_t masked_flag = 0;

  if ( (buf[1] & 0x80) == 0x80 )
    masked_flag = 1;

  if ( (buf[1] & 0x7F) == 126)
  {
    index_data = 4;
    *len = buf[2] | buf[3];
  }
  else
  {
    *len = buf[1] & 0x7F;
  }

  *pPayload = &buf[index_data];
  return masked_flag;
}

/**
 * [ws_binary_pack_len description]
 * @param  len     [description]
 * @param  outbuf  [description]
 * @param  bytecnt [description]
 * @return         [description]
 */
static uint8_t ws_create_binary_len(uint32_t len, uint8_t *outbuf)
{
  uint8_t tmp_byte = 0, bytecnt = 0;
  
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
 * [unmaskInputData description]
 * @param masked_buf [description]
 * @param size       [description]
 * @param mask       [description]
 * @param out_buf    [description]
 */
static void ws_unmask_input_data(uint8_t *masked_buf, 
                               uint32_t size, 
                               uint8_t *mask, 
                               uint8_t *out_buf)
{
  for (int i = 0; i < size; ++i)
  {
    out_buf[i] = mask[(i%4)]^masked_buf[i];
  }
}

/**
 * [get_ws_key description]
 * @param  buf [description]
 * @param  key [description]
 * @return     [description]
 */
static uint8_t get_ws_key(char *buf, char *key)
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

void ws_string_callback (uint8_t *data, uint32_t len)
{
  
}


struct device_status1
{
  uint8_t irp[4];
  uint8_t afu[4];
  uint8_t lit_en[4];

  uint8_t emit;
  uint8_t mode;
  uint8_t submode;
  uint8_t mode_vz;
};
struct device_status1 recv_dev_status;

void ws_binary_callback (uint8_t *data, uint32_t len)
{
  memcpy(&recv_dev_status, data, len);
}

/*************************** END OF FILE ***************************/
