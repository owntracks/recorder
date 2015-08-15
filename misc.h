#ifndef MISC_H_INCLUDED
# define MISC_H_INCLUDED

#include <time.h>
char *bindump(char *buf, long buflen);
void monitorhook(time_t now, char *topic);

#endif
