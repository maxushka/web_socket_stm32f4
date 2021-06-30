#include "http.h"


char *head_js_resp = "HTTP/1.1 200 OK\n\
Connection: close\n\
Access-Control-Allow-Origin: *\n\
Content-Type: application/json; charset=utf-8\n\n";

char *head_ok_resp = "HTTP/1.1 200 OK\nConnection: close\r\n\r\n";
char *head_not_found = "HTTP/1.1 404 Not Found\r\n\r\n";

int json_input_size = 0;
uint32_t ErrContent = 0;

static uint8_t        create_receive_threads ( void );
static uint8_t        cmp_cookie_token       ( char *pBuffer, char *token );
static void           http_receive_handler   ( void * argument );
static unsigned char *find_request_content   ( char *request, 
                                               struct website_file_system *wsfs, 
                                               size_t *ret_size );
static void           send_file_response     ( char *file_name, 
                                               struct netconn *con, 
                                               struct website_file_system *wsfs );


uint8_t http_create_filesystem( httpFileSystem_t *fs )
{
  uint8_t err = 1;

  memcpy(&fs->files_cnt, (uint8_t*)fs->flash_addr, sizeof(uint32_t));
  if ( (fs->files_cnt > 0) && (fs->files_cnt < 0xFFFFFFFF) )
  {
    size_t size_files = sizeof(httpFile_t)*fs->files_cnt;
#if NET_USE_SDRAM == 0
    fs->files = (httpFile_t *)(fs->flash_addr+sizeof(uint32_t));
    err = 0;
#else
    fs->files = (httpFile_t *)(HTTP_SDRAM_WEB_SITE_ADDRESS);
    if (fs->files != NULL)
    {
      memcpy((void*)fs->files, fs->flash_addr+sizeof(uint32_t), size_files);
      err = 0;
    }
#endif
  }
  return err;
}

/**
 * Function of starting an http server and receiving incoming connections.
 * When the stream of receiving messages is free, 
 * the structure of the received connection is passed to it.
 * 
 * @param arg httpServer_t structure (refer http.h)
 */
void http_server_task( void * arg )
{ 
  httpServer_t *server = (struct webworker*)arg;
  httpConnection_t *connection;

  struct netconn *conn = netconn_new(NETCONN_TCP);
  if (conn == NULL)
    vTaskDelete(NULL);
  if (netconn_bind(conn, NULL, 80) != ERR_OK)
    vTaskDelete(NULL);
  netconn_listen(conn);

  create_tasks_for_connections( server );

  for(;;) 
  {
    for (int iTask = 0; iTask < HTTP_MAX_CONNECTIONS; ++iTask)
    {
#if NET_USE_SDRAM == 1
      connection = &(*net_conn_pool[iTask]);
#else
      connection = &net_conn_pool[iTask];
#endif
      if (connection->isopen == 0)
      {
        if (netconn_accept(conn, &connection->accepted_sock) == ERR_OK)
        {
          connection->isopen = 1;
          vTaskResume(connection->task_handle);
        }
      }
    }
    osDelay(10);
  }
}

static void create_tasks_for_connections( httpServer_t *server )
{
  const uint32_t offset = sizeof(httpConnection_t);
  httpConnection_t *connection;

  for (int iCon = 0; iCon < HTTP_MAX_CONNECTIONS; ++iCon)
  {
#if NET_USE_SDRAM == 1
    server->connections_pool[iCon] = (httpConnection_t*)(HTTP_CONNECTION_START_ADDR+(offset*iCon));
    memset((void*)(*(&net_conn_pool[iCon])), 0x00, sizeof(net_connection));
    connection = &(*server->connections_pool[iCon]);
#else
    connection = &server->connections_pool[iCon];
#endif
    connection->server_ptr = server;
    xTaskCreate( http_request_handler, "http_recv", 256, (void*)connection, 
                 osPriorityNormal, &connection->task_handle );
  }
}

