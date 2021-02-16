/**
 * @project Name
 * @file    net.c
 * @mcu     STM...
 * @device  ЯДИМ.******.***
 * @program ЯДИМ.******
 * @author  M.Beletsky
 * @desc    Description
 */

#include "net.h"
#include "config.h"

static void http_receive_handle(struct webworker *web, struct netconn *conn);

extern struct http_file_system http_file_system;
extern __IO char *sdram_http_address;
char __IO *tmp_post = (__IO char*)(SDRAM_JSON_ADDRESS);
char __IO *jsonbuf = (__IO char*)(SDRAM_JSON_BUF_ADDRESS);
//char data[1000] = "";
//char page[100] = "";
char* buf = NULL;
int json_input_size = 0;

/**
 * Thread http
 * @author M.Beletsky
 * @date   2020-05-08
 * @param  argument   struct webworker
 */
void http_server_netconn_thread(void * argument)
{ 
  struct netconn *conn, *newconn;
  struct webworker *web = (struct webworker*)argument;
  err_t err;
  /* Create a new TCP connection handle */
  conn = netconn_new(NETCONN_TCP);
  if (conn!= NULL)
  {
    /* Bind to port 80 (HTTP) with default IP address */
    err = netconn_bind(conn, NULL, 80);
    if (err == ERR_OK)
    {
      /* Put the connection into LISTEN state */
      netconn_listen(conn);
      while(1) 
      {
        /* accept any icoming connection */
        err = netconn_accept(conn, &newconn);
        if (err == ERR_OK)
        {
          /* serve connection */
          http_receive_handle(web, newconn);
          /* delete connection */
          netconn_delete(newconn);
        }
      }
    }
  }
}

/**
 * [http_receive_handle description]
 * @author M.Beletsky
 * @date   2020-05-08
 * @param  web        struct webworker
 * @param  conn       struct netconn
 */
