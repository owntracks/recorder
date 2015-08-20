#ifndef _STORAGE_H_INCL_
# define _STORAGE_H_INCL_

#include "json.h"


JsonNode *lister(char *username, char *device, char *yyyymm);
void locations(char *filename, JsonNode *obj, JsonNode *arr);

#endif
