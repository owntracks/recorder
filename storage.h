#ifndef _STORAGE_H_INCL_
# define _STORAGE_H_INCL_

#include <time.h>
#include "json.h"


JsonNode *lister(char *username, char *device, time_t s_lo, time_t s_hi);
void locations(char *filename, JsonNode *obj, JsonNode *arr, time_t s_lo, time_t s_hi);
int make_times(char *time_from, time_t *s_lo, char *time_to, time_t *s_to);

#endif
