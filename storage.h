#ifndef _STORAGE_H_INCL_
# define _STORAGE_H_INCL_

#include <time.h>
#include "gcache.h"
#include "json.h"

#define DEFAULT_HISTORY_HOURS 6

/* Output types */
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

/* JSON payload types */
typedef enum {
	T_UNKNOWN	= 0,
	T_BEACON,
	T_CARD,
	T_CMD,
	T_CONFIG,
	T_LOCATION,
	T_LWT,
	T_MSG,
	T_STEPS,
	T_TRANSITION,
	T_WAYPOINT,
	T_WAYPOINTS,
} payload_type;

JsonNode *lister(char *username, char *device, time_t s_lo, time_t s_hi, int reverse);
JsonNode *multilister(JsonNode *udpairs, time_t s_lo, time_t s_hi, int reverse);
void locations(char *filename, JsonNode *obj, JsonNode *arr, time_t s_lo, time_t s_hi, output_type otype, int limit, JsonNode *fields, char *username, char *device);
int make_times(char *time_from, time_t *s_lo, char *time_to, time_t *s_to);
JsonNode *geo_json(JsonNode *json);
JsonNode *geo_linestring(JsonNode *location_array);
JsonNode *kill_datastore(char *username, char *device);
JsonNode *last_users(char *user, char *device, JsonNode *fields);
char *gpx_string(JsonNode *json);
void storage_init(int revgeo);
void storage_gcache_dump(char *lmdbname);
void storage_gcache_load(char *lmdbname);
void xml_output(JsonNode *json, output_type otype, JsonNode *fields, void (*func)(char *s, void *param), void *param);
char *storage_userphoto(char *username);

#endif
