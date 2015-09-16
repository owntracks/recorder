#ifndef _STORAGE_H_INCL_
# define _STORAGE_H_INCL_

#include <time.h>
#include "gcache.h"
#include "json.h"

#define DEFAULT_HISTORY_HOURS 6

typedef enum {
	GEOJSON = 0,
	CSV,
	JSON,
	RAW,
	GPX,
	RAWPAYLOAD,
	LINESTRING,
	XML,
} output_type;

JsonNode *lister(char *username, char *device, time_t s_lo, time_t s_hi, int reverse);
void locations(char *filename, JsonNode *obj, JsonNode *arr, time_t s_lo, time_t s_hi, output_type otype, int limit, JsonNode *fields);
int make_times(char *time_from, time_t *s_lo, char *time_to, time_t *s_to);
JsonNode *geo_json(JsonNode *json);
JsonNode *geo_linestring(JsonNode *location_array);
JsonNode *kill_datastore(char *username, char *device);
JsonNode *last_users(char *user, char *device);
char *gpx_string(JsonNode *json);
void storage_init(int revgeo);
void storage_gcache_dump();
void storage_gcache_load();
void xml_output(JsonNode *json, output_type otype, JsonNode *fields, void (*func)(char *s, void *param), void *param);

#endif
