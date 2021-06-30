#include "jsonlib.h"

void json_get( char *json, char *key, void *value, JsonKeyType_t type )
{
  size_t str_len = 0;
  char *pEnd = NULL;

  char *pKey = strstr(json, key);
  if (pKey == NULL)
    return;
  char *pValue = strchr(pKey, ':')+1;
  if (pValue == NULL)
    return;

  switch(type)
  {
    case jsSTRING:
      pValue++;
      str_len = strchr(pValue, '\"') - pValue;
      memcpy(value, pValue, str_len);
      break;

    case jsINT:
      *(int*)value = strtol(pValue, &pEnd, 10);
      break;

    case jsFLOAT:
      *(float*)value = strtof(pValue, &pEnd);
      break;

    case jsARR:
      pValue++;
      str_len = strchr(pValue, ']') - pValue;
      memcpy(value, pValue, str_len);
      break;
  }
}

void json_create( char *json, char *key, void *value, JsonKeyType_t type )
{
  char *end = NULL;
  char punct = '\0';

  /** If string is empty */
  if (strstr(json, "{") == NULL)
  {
    punct = '{';
    end = json;
  }
  else
  {
    /** If string haven't the key */
    if (strstr(json, key) == NULL)
    {
      end = strrchr(json, '}');
      punct = ',';
    }
    /**
     * Changing the existing key 
     * has not yet been developed
     */
    else
    {
      return;
    }
  }

  switch(type)
  {
    case jsSTRING:
      sprintf(end, "%c\"%s\":\"%s\"}", punct, key, (char*)value);
      break;
    case jsINT:
      sprintf(end, "%c\"%s\":%d}", punct, key, (int)value);
      break;
    case jsFLOAT:
      sprintf(end, "%c\"%s\":%0.1f}", punct, key, *(float*)value);
      break;
    case jsARR:
      sprintf(end, "%c\"%s\":%s}", punct, key, (char*)value );
      break;
  }
}

