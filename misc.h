#ifndef MISC_H_INCLUDED
# define MISC_H_INCLUDED

#include <time.h>
#include "udata.h"

extern char STORAGEDIR[];

#ifndef TRUE
# define TRUE (1)
# define FALSE (0)
#endif

int mkpath(char *path);
char *bindump(char *buf, long buflen);

// void monitor_update(struct udata *ud, time_t now, char *topic);
void monitorhook(struct udata *userdata, time_t now, char *topic);
char *monitor_get();
void get_defaults(char *filename, struct udata *userdata);
void display_json_variables(struct udata *userdata, bool plain);

#endif
