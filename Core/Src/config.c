#include "main.h"
#include "http.h"
#include "websocket.h"
//#include "http_handlers.h"

uint8_t SELF_NET_ID = 0xF0;

http_server_t http_server = {
  .file_system = {
    .flash_addr = (uint8_t*)(0x080E0000),
    .files_cnt = 0,
    .files = NULL
  }
  // .getHandler = GET_Handler,
  // .postHandler = POST_Handler
};


ws_server_t ws_server = {
  .connected_clients_cnt = 0
};
