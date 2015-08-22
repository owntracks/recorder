#include "config.h"
#include "udata.h"
#ifdef HAVE_REDIS
# include <hiredis/hiredis.h>
#endif
#include "geohash.h"
#include "utstring.h"
#include "json.h"

#ifndef TRUE
# define TRUE 1
# define FALSE 0
#endif

#ifdef HAVE_REDIS
void redis_ping(redisContext **redis);
void last_storeredis(redisContext **redis, char *username, char *device, char *jsonstring);
#endif
int ghash_readcache(struct udata *ud, char *ghash, UT_string *addr, UT_string *cc);
void ghash_storecache(struct udata *ud, JsonNode *geo, char *ghash, char *addr, char *cc);
