#ifndef _GCACHE_H_INCLUDED_
# define _GCACHE_H_INCLUDED_

#include "json.h"
#include "lmdb.h"

#define LMDB_DB_SIZE	(100000 * 1024 * 1024)

struct gcache {
	MDB_env *env;
	MDB_dbi dbi;
};

struct gcache *gcache_open(char *path, char *dbname, int rdonly);
void gcache_close(struct gcache *);
int gcache_put(struct gcache *, char *ghash, char *payload);
int gcache_json_put(struct gcache *, char *ghash, JsonNode *geo);
long gcache_get(struct gcache *, char *key, char *buf, long buflen);
JsonNode *gcache_json_get(struct gcache *, char *key);
void gcache_dump(char *path, char *lmdbname);
void gcache_load(char *path, char *lmdbname);
int gcache_del(struct gcache *gc, char *keystr);
bool gcache_enum(char *user, char *device, struct gcache *gc, char *key_part, int (*func)(char *key, wpoint *wp, double lat, double lon), double lat, double lon, struct udata *ud, char *topic, JsonNode *json);

#endif
