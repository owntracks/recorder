#ifndef _UTIL_H_INCL_
# define  _UTIL_H_INCL_

#ifndef TRUE
# define TRUE (1)
# define FALSE (0)
#endif

#include <time.h>
#include "json.h"

const char *isotime(time_t t);
char *slurp_file(char *filename, int fold_newlines);
int json_copy_to_object(JsonNode * obj, JsonNode * object_or_array, int clobber);
int json_copy_from_file(JsonNode * obj, char *filename);
int splitter(char *s, char *sep, char **parts);
JsonNode *json_splitter(char *s, char *sep);

#endif