static void http_request_handler( void * arg )
{
  httpConnection_t *connection = (httpConnection_t*)arg;
  httpServer_t *server_ptr = connection->server_ptr;
  struct netbuf *inbuf = NULL;
  char *inbuf_ptr = NULL;
  uint16_t buflen = 0;
  uint8_t isAuth = 0;
  char tmp_url[HTTP_REQ_URL_BUFF_SIZE] = "";

  for(;;)
  {
    // The created task is in standby mode 
    // until an incoming connection unblocks it
    vTaskSuspend(NULL);

    if (netconn_recv(connection->accepted_sock, &inbuf) == ERR_OK)
    {
      inbuf_ptr = NULL;
      netbuf_data(inbuf, (void**)&inbuf_ptr, &buflen);
      memcpy((char*)connection->request_data, (char*)inbuf_ptr, buflen);
      inbuf_ptr = connection->request_data;

#if NET_USE_AUTH == 1
      /** Check user authorization */
      isAuth = compare_token(inbuf_ptr, server_ptr->token);
#else
      isAuth = 1;
#endif

      /** If this is a GET request */
      if (strncmp((char*)inbuf_ptr, "GET /", 5) == 0)
      {
        char *response = connection->response_data;
        char *url = get_request_url(inbuf_ptr, "GET");
        if (is_page_request(url))
        {
          // fixit !!!
          response = get_page_content(url, site, isAuth);
        }
        else
        {
          memset((char*)connection->resp_js_buff, 0, HTTP_RESP_DATA_BUFF_SIZE);
          GETHandler(url, buffer);
        }
        netconn_write(response);
//-------------------------------------------------------------------------------------------------

        inbuf_ptr += strlen("GET ");
        /** Get the page name */
        memcpy(tmp_url, (void*)inbuf_ptr, HTTP_REQ_URL_BUFF_SIZE);
        char *pPage = strtok(tmp_url, " ");

        if (strcmp(pPage, "/") == 0)
          strncpy((char*)connection->request_url, "index.html", HTTP_REQ_URL_BUFF_SIZE);
        else
          strncpy((char*)connection->request_url, pPage+1, HTTP_REQ_URL_BUFF_SIZE);

        /* If the request does not start with .api, 
         * then this is a page request */
        if (strstr((char*)connection->request_url, "api") == NULL)
        {
          if (isAuth == 0)
            if (strstr((char*)connection->request_url, "html") != NULL)
              sprintf((char*)connection->request_url, "auth.html");
          /** Search for file name in structure website_file_system and send to socket */
          send_file_response( (char*)connection->request_url, connection->accepted_sock, &connection->web->wsfs );
        }
        /** If this is a custom GET request */
        else
        {
          /** Call the GET request callback and receive response content */
          __IO char* response = connection->web->getHandler( (char*)connection->request_url );
          
          /** Make full response string */
          memset((char*)connection->resp_js_buff, 0, HTTP_RESP_DATA_BUFF_SIZE);
          strcat((char*)connection->resp_js_buff, head_js_resp);
          strcat((char*)connection->resp_js_buff, (char*)response);
          netconn_write(connection->accepted_sock, (char*)connection->resp_js_buff, 
                        strlen((char*)connection->resp_js_buff), NETCONN_NOCOPY);
          osDelay(10);
        }
      }
      /** If this is a POST request */
      else if( (strncmp((char*)inbuf_ptr, "POST /", 6) == 0) )
      {
        struct netbuf *in;
        char *url = (char*)connection->request_data + strlen("POST ");

        sdram_memcpy(tmp_url, url, HTTP_REQ_URL_BUFF_SIZE);
        url = strtok(tmp_url, " ");
        strncpy((char*)connection->request_url, url+1, HTTP_REQ_URL_BUFF_SIZE);
        url = (char*)connection->request_url;

        /** Search content length of request data */
        char *pContLen = strstr((char*)connection->request_data, "Content-Length: ");
        pContLen = pContLen+strlen("Content-Length: ");
        json_input_size = strtol((char*)pContLen, NULL, 10);

        /** 
         * Search start of JSON string
         * In this server we have the rule - 
         * all body's post requests have construction {"json":...
         */
        char *pContent = strstr((char*)connection->request_data, "{\"json\":");
        if (pContent)
        {
          /** Read the entire payload in <connection->request_data> */
          while (strlen(pContent) < json_input_size)
          {
            if (netconn_recv(connection->accepted_sock, &in) == ERR_OK)
            {
              if ( netbuf_data( in, (void**)&inbuf_ptr, &buflen ) == ERR_OK )
              {
                if (inbuf_ptr != NULL)
                  strncat((char*)connection->request_data, (char*)inbuf_ptr, buflen);
                pContent = strstr((char*)connection->request_data, "{\"json\":");
              }
            }
            netbuf_delete(in);
          }
          /** Call the POST request callback and receive response content */
          __IO char* response = connection->web->postHandler( url, pContent+strlen("{\"json\":") );
          
          memset((char*)connection->resp_js_buff, 0x00, 2048);
          if (response != NULL)
          {
            strcat((char*)connection->resp_js_buff, head_js_resp);
            strcat((char*)connection->resp_js_buff, (char*)response);
          }
          else
          {
            strcat((char*)connection->resp_js_buff, head_ok_resp);
          }
          size_t size_send = strlen((char*)connection->resp_js_buff);
          netconn_write( connection->accepted_sock, 
                         (const unsigned char*)(connection->resp_js_buff), 
                         size_send, 
                         NETCONN_NOCOPY);
          osDelay(10);
        }
        else
        {
          ErrContent++;
        }
      }
    }
    /** Close connection, clear netbuf and connection */
    netconn_close(connection->accepted_sock);
    netbuf_delete(inbuf);
    netconn_delete(connection->accepted_sock);
    /** Now the task not work */
    connection->isopen = 0;
  }
}


