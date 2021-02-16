#include "jsonlib.h"
#include <stdio.h>
#include <stdlib.h>

//char tmp_js[500] = "";
char __IO *tmp_js = (char __IO*)(0xC0500000+0x20000);

void json_get(char *jstr, char *key, void *value)
{
  int intVal = 0;
  char *pEnd;
  memset((char*)tmp_js, 0x00, 600);
	char *pKey = strstr(jstr, key);
	if (pKey == NULL)
		return;
	char *pValue = strchr(pKey, ':')+1;
	if (pValue == NULL)
		return;
	strcpy((char*)tmp_js, pValue);
  
	pValue = strtok((char*)tmp_js, ",}");
	char *pStr = strchr(pValue, '\"');
	if (pStr != NULL)
	{
		pValue = strtok(pValue, "\"");
		strcpy(value, pValue);
	}
	else
	{
		if (strchr(pValue, '[') != NULL)
		{
			char *pStr = strchr(pValue, '[');
			pValue = strtok(pValue, "]");
			strcpy(value, pValue+1);
		}
		else
		{
			//intVal = atoi(pValue);
      intVal = strtol(pValue, &pEnd, 10);
      printf("pValue --> %s\n", pValue);
			memcpy(value, &intVal, sizeof(int));
		}
	}
}

void json_create(char *json, char *key, void *value, const char* type)
{
	/** Если строка пустая */
	if (strstr(json, "{") == NULL)
	{
		if (strcmp(type, "string") == 0)
			sprintf(json, "{\"%s\":\"%s\" }", key, value);
		else if (strcmp(type, "int") == 0)
			sprintf(json, "{\"%s\":%d }", key, value);
		else if (strcmp(type, "obj") == 0)
			sprintf(json, "{\"%s\":%s }", key, value);
	}
	else
	{
		/** Если ключа еще нет */
		if (strstr(json, key) == NULL)
		{
			char *end = strrchr(json, '}');
			if (strcmp(type, "string") == 0)
				sprintf(end-1, ",\"%s\":\"%s\" }", key, value);
			else if (strcmp(type, "int") == 0)
				sprintf(end-1, ",\"%s\":%d }", key, value);
			else if (strcmp(type, "obj") == 0)
				sprintf(end-1, ",\"%s\":%s }", key, value);
		}
		else
		{

		}
	}
}
