#include "net.h"
#include "mbedtls.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"

char *head_js_resp = "HTTP/1.1 200 OK\n\
                      Connection: close\n\
                      Access-Control-Allow-Origin: *\n\
                      Content-Type: application/json; charset=utf-8\n\n";
char *head_ok_resp = "HTTP/1.1 200 OK\n\
                      Connection: close\r\n\r\n";
char *head_not_found = "HTTP/1.1 404 Not Found\r\n\r\n";

char *head_ws = "HTTP/1.1 101 Switching Protocols\n\
                 Upgrade: websocket\n\
                 Connection: Upgrade\n\
                 Sec-WebSocket-Accept: ";

const char *ws_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

net_thread net_threads[NET_MAX_CONNECTIONS] = {0};
QueueHandle_t wsSendQueue;

static uint8_t cmp_cookie_token(char *pBuffer, char *token);
static void http_receive_handler(void * argument);
static void ws_server_thread(void * argument);
static void get_ws_key(char *buf, char *key);
static uint8_t ws_binary_pack_len(uint32_t len, uint8_t *outbuf);


/**
 * [net_create_filesystem description]
 * @param  wsfs [description]
 * @return      [description]
 */
uint8_t net_create_filesystem(struct website_file_system *wsfs)
{
  uint8_t err = 1;
  /** Get a cont files of website and make site catalogue */
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
 * [http_server_netconn_thread description]
 * @param argument [description]
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
  /** Create WebSocket thread*/
  sys_thread_new("websocket", ws_server_thread, 
                  (void*)&web->ws, 1024, osPriorityNormal);
  
  /* Put the connection into LISTEN state */
  netconn_listen(conn);
  for(;;) 
  {
    /** Check available task and newconn */
    for (int iTask = 0; iTask < NET_MAX_CONNECTIONS; iTask++)
    {
      net_thread *nth = &net_threads[iTask];
      /** If task not work */
      if (nth->iswork == 0)
      {
        /** If task handle was created and now is deleted -> clear handle ptr */
        if ( (nth->xHandle != NULL ) &&
             (eTaskGetState(nth->xHandle) == eDeleted) )
          nth->xHandle = NULL;
        /* Accept icoming connection and create task for it */
        if (netconn_accept(conn, &nth->newconn) == ERR_OK)
        {
          struct new_connection new_netconn = {
            .web = web,
            .netconn = nth->newconn,
            .iswork = 1
          };
          /** The created task will delete the connection upon completion */
          xTaskCreate(http_receive_handler, "NewAccept", 1024, 
            (void*)&new_netconn, osPriorityNormal, &nth->xHandle);
        }
      }
    }
  }
}

/**
 * [ws_server_netconn_thread description]
 * @param argument [description]
 */
static void ws_server_thread(void * argument)
{
  struct websocket *ws = (struct websocket*)argument;
  struct netconn *ws_con, *accept_sock;
  struct netbuf *inbuf = NULL;
  char* pInbuf = NULL;
  uint16_t size_inbuf = 0;
  TaskHandle_t SendTaskHandle;

  /* Create a new TCP connection handle */
  ws_con = netconn_new(NETCONN_TCP);
  if (ws_con == NULL)
    vTaskDelete(NULL);

  /* Bind to port (WS) with default IP address */
  if (netconn_bind(ws_con, NULL, 8765) != ERR_OK)
    vTaskDelete(NULL);

  netconn_listen(ws_con);

  for(;;)
  {
    if (netconn_accept(ws_con, &accept_sock) == ERR_OK)
    {
      /** 
       * Create a task for sending messages to client
       * Accepted socket is the argument for a task function
       */
      xTaskCreate(ws_send_thread, "send_thread", 255, 
        (void*)accept_sock, osPriorityNormal, &SendTaskHandle);

      /** Receive incomming messages */
      while (netconn_recv(accept_sock, &inbuf) == ERR_OK)
      {
        netbuf_data(inbuf, (void**)&pInbuf, &size_inbuf);
        if (strncmp(pInbuf, "GET /", 5) == 0)
        {
          /** Get client Sec-WebSocket-Key */
          get_ws_key(pInbuf, ws->key);
          /** Concat with const GUID and make answer*/
          sprintf(ws->concat_key, "%s%s", ws->key, ws_guid);
          mbedtls_sha1((unsigned char *)ws->concat_key, 60, 
                       (unsigned char*)ws->hash);
          int len = 0;
          mbedtls_base64_encode((unsigned char*)ws->hash_base64, 100, 
                                (size_t*)&len, (unsigned char*)ws->hash, 20);
          /** Send response handshake */
          sprintf(ws->send_buf, "%s%s", head_ws, ws->hash_base64);
          netconn_write(accept_sock, ws->send_buf, 
                        strlen(ws->send_buf), NETCONN_NOCOPY);
        }

        netbuf_delete(inbuf);
      }
      netconn_close(accept_sock);
      netconn_delete(conn);
      /** Delete tcp send thread */
      vTaskDelete(SendTaskHandle);
    }
  }
}

