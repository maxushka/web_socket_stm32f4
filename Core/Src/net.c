#include "net.h"
#include "mbedtls.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
#include "utils.h"

char *head_js_resp = "HTTP/1.1 200 OK\n\
Connection: close\n\
Access-Control-Allow-Origin: *\n\
Content-Type: application/json; charset=utf-8\n\n";
char *head_ok_resp = "HTTP/1.1 200 OK\nConnection: close\r\n\r\n";
char *head_not_found = "HTTP/1.1 404 Not Found\r\n\r\n";

__IO net_connection *net_conn_pool[HTTP_MAX_CONNECTIONS];

xSemaphoreHandle smpr_HttpRequest;

int json_input_size = 0;
uint32_t ErrContent = 0;

static uint8_t create_receive_threads     ( void );
static uint8_t cmp_cookie_token           ( char *pBuffer, char *token );
static void    http_receive_handler       ( void * argument );

/**
 * The function of creating a file structure of the site
 * 
 * @param  wsfs web site structure (refer to <net.h>)
 * @return      0 - errors none
 *              1 - http file system not created
 */
uint8_t http_create_filesystem( struct website_file_system *wsfs )
{
  uint8_t err = 1;
  /** Get a count files of website and make site catalogue */
  memcpy(&wsfs->files_cnt, (uint8_t*)wsfs->flash_addr, sizeof(uint32_t));
  if ( (wsfs->files_cnt > 0) && (wsfs->files_cnt < 0xFFFFFFFF))
  {
    size_t size_files = sizeof(struct website_file)*wsfs->files_cnt;

#if NET_USE_SDRAM == 0
    wsfs->files = (__IO struct website_file *)(wsfs->flash_addr+sizeof(uint32_t));
#else
    wsfs->files = (__IO struct website_file *)(HTTP_SDRAM_WEB_SITE_ADDRESS);
    if (wsfs->files != NULL)
    {
      memcpy((void*)wsfs->files, 
             (uint8_t*)wsfs->flash_addr+sizeof(uint32_t), 
             size_files);
      err = 0;
    }
#endif
  }
  return err;
}

/**
 * Function of initializing the structure of incoming connections, 
 * as well as streams for receiving and processing http requests.
 * 
 * @return  0 - Errors none
 *          1 - Semaphore create fail
 *          2 - Task crete fail
  */
