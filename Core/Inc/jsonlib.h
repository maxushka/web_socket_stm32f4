#ifndef _JSONLIB_H
#define _JSONLIB_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef enum 
{
  jsSTRING = 0,
  jsINT,
  jsFLOAT,
  jsARR
} JsonKeyType_t;

void json_get    ( char *jstr, char *key, void *value, JsonKeyType_t type );
void json_create ( char *json, char *key, void *value, JsonKeyType_t type);


#endif