/**
 * [ws_send_thread description]
 * @param argument [description]
 */
static void ws_send_thread(void * argument)
{
  struct netconn *ws_con = (struct netconn*)argument;

  wsSendQueue = xQueueCreate( 5, sizeof( uint8_t /* temp size */ ));
  if (wsSendQueue == NULL)
    vTaskDelete(NULL);

  for (;;)
  {
    xQueueReceive( wsSendQueue, &( recv_struct /* temp var (not compile) */ ), portMAX_DELAY);
    // netconn_write(accept_sock, ws->send_buf, strlen(ws->send_buf), NETCONN_NOCOPY);
  }
}

/**
 * [netws_send_string_message description]
 * @param msg [description]
 */
void netws_send_string_message(char *msg, uint32_t len)
{
  /**
   * len
   * max len check
   * msg
   */
  // xQueueSend(wsSendQueue, ())
}

static uint8_t get_binary_pack_len(uint8_t *buf)
{

}

/**
 * [ws_binary_pack_len description]
 * @param  len     [description]
 * @param  outbuf  [description]
 * @param  bytecnt [description]
 * @return         [description]
 */
static uint8_t ws_binary_pack_len(uint32_t len, uint8_t *outbuf)
{
  uint8_t tmp_byte = 0, bytecnt = 0;
  for (uint8_t iByte = 4; iByte >= 0; --iByte)
  {
    tmp_byte = ( len >> (7 * iByte)) & 0x7F;
    if (tmp_byte != 0)
    {
      tmp_byte = (iByte != 0) ? (tmp_byte | 0x80) : tmp_byte;
      outbuf[bytecnt] = tmp_byte;
      bytecnt++;
    }
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
    strncpy(out, p, size);
    err = 0;
  }
  return err;
}
/**
 * [cmp_cookie_token description]
 * @param  pBuffer [description]
 * @param  token   [description]
 * @return         [description]
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
 * [http_receive_handle description]
 * @param web  [description]
 * @param conn [description]
 */
