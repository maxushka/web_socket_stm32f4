#include "main.h"
#include "net.h"

struct webworker webworker = {
  .wsfs = {
    .flash_addr = (uint8_t __IO*)(0x080E0000),
    .files_cnt = 0,
    .files = NULL
  },
};

