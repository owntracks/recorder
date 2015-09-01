#ifndef _GCACHE_H_INCLUDED_
# define _GCACHE_H_INCLUDED_

#ifdef HAVE_LMDB

#include "json.h"
#include "lmdb.h"

#define LMDB_DB_SIZE	(120 * 1024 * 1024)

struct gcache {
	MDB_env *env;
	MDB_dbi dbi;
};

struct gcache *gcache_open(char *path, int rdonly);
void gcache_close(struct gcache *);
int gcache_put(struct gcache *, char *ghash, char *payload);
int gcache_json_put(struct gcache *, char *ghash, JsonNode *geo);
int gcache_get(struct gcache *, char *key);
JsonNode *gcache_json_get(struct gcache *, char *key);

#endif
#endif
