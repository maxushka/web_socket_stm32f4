#include "main.h"
#include "net.h"

uint8_t SELF_NET_ID = 0xF0;

struct webworker webworker = {
  .wsfs = {
    .flash_addr = (uint8_t __IO*)(0x080E0000),
    .files_cnt = 0,
    .files = NULL
  },
  .ws = {
    .string_callback = ws_string_callback,
    .binary_callback = ws_binary_callback
  }
};

