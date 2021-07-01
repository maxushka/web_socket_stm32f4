#include "http.h"


const char *HEADER_OK_RESPONSE = "HTTP/1.1 200 OK\nConnection: close\r\n\r\n";
const char *HEADER_FORBIDDEN_RESPONSE = "HTTP/1.1 403 Forbidden\r\n\r\n";
const char *HEADER_NOT_FOUND_RESPOSE = "HTTP/1.1 404 Not Found\r\n\r\n";
const char *HEADER_JSON_RESPONSE = "HTTP/1.1 200 OK\n\
Connection: close\n\
Access-Control-Allow-Origin: *\n\
Content-Type: application/json; charset=utf-8\nn";

static void     create_tasks_for_connections( httpServer_t *server );
static void     http_request_handler( void * arg );
static uint8_t  check_authorization( char *request, char *token );
static char*    get_request_url( char *inbuf );
static uint8_t  is_page_request( char *url );
static char*    get_page_content( char *page_name, httpFileSystem_t *fs, uint32_t *page_size );
static char*    find_page( char *page_name, httpFileSystem_t *fs, uint32_t *page_size );
static int      get_content_length( char *inbuf );
static void     get_full_request_body( httpConnection_t *con, int content_len );
static char*    get_content_pointer( char *inbuf );


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
  uint8_t is_authorized = 0;
  char tmp_url[HTTP_REQ_URL_BUFF_SIZE] = "";

  for(;;)
  {
    // The created task is in standby mode 
    // until an incoming connection unblocks it
    vTaskSuspend(NULL);

    if (netconn_recv(connection->accepted_sock, &inbuf) == ERR_OK)
    {
      netbuf_data(inbuf, (void**)&inbuf_ptr, &buflen);
      memcpy(connection->request_data, inbuf_ptr, buflen);
      inbuf_ptr = connection->request_data;
      memset(connection->response_data, 0, HTTP_RESP_DATA_BUFF_SIZE);

      is_authorized = check_authorization(inbuf_ptr, server_ptr->token);
      char *url = get_request_url(inbuf_ptr);
      char *response = connection->response_data;

      // If this is a GET request
      if (strncmp((char*)inbuf_ptr, "GET /", 5) == 0)
      {
        if (is_page_request(url))
        {
          if (!is_authorized && strstr(url, "html"))
            url = "auth.html";
          response = get_page_content(url, server_ptr->file_system, size);
        }
        else
        {
          char *resp_body_ptr = response;
          if (is_authorized)
          {
            resp_body_ptr += sprintf(response, "%s", HEADER_JSON_RESPONSE);
            server_ptr->getHandler(url, resp_body_ptr);
          }
          else
          {
            response = HEADER_FORBIDDEN_RESPONSE;
          }
        }
      }
      // If this is a POST request
      else if(strncmp(inbuf_ptr, "POST /", 6) == 0)
      {
        int content_length = get_content_length(inbuf_ptr);
        get_full_request_body(connection, content_length);
        char *content_ptr = get_content_pointer(inbuf_ptr);
        response = server_ptr->postHandler(url, content_ptr, response);
        if (response)
          add_header_to_response(response, HEADER_JSON_RESPONSE);
        else
          response = HEADER_OK_RESPONSE;
      }
      else
        response = NULL;

      if (response)
      {
        netconn_write( connection->accepted_sock, response, strlen(response), NETCONN_NOCOPY);
        osDelay(10);
      }
      netbuf_delete(inbuf);
    }
    netconn_close(connection->accepted_sock);
    netconn_delete(connection->accepted_sock);
    connection->isopen = 0;
  }
}

/**
 * Function is comparing the "token" parameter in the Cookie field.
 * When creating an http stream, a random number is generated. 
 * The unauthorized client does not know it and 
 * therefore will be redirected to the authorization page. 
 * After authorization, the "token" parameter will appear in the Cookie field
 * and function return 1
 */
static uint8_t check_authorization( char *request, char *token )
{
#if HTTP_USE_AUTH == 0
  return 1;
#else

  static char tmp[32] = {0};
  uint8_t auth = 0;

  memset(tmp, 0x00, 32);
  char *pAuthHead = strstr(request, "Cookie:");
  if (pAuthHead)
  {
    char *pToken = strstr(pAuthHead, "token=");
    if (pToken)
    {
      strncpy(tmp, pToken+strlen("token="), 30);
      pToken = strtok(tmp, "\r");
      auth = (strcmp(pToken, token) == 0) ? 1:0;
    }
  }
  return auth;
#endif
}

static char* get_request_url( char *inbuf )
{
  char *p = strchr(inbuf, '/');
  if (p)
    p++;
  return p;
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
  char *page_name_ptr = page_name+1;
  if (strncmp(page_name, "/ ", 2) == 0)
    page_name_ptr = "index.html";

  char *content = find_page( page_name, fs, page_size );
  if ( content )
  {
    return content;
  }
  else
  {
    *page_size = strlen(HEADER_NOT_FOUND_RESPOSE);
    return HEADER_NOT_FOUND_RESPOSE;
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

static int get_content_length( char *inbuf )
{
  char *p = strstr(inbuf, "Content-Length: ");
  if (!p)
    return 0;

  p += strlen("Content-Length: ");
  return (int)strtol(p, NULL, 10);
}

static void get_full_request_body( httpConnection_t *con, int content_len )
{
  struct netbuf *inbuf;
  char *inbuf_ptr;
  int buflen;

  while (strlen(con->request_data) < content_len)
  {
    if (netconn_recv(con->accepted_sock, &inbuf) == ERR_OK)
    {
      netbuf_data(inbuf, &inbuf_ptr, &buflen);
      if (inbuf_ptr)
        strncat(con->request_data, inbuf_ptr, buflen);
      netbuf_delete(inbuf);
    }
  }
}

/** 
 * In this server we have the rule - 
 * all body's post requests have construction {"json":...
 */
static char* get_content_pointer( char *inbuf )
{
  char *p = strstr(inbuf, "{\"json\":");
  if (p)
    p += strlen("{\"json\":");
  return p;
}

static void add_header_to_response( char *inbuf, char *header )
{
  char *end_header_ptr = inbuf+strlen(header);
  memmove(end_header_ptr, inbuf, strlen(inbuf));
  memmove(inbuf, header, strlen(header));
}