static uint8_t create_receive_threads( void )
{
  uint32_t offset = sizeof(net_connection);
  __IO net_connection *nct;

  for (int iCon = 0; iCon < HTTP_MAX_CONNECTIONS; ++iCon)
  {
#if NET_USE_SDRAM == 1
    net_conn_pool[iCon] = (__IO net_connection *)(HTTP_CONNECTION_START_ADDR + (offset*iCon) );
    memset((void*)(*(&net_conn_pool[iCon])), 0x00, sizeof(net_connection));
    nct = &(*net_conn_pool[iCon]);
#else
    nct = &net_conn_pool[iCon];
#endif
    nct->semaphore = xSemaphoreCreateCounting( 1, 0 );
    if (nct->semaphore == NULL)
      return 1;

    if ( sys_thread_new( "http_recv", 
                         http_receive_handler, 
                         (void*)nct, 256, osPriorityNormal ) == NULL )
      return 2;
  }
  return 0;
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
static uint8_t cmp_cookie_token( char *pBuffer, char *token )
{
  static char tmp[32] = {0};
  memset(tmp, 0x00, 32);
  uint8_t auth = 0;
  char *pAuthHead = strstr(pBuffer, "Cookie:");
  if (pAuthHead)
  {
    /** Detaching field <token=> */
    char *pToken = strstr(pAuthHead, "token=");
    if (pToken)
    {
      /** Allocating a separate space for the strtok function */
      strncpy(tmp, pToken+strlen("token="), 30);
      /** Get a token value */
      pToken = strtok(tmp, "\r");
      /** Client token validation with token issued by server */
      auth = (strcmp(pToken, token) == 0) ? 1:0;
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
 * @param argument webworker structure (refer http.h)
 */
void http_server_task( void * argument )
{ 
  struct netconn *conn;
  struct webworker *web = (struct webworker*)argument;
  err_t err;
  __IO net_connection *nct;

  /* Create a new TCP connection handle */
  conn = netconn_new(NETCONN_TCP);
  if (conn == NULL)
    vTaskDelete(NULL);

  /* Bind to port 80 (HTTP) with default IP address */
  err = netconn_bind(conn, NULL, 80);
  if (err != ERR_OK)
    vTaskDelete(NULL);

  if (create_receive_threads() != 0)
    vTaskDelete(NULL);

  /* Put the connection into LISTEN state */
  netconn_listen(conn);

  for(;;) 
  {
    /** Check available task and newconn */
    for (int iTask = 0; iTask < HTTP_MAX_CONNECTIONS; ++iTask)
    {
#if NET_USE_SDRAM == 1
      nct = &(*net_conn_pool[iTask]);
#else
      nct = &net_conn_pool[iTask];
#endif
      if (nct->web == NULL)
        nct->web = web;
      /** If connection currently not work */
      if (nct->isopen == 0)
      {
        /* Accept icoming connection and create task for it */
        if (netconn_accept(conn, (struct netconn**)&nct->netconn) == ERR_OK)
        {
          nct->isopen = 1;
          /** Set a semaphore for a thread of processing */
          if (xSemaphoreGive( nct->semaphore ) != pdTRUE)
          {
            nct->isopen = 0;
            netconn_close(nct->netconn);
            netconn_delete(nct->netconn);
          }
        }
      }
    }
    osDelay(10);
  }
}

/**
 * Search function of the requested page
 * 
 * @param  request Url or a page name
 * @param  wsfs    Struct of website file system
 * @return         Pointer to the requested file or
 *                 NULL if file not found
 */
static unsigned char *find_request_content( char *request, 
                                            struct website_file_system *wsfs, 
                                            size_t *ret_size )
{
  __IO struct website_file *pFile;
  for (int iFile = 0; iFile < wsfs->files_cnt; iFile++)
  {
    pFile = &wsfs->files[iFile];
    if ( strcmp( request, (char*)pFile->file_name ) == 0 )
    {
      /** Returning the contents of the requested file */
      *ret_size = pFile->page_size;
      return (unsigned char*)(wsfs->flash_addr+pFile->offset);
    }
  }
  /** File not found */
  return NULL;
}

/**
 * Function of sending the page requested by the client.
 * If the requested page is not found, the header "File not found" is sent.
 * 
 * @param file_name String of file name
 * @param con       Current net connection
 * @param wsfs      Struct website file system
 */
static void send_file_response( char *file_name, 
                                struct netconn *con, 
                                struct website_file_system *wsfs )
{
  size_t file_size = 0;
  unsigned char* pContent = find_request_content( file_name, wsfs, &file_size );
  if ( pContent != NULL )
  {
    netconn_write(con, pContent, file_size, NETCONN_NOCOPY);
  }
  else
  {
    netconn_write(con, head_not_found, strlen(head_not_found), NETCONN_NOCOPY);
  }
  osDelay(10);
}

/**
 * [http_receive_handler description]
 * 
 * @param argument [description]
 */
static void http_receive_handler( void * argument )
{
  net_connection *net = (net_connection*)argument;
  struct netbuf *inbuf = NULL;
  __IO char* pBuffer = NULL;
  uint16_t buflen = 0;
  uint8_t isAuth = 0;
  char tmp_url[HTTP_REQ_URL_BUFF_SIZE] = "";

  for(;;)
  {
    xSemaphoreTake(net->semaphore, portMAX_DELAY);

    if (netconn_recv(net->netconn, &inbuf) == ERR_OK)
    {
      pBuffer = NULL;
      netbuf_data(inbuf, (void**)&pBuffer, &buflen);
      memcpy((char*)net->request_data, (char*)pBuffer, buflen);

      pBuffer = net->request_data;
#if NET_USE_AUTH == 1
      isAuth = cmp_cookie_token((char*)pBuffer, net->web->token);
#else
      isAuth = 1;
#endif
      /** If this is a GET request */
      if (strncmp((char*)pBuffer, "GET /", 5) == 0)
      {
        pBuffer += strlen("GET ");
        /** Get the page name */
        memcpy(tmp_url, (void*)pBuffer, HTTP_REQ_URL_BUFF_SIZE);
        char *pPage = strtok(tmp_url, " ");

        if (strcmp(pPage, "/") == 0)
          strncpy((char*)net->request_url, "index.html", HTTP_REQ_URL_BUFF_SIZE);
        else
          strncpy((char*)net->request_url, pPage+1, HTTP_REQ_URL_BUFF_SIZE);

        /* If the request does not start with .api, 
         * then this is a page request */
        if (strstr((char*)net->request_url, "api") == NULL)
        {
          if (isAuth == 0)
            if (strstr((char*)net->request_url, "html") != NULL)
              sprintf((char*)net->request_url, "auth.html");
          /** Search for file name in structure website_file_system and send to socket */
          send_file_response( (char*)net->request_url, net->netconn, &net->web->wsfs );
        }
        /** If this is a custom GET request */
        else
        {
          /** Call the GET request callback and receive response content */
          __IO char* response = net->web->getHandler( (char*)net->request_url );
          
          /** Make full response string */
          memset((char*)net->resp_js_buff, 0, HTTP_RESP_DATA_BUFF_SIZE);
          strcat((char*)net->resp_js_buff, head_js_resp);
          strcat((char*)net->resp_js_buff, (char*)response);
          netconn_write(net->netconn, (char*)net->resp_js_buff, 
                        strlen((char*)net->resp_js_buff), NETCONN_NOCOPY);
          osDelay(10);
        }
      }
      /** If this is a POST request */
      else if( (strncmp((char*)pBuffer, "POST /", 6) == 0) )
      {
        struct netbuf *in;
        char *url = (char*)net->request_data + strlen("POST ");

        sdram_memcpy(tmp_url, url, HTTP_REQ_URL_BUFF_SIZE);
        url = strtok(tmp_url, " ");
        strncpy((char*)net->request_url, url+1, HTTP_REQ_URL_BUFF_SIZE);
        url = (char*)net->request_url;

        /** Ищем указатель на входящую JSON-строку */
        char *pContLen = strstr((char*)net->request_data, "Content-Length: ");
        pContLen = pContLen+strlen("Content-Length: ");
        json_input_size = strtol((char*)pContLen, NULL, 10);
        char *pContent = strstr((char*)net->request_data, "{\"json\":");
        if (pContent)
        {
          /** Вычитываем всю полезную нагрузку в массив <net->request_data> */
          while (strlen(pContent) < json_input_size)
          {
            if (netconn_recv(net->netconn, &in) == ERR_OK)
            {
              if ( netbuf_data( in, (void**)&pBuffer, &buflen ) == ERR_OK )
              {
                if (pBuffer != NULL)
                  strncat((char*)net->request_data, (char*)pBuffer, buflen);
                pContent = strstr((char*)net->request_data, "{\"json\":");
              }
            }
            netbuf_delete(in);
          }
          /** Вызываем обработчик POST-запроса и передаем ему URL и JSON-строку */
          memset((char*)net->resp_js_buff, 0x00, 2048);
          
          __IO char* response = net->web->postHandler( url, pContent+strlen("{\"json\":") );
          if (response != NULL)
          {
            strcat((char*)net->resp_js_buff, head_js_resp);
            strcat((char*)net->resp_js_buff, (char*)response);
          }
          else
          {
            strcat((char*)net->resp_js_buff, head_ok_resp);
          }
          size_t size_send = strlen((char*)net->resp_js_buff);
          netconn_write(net->netconn, (const unsigned char*)(net->resp_js_buff), size_send, NETCONN_NOCOPY);
          osDelay(10);
        }
        else
        {
          ErrContent++;
        }
      }
    }
    /** Close connection, clear netbuf and connection */
    netconn_close(net->netconn);
    netbuf_delete(inbuf);
    netconn_delete(net->netconn);
    //memset((void*)net->request_data, 0x00, HTTP_REQ_DATA_BUFF_SIZE);
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

  ws_init_memory(ws);
  netconn_listen(ws_con);
  
  smpr_WsInMsg = xSemaphoreCreateCounting( 5, 0 );
  
  /** 
   * Create a task for sending messages to client
   * Accepted socket is the argument for a task function
   */
  xTaskCreate(ws_send_thread, "send_thread", 255, 
   (void*)ws, osPriorityNormal, &SendTaskHandle);
  
  struct ws_client *new_client;

  for(;;)
  {
    for (int iClient = 0; iClient < NET_WS_MAX_CLIENTS; ++iClient)
    {
      new_client = &(ws->ws_clients[iClient]);
      if (new_client->established == 0)
      {
        if (netconn_accept(ws_con, (struct netconn**)&new_client->accepted_sock) == ERR_OK)
        {
          new_client->established = 1;
          vTaskResume(new_client->tHandle);
          //ws->connected_clients++;
          //xSemaphoreGive( smpr_WsInMsg );
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
 * [ws_client_thread description]
 * @param argument [description]
 */
static void ws_client_thread(void * argument)
{

  struct ws_client *client = (struct ws_client*)argument;
  struct ws_server *parent = (struct ws_server*)client->parent;
  struct netbuf *inbuf = NULL;
  uint8_t type = 0;
  uint16_t size_inbuf = 0;
  uint32_t len = 0;
  uint8_t msk = 0, pIndex = 0;
  char* pInbuf = NULL;

  for (;;)
  {
    //xSemaphoreTake( smpr_WsInMsg, portMAX_DELAY );
    vTaskSuspend(NULL);
    
    parent->connected_clients++;
    /** Receive incomming messages */
    while (netconn_recv(client->accepted_sock, &inbuf) == ERR_OK)
    {
      memset(client->raw_data, 0x00, NET_WS_CLIENT_RECV_BUFFER_SIZE);
      netbuf_data(inbuf, (void**)&pInbuf, &size_inbuf);
      memcpy(client->raw_data, (void*)pInbuf, size_inbuf);
      pInbuf = (char*)client->raw_data;

      if (strncmp((char*)pInbuf, "GET /", 5) == 0)
      {
        /** Get client Sec-WebSocket-Key */
        get_ws_key((char*)pInbuf, client->key);
        /** Concat with const GUID and make answer*/
        sprintf(client->concat_key, "%s%s", client->key, ws_guid);
        mbedtls_sha1((unsigned char *)client->concat_key, 60, 
                     (unsigned char*)client->hash);
        int baselen = 0;
        mbedtls_base64_encode((unsigned char*)client->hash_base64, 100, 
                              (size_t*)&baselen, (unsigned char*)client->hash, 20);
        /** Send response handshake */
        sprintf((char*)parent->send_buf, "%s%s%s", head_ws, client->hash_base64, "\r\n\r\n");
        netconn_write( client->accepted_sock, 
                       (uint8_t*)parent->send_buf, 
                       strlen((char*)parent->send_buf), 
                       NETCONN_NOCOPY );
      }
      else if ( (pInbuf[0] == (char)WS_ID_STRING) || (pInbuf[0] == (char)WS_ID_BINARY) )
      {
        type = pInbuf[0];
        pIndex = ws_get_binary_pack_len( (char*)pInbuf, &len, &msk );
        pInbuf += pIndex;
        if (pIndex != 0)
        {
          memset(client->recv_buf, 0x00, NET_WS_CLIENT_RECV_BUFFER_SIZE);
          if (msk == 1)
          {
            sdram_memcpy(client->mask, pInbuf, sizeof(uint32_t));
            pInbuf += sizeof(uint32_t);
            ws_unmask_input_data( (uint8_t*)pInbuf, len, client->mask, client->recv_buf);
          }
          else
          {
            memcpy(client->recv_buf, (char*)pInbuf, len);
          }

          if (type == (uint8_t)WS_ID_STRING)
          {
            parent->string_callback(client->recv_buf, len);
          }
          else if (type == (uint8_t)WS_ID_BINARY)
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
 * [ws_init_memory description]
 * @param ws [description]
 */
static void ws_init_memory(struct ws_server *ws)
{
  ws->send_buf = (char *)(NET_WS_SEND_BUFFER_START_ADDR);
  ws->connected_clients = 0;
  memset((void*)ws->send_buf, 0x00, NET_WS_SEND_BUFFER_SIZE);
  //ws->semaphore = xSemaphoreCreateCounting( NET_WS_MAX_CLIENTS, 0 );
  
  struct ws_client *wscl;
  for (int i = 0; i < NET_WS_MAX_CLIENTS; ++i)
  {
    //ws->ws_clients[i] = (struct ws_client *)( NET_WS_CLIENTS_START_ADDR 
    //                                        + sizeof(struct ws_client)*i);
    wscl = (&ws->ws_clients[i]);
    memset((void*)(wscl), 0x00, sizeof(struct ws_client));
    wscl->parent = (void*)ws;
    xTaskCreate(ws_client_thread, "ws_client", 256, (void*)wscl, osPriorityNormal, &wscl->tHandle);
    //sys_thread_new("ws_client", ws_client_thread, (void*)wscl, 256, osPriorityNormal);
  }
}

/**
 * [netws_send_message description]
 * @param cmd  [description]
 * @param misc [description]
 * @param msg  [description]
 * @param len  [description]
 * @param type [description]
 */
void netws_send_message( uint16_t cmd, 
                         uint16_t dest,
                         uint8_t misc, 
                         uint8_t *msg, 
                         uint32_t len, 
                         ws_msg_type type )
{
  static ws_msg msg_struct;
  
  msg_struct.cmd = cmd;
  msg_struct.dest = dest;
  msg_struct.misc = (uint16_t)misc;
  msg_struct.len = len;
  msg_struct.type = type;
  if ( (msg != NULL) && (len != 0) )
    sdram_memcpy(msg_struct.msg, msg, len);

  if (wsSendQueue != NULL)
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
      memset((void*)ws->send_buf, 0x00, 1024);

      ws->send_buf[0] = (msg.type == WS_STRING) ? 0x81 : 0x82;
      /** Записываем длину отправляемого пакета в массив */
      uint8_t byte_cnt = ws_create_binary_len(msg.len+sizeof(SysPkg_Typedef), 
                                              (uint8_t*)ws->send_buf+1);

      pkg.cmd = msg.cmd;
      pkg.misc = msg.misc;
      pkg.dest_id = msg.dest;

      payload = (msg.len == 0) ? NULL:msg.msg;
      Utils_CmdCreate(&pkg, payload, msg.len);

      uint8_t *pWriteBuf = (uint8_t*)(ws->send_buf+byte_cnt+1);
      uint8_t *pPkg = (uint8_t*)&pkg;

      sdram_memcpy(pWriteBuf, pPkg, sizeof(SysPkg_Typedef));
      pWriteBuf += sizeof(SysPkg_Typedef);
      
      if (payload != NULL)
        sdram_memcpy(pWriteBuf, payload, msg.len);

      msg.len = msg.len + byte_cnt + 1 + sizeof(SysPkg_Typedef);

      for (int iClient = 0; iClient < NET_WS_MAX_CLIENTS; ++iClient)
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
      osDelay(1);
    }
  }
}

/**
 * [ws_get_binary_pack_len description]
 * @param  buf      [description]
 * @param  len      [description]
 * @param  pPayload [description]
 * @return          [description]
 */
uint8_t ws_get_binary_pack_len(char *buf, uint32_t *len, uint8_t *masked)
{
  uint8_t index_data = 2;

  if ( (buf[1] & 0x80) == 0x80 )
    *masked = 1;

  if ( (buf[1] & 0x7F) == 126 )
  {
    index_data = 4;
    *len = buf[2] | buf[3];
  }/*
  else if ( (buf[1] & 0x7F) == 127 )
  {
    index_data = 6;
    *len = buf[2] | buf[3] | buf[4] | buf[5];
  }*/
  else
  {
    *len = buf[1] & 0x7F;
  }

  return index_data;
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

/**
 * [unmaskInputData description]
 * @param masked_buf [description]
 * @param size       [description]
 * @param mask       [description]
 * @param out_buf    [description]
 */
void ws_unmask_input_data(uint8_t *masked_buf, 
                          uint32_t size, 
                          uint8_t *mask, 
                          uint8_t *out_buf)
{
  uint8_t mbyte = 0, bmask = 0;
  for (int i = 0; i < size; ++i)
  {
    mbyte = masked_buf[i];
    bmask = mask[i%4];
    out_buf[i] = bmask^mbyte;
  }
}

/*************************** END OF FILE ***************************/