static void http_receive_handle(struct webworker *web, struct netconn *conn)
{
  char *header =  "HTTP/1.1 200 OK\nExpires: Mon, 26 Jul 1997 05:00:00 GMT\nCache-Control: max-age=0,no-cache,no-store,post-check=0,pre-check=0\nConnection: close\nPragma: no-cache\nExpires: 0\nAccess-Control-Allow-Origin: *\nContent-Type: application/json; charset=utf-8\n\n";
  char *headerOk =  "HTTP/1.1 200 OK\nConnection: close\r\n\r\n";
  char *headerNotFound = "HTTP/1.1 404 Not Found\r\n\r\n";

  uint8_t file_not_found = 0;
  uint16_t buflen = 0;
  struct netbuf *inbuf = NULL;
  char* pStr = NULL;
  bool isAuth = false;

  //for (int i = 0; i < 1000; i++) data[i] = 0;
  //for (int i = 0; i < 100; i++) page[i] = 0;
  char __IO* data = (char __IO*)(0xC0510000);
  char __IO* page = (char __IO*)(0xC051F000);
  memset((char*)data, 0x00, 1000);
  memset((char*)page, 0x00, 100);

  err_t res = netconn_recv(conn, &inbuf);
  if (res == ERR_OK)
  {
    netbuf_data(inbuf, (void**)&buf, &buflen);
    /** Смотрим поле Cookie */
    char *pAuthHead = strstr(buf, "Cookie:");
    if (pAuthHead)
    {
      /** Отделяем поле <token=> */
      char *pToken = strstr(pAuthHead, "token=");
      char *tmp = pvPortMalloc(32);
      if (tmp != NULL)
      {
        strncpy(tmp, pToken+strlen("token="), 30);
        pToken = strtok(tmp, "\r");
        /** Проверяем token, Если он совпадает - мы авторизованы */
        if (strcmp(pToken, web->token) == 0)
          isAuth = true;
        else
          isAuth = false;
        vPortFree(tmp);
      }
    }

    if ( (buflen >=5) && (strncmp(buf, "GET /", 5) == 0) )
    {
      strcpy((char*)data, buf+strlen("GET "));
      pStr = strtok((char*)data, " ");
      if (strcmp(pStr, "/") == 0)
        strcpy((char*)page, "/index.html");
      else
        strcpy((char*)page, pStr);
      /** Если запрос не начинается с .api, то это запрос страницы */
      if (strstr((char*)page, "api") == NULL)
      {
        file_not_found = 1;
        /** Если мы не авторизованы, перенаправляем клинта на страницу авторизации */
        if (isAuth == false)
        {
          if (strstr((char*)page, "html") != NULL)
          {
            sprintf((char*)page, "/auth.html");
          }
        }
        /** По наименованию ищем файл в http_file_system.http_file_store */
        for (int iFile = 0; iFile < http_file_system.http_file_cnt; iFile++)
        {
          if (strcmp((char*)page+1, http_file_system.http_file_store[iFile].file_name) == 0)
          {
            file_not_found = 0;
            /** И возвращаем указатель на него с нужным смещением */
            netconn_write(conn, (const unsigned char*)(sdram_http_address+http_file_system.http_file_store[iFile].offset), 
                                                      (size_t)http_file_system.http_file_store[iFile].page_size, NETCONN_NOCOPY);
            osDelay(10);
            break;
          }
        }
        /** Если файл не найден, возвращаем заголовок 404 */
        if (file_not_found == 1)
        {
          netconn_write(conn, (const unsigned char*)(headerNotFound), (size_t)strlen(headerNotFound), NETCONN_COPY);
        }
      }
      /** Если это пользовательский запрос GET*/
      else
      {
        printf("Clear jsonbuf\n");
        memset((char*)jsonbuf, 0x00, 2048);
        /** Вызываем обработчик GET-запросов, который возвращает JSON-строку */
        printf("Call get handler\n");
        char *json = web->getHandler((char*)page);
        printf("Return from get handler\n");
        /** Совмещаем ее вместе с заголовком и возвращаем клиенту */
        strcat((char*)jsonbuf, header);
        strcat((char*)jsonbuf, json);
        //printf("Response json: %s\n", jsonbuf);
        netconn_write(conn, (const unsigned char*)(jsonbuf), (size_t)strlen((char*)jsonbuf), NETCONN_NOCOPY);
      }
    }
    /** Если это POST-запрос */
    else if( (buflen >=5) && (strncmp(buf, "POST /", 6) == 0) )
    {
      for (int i = 0; i < 2048; i++)
        tmp_post[i] = 0;
      memcpy((char*)tmp_post, buf, buflen);

      struct netbuf *in;
      strcpy((char*)data, buf+strlen("POST /"));
      char *url = strtok((char*)data, " ");
      /** Ищем указатель на входящую JSON-строку */
      char *pContLen = strstr((char*)tmp_post, "Content-Length: ");
      pContLen = pContLen+strlen("Content-Length: ");
      json_input_size = strchr(pContLen, ' ') - pContLen;
      char __IO *ContentLenArr = (char __IO*)(0xC050F000);//pvPortMalloc(4);
      memset((char*)ContentLenArr, 0x00, 4);
      //if (ContentLenArr != NULL)
      {
        memcpy((char*)ContentLenArr, pContLen, json_input_size);
        json_input_size = atoi((char*)ContentLenArr);
      }
      //else
        //json_input_size = 0;
      printf("json_input_size: %d\n", json_input_size);
      char *pContent = strstr((char*)tmp_post, "{\"json\":");
      /** Вычитываем всю полезную нагрузку в массив <tmp_post> */
      //if (strlen(pContent) < json_input_size)
      {
        while (strlen(pContent) < json_input_size)//do
        {
          err_t res = netconn_recv(conn, &in);
          netbuf_data(in, (void**)&buf, &buflen);
          strncat((char*)tmp_post, buf, buflen);
          netbuf_delete(in);
          pContent = strstr((char*)tmp_post, "{\"json\":");
        } 
      }
      /** Вызываем обработчик POST-запроса и передаем ему URL и JSON-строку */
      char *response = web->postHandler(url, pContent+strlen("{\"json\":"), web);
      /** Если postHandler возвращает указатель на данные */
      if (response != NULL)
      {
        /** То совмещаем их с заголовком и возвращаем клиенту */
        memset((char*)tmp_post, 0x00, 2048);
        strcat((char*)tmp_post, headerOk);
        strcat((char*)tmp_post, response);
        size_t size_send = strlen((char*)tmp_post);
        netconn_write(conn, (const unsigned char*)(tmp_post), size_send, NETCONN_NOCOPY);
        printf("return post netcon\n");
        osDelay(10);
      }
      /** Иначе отправляем просто заголовок OK */
      else
      {
        netconn_write(conn, (const unsigned char*)(headerOk), (size_t)strlen(headerOk), NETCONN_COPY);
      }
    }
  }
  /** Закрываем соединение */
  netconn_close(conn);
  /** Очищаем входной буфер */
  netbuf_delete(inbuf);
}

/*************************** END OF FILE ***************************/
