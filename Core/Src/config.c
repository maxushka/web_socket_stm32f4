#include "main.h"
#include "http.h"
#include "http_handlers.h"

// uint8_t SELF_NET_ID = 0xF0;

struct webworker webworker = {
  .wsfs = {
    .flash_addr = (uint8_t __IO*)(0x080E0000),
    .files_cnt = 0,
    .files = NULL
  },
  .getHandler = GET_Handler,
  .postHandler = POST_Handler
};