static void http_receive_handler(void * argument)
{
  struct new_connection *newconn = (struct new_connection *)argument;
  struct webworker *web = newconn->web;
  struct netconn *conn = newconn->netconn;
  struct website_file_system *wsfs = &web->wsfs;
  struct netbuf *inbuf = NULL;
  uint16_t buflen = 0;
  char* pBuffer = NULL;
  uint8_t isAuth = 0;

  for(;;)
  {
    if (netconn_recv(conn, &inbuf) == ERR_OK)
    {
      netbuf_data(inbuf, (void**)&pBuffer, &buflen);
      isAuth = cmp_cookie_token(pBuffer, web->token);
      
      isAuth = 1; //Заглушка!!!

      if (strncmp(pBuffer, "GET /", 5) == 0)
      {
        /** Copy request body */
        /** TODO: check request_data length buffer and buflen */
        strncpy(newconn->request_data, pBuffer+strlen("GET "), buflen);
        /** Get the page name */
        char *pPage = strtok((char*)newconn->request_data, " ");
        if (pPage)
        {
          if (strcmp(pPage, "/") == 0)
            strcpy((char*)newconn->request_url, "/index.html");
          else
            strcpy((char*)newconn->request_url, pPage);
        }

        /* If the request does not start with .api, 
         * then this is a page request */
        if (strstr((char*)newconn->request_url, "api") == NULL)
        {
          int file_not_found = 1;
          /** If the authorization is not successful, 
          the client is redirected to the authorization page */
          if (isAuth == 0)
          {
            sprintf((char*)newconn->request_url, "/auth.html");
          }
          /** Search for file name in structure website_file_system */
          for (int iFile = 0; iFile < wsfs->files_cnt; iFile++)
          {
            if ( strcmp((char*)newconn->request_url+1, 
                        wsfs->files[iFile].file_name) == 0 )
            {
              file_not_found = 0;
              /** Returning the contents of the requested file */
              netconn_write(conn, (unsigned char*)wsfs->flash_addr+wsfs->files[iFile].offset, 
                            wsfs->files[iFile].page_size, NETCONN_COPY);
              osDelay(10);
              break;
            }
          }
          if (file_not_found == 1)
          {
            netconn_write(conn, head_not_found, strlen(head_not_found),
                          NETCONN_COPY);
          }
        }
        /** If this is a custom GET request */
        else
        {
          memset((char*)newconn->resp_js_buff, 0, newconn->resp_js_buff_len);
          char *resp_json = web->getHandler((char*)newconn->request_url);
          strcat((char*)newconn->resp_js_buff, head_js_resp);
          strcat((char*)newconn->resp_js_buff, resp_json);
          netconn_write(conn, newconn->resp_js_buff, 
                        strlen((char*)newconn->resp_js_buff), NETCONN_NOCOPY);
        }
      }

//      /** Если это POST-запрос */
//      else if( (strncmp(pBuffer, "POST /", 6) == 0) )
//      {
//        for (int i = 0; i < 2048; i++)
//          tmp_post[i] = 0;
//        memcpy((char*)tmp_post, pBuffer, buflen);

//        struct netbuf *in;
//        strcpy((char*)data, pBuffer+strlen("POST /"));
//        char *url = strtok((char*)data, " ");
//        /** Ищем указатель на входящую JSON-строку */
//        char *pContLen = strstr((char*)tmp_post, "Content-Length: ");
//        pContLen = pContLen+strlen("Content-Length: ");
//        json_input_size = strchr(pContLen, ' ') - pContLen;
//        char __IO *ContentLenArr = (char __IO*)(0xC050F000);//pvPortMalloc(4);
//        memset((char*)ContentLenArr, 0x00, 4);
//        //if (ContentLenArr != NULL)
//        {
//          memcpy((char*)ContentLenArr, pContLen, json_input_size);
//          json_input_size = atoi((char*)ContentLenArr);
//        }
//        //else
//          //json_input_size = 0;
//        printf("json_input_size: %d\n", json_input_size);
//        char *pContent = strstr((char*)tmp_post, "{\"json\":");
//        /** Вычитываем всю полезную нагрузку в массив <tmp_post> */
//        //if (strlen(pContent) < json_input_size)
//        {
//          while (strlen(pContent) < json_input_size)//do
//          {
//            err_t res = netconn_recv(conn, &in);
//            netbuf_data(in, (void**)&pBuffer, &buflen);
//            strncat((char*)tmp_post, pBuffer, buflen);
//            netbuf_delete(in);
//            pContent = strstr((char*)tmp_post, "{\"json\":");
//          } 
//        }
//        /** Вызываем обработчик POST-запроса и передаем ему URL и JSON-строку */
//        char *response = web->postHandler(url, pContent+strlen("{\"json\":"), web);
//        /** Если postHandler возвращает указатель на данные */
//        if (response != NULL)
//        {
//          /** То совмещаем их с заголовком и возвращаем клиенту */
//          memset((char*)tmp_post, 0x00, 2048);
//          strcat((char*)tmp_post, head_ok_resp);
//          strcat((char*)tmp_post, response);
//          size_t size_send = strlen((char*)tmp_post);
//          netconn_write(conn, (const unsigned char*)(tmp_post), size_send, NETCONN_NOCOPY);
//          printf("return post netcon\n");
//          osDelay(10);
//        }
//        /** Иначе отправляем просто заголовок OK */
//        else
//        {
//          netconn_write(conn, (const unsigned char*)(head_ok_resp), (size_t)strlen(head_ok_resp), NETCONN_COPY);
//        }
//      }
    }
    /** Close connection, clear netbuf and connection */
    netconn_close(conn);
    netbuf_delete(inbuf);
    netconn_delete(conn);
    /** Now the task not work */
    newconn->iswork = 0;
    vTaskDelete(NULL);
  }
}

/*************************** END OF FILE ***************************/
