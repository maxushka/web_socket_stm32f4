#include "main.h"
#include "net.h"


struct website_file_system website = {
  .flash_addr = (uint8_t __IO*)(0x080E0000),
  .file_cnt = 0,
  .files = NULL
};
