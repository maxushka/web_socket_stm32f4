#include "http.h"

/** This file variable -------------------------------------------------------------------------- */
char *head_js_resp = "HTTP/1.1 200 OK\n\
Connection: close\n\
Access-Control-Allow-Origin: *\n\
Content-Type: application/json; charset=utf-8\n\n";

char *head_ok_resp = "HTTP/1.1 200 OK\nConnection: close\r\n\r\n";
char *head_not_found = "HTTP/1.1 404 Not Found\r\n\r\n";

__IO net_connection *net_conn_pool[HTTP_MAX_CONNECTIONS];
int json_input_size = 0;
uint32_t ErrContent = 0;

/** Static functions prototypes ----------------------------------------------------------------- */
static uint8_t        create_receive_threads ( void );
static uint8_t        cmp_cookie_token       ( char *pBuffer, char *token );
static void           http_receive_handler   ( void * argument );
static unsigned char *find_request_content   ( char *request, 
                                               struct website_file_system *wsfs, 
                                               size_t *ret_size );
static void           send_file_response     ( char *file_name, 
                                               struct netconn *con, 
                                               struct website_file_system *wsfs );

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
 * Threading function to handle an incoming HTTP request.
 * Performs processing of requests for HTML pages, 
 * as well as GET and POST requests.
 * 
 * @param argument - net_connection structure (refer to <http.h>)
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
      /** Check user authorization */
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

        /** Search content length of request data */
        char *pContLen = strstr((char*)net->request_data, "Content-Length: ");
        pContLen = pContLen+strlen("Content-Length: ");
        json_input_size = strtol((char*)pContLen, NULL, 10);

        /** 
         * Search start of JSON string
         * In this server we have the rule - 
         * all body's post requests have construction {"json":...
         */
        char *pContent = strstr((char*)net->request_data, "{\"json\":");
        if (pContent)
        {
          /** Read the entire payload in <net->request_data> */
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
          /** Call the POST request callback and receive response content */
          __IO char* response = net->web->postHandler( url, pContent+strlen("{\"json\":") );
          
          memset((char*)net->resp_js_buff, 0x00, 2048);
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
          netconn_write( net->netconn, 
                         (const unsigned char*)(net->resp_js_buff), 
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
    netconn_close(net->netconn);
    netbuf_delete(inbuf);
    netconn_delete(net->netconn);
    /** Now the task not work */
    net->isopen = 0;
  }
}

/*************************************** END OF FILE **********************************************/