static char* get_request_url( char *inbuf, char *type_request )
{
  char *p = strstr(inbuf, type_request);
  if (p)
  {
    return (p + strlen(type_request) + 1);
  }
  return NULL;  
}

static uint8_t is_page_request( char *url )
{
  if (strstr(url, "api") == NULL)
    return 1;
  return 0;
}

static char *get_page_content( char *page_name, httpFileSystem_t *fs, uint32_t *page_size )
{
  uint32_t file_size = 0;
  char *content = find_request_content( page_name, fs, page_size );
  if ( content )
  {
    return content;
  }
  else
  {
    *page_size = strlen(head_not_found);
    return head_not_found;
  }
}

static char *find_page( char *page_name, httpFileSystem_t *fs, uint32_t *page_size )
{
  httpFile_t *file_ptr;
  for (int iFile = 0; iFile < fs->files_cnt; iFile++)
  {
    file_ptr = &fs->files[iFile];
    if ( strstr( page_name, file_ptr->file_name ) != NULL )
    {
      *page_size = file_ptr->page_size;
      return (char*)(fs->flash_addr + file_ptr->offset);
    }
  }
  return NULL;
}

/**
 * Function for comparing the "token" parameter in the Cookie field.
 * When creating an http stream, a random number is generated. 
 * The unauthorized client does not know it and 
 * therefore will be redirected to the authorization page. 
 * After authorization, the "token" parameter will appear in the Cookie field
 * and function return 1
 */
static uint8_t compare_token( char *request, char *token )
{
  static char tmp[32] = {0};
  memset(tmp, 0x00, 32);
  uint8_t auth = 0;
  char *pAuthHead = strstr(request, "Cookie:");
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
 * Search function of the requested page
 * 
 * @param  request Url or a page name
 * @param  wsfs    Struct of website file system
 * @return         Pointer to the requested file or
 *                 NULL if file not found
 */


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



/*************************************** END OF FILE **********************************************/
