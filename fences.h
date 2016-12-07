#ifndef FENCES_H_INCLUDED
# define FENCES_H_INCLUDED

#include <stdbool.h>

typedef struct {
	double lat;
	double lon;
	long rad;
	char *desc;
	bool io;	/* true: is IN region. false: is OUT of region */
	char *user;
	char *device;
	enum { ENTER, LEAVE } event;
	struct udata *ud;
} wpoint;

void check_fences(struct udata *ud, char *username, char *device, double lat, double lon, JsonNode *json);


#endif

