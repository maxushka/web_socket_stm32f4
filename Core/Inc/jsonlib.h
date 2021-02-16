#ifndef _JSONLIB_H
#define _JSONLIB_H

#include "main.h"
#include <string.h>
#include <stdbool.h>

void json_get(char *jstr, char *key, void *value);
void json_create(char *json, char *key, void *value, const char* type);


#endif
