#ifndef _HTTP_HANDLERS_H
#define _HTTP_HANDLERS_H

#include "main.h"
#include "net.h"
#include <string.h>


// ------------------------------------------------ Temp Addresses
#define TEMP_GET_HANDL_START_ADDR   (NET_TEMP_JSON_LIB_END_ADDR)
#define TEMP_GET_HANDL_END_ADDR     (NET_TEMP_GET_HANDL_START_ADDR+NET_TEMP_GET_HANDL_SIZE)

#define TEMP_POST_HANDL_START_ADDR  (NET_TEMP_GET_HANDL_END_ADDR)
#define TEMP_POST_HANDL_END_ADDR    (NET_TEMP_POST_HANDL_START_ADDR+NET_TEMP_POST_HANDL_SIZE)

// ------------------------------------------------ Temp Size
#define NET_TEMP_GET_HANDL_SIZE           4096
#define NET_TEMP_POST_HANDL_SIZE          4096
// ------------------------------------------------

__IO char *GET_Handler  ( char *url );
__IO char *POST_Handler ( char *url, char *json );

#endif
