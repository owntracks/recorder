#include <stdlib.h>
#include <unistd.h>
#include "ghash.h"
#include "misc.h"

#ifdef HAVE_REDIS

void redis_ping(redisContext **redis)
{
	struct timeval timeout = { 1, 500000 }; // 1.5 seconds
	redisReply *r;
	int i = 0;

	do {
		if ((r = redisCommand(*redis,"PING")) != NULL) {
			// printf("PING: %s\n", r->str);
			freeReplyObject(r);
			return;
		}
		fprintf(stderr, "REDIS: %d  %s\n", (*redis)->err, (*redis)->errstr);
		
		*redis = redisConnectWithTimeout("localhost", 6379, timeout);

		fprintf(stderr, "Reconnecting to Redis...\n");
		sleep(5);
	} while (i++ < 10);
	
}

void ghash_store_redis(redisContext **redis, char *ghash, char *addr, char *cc)
{
	redisReply *r;

	redis_ping(redis);

	r = redisCommand(*redis, "HMSET ghash:%s cc %s addr %s", ghash, cc, addr);
	if (r) /* FIXME */
		return;
}


void last_storeredis(redisContext **redis, char *username, char *device, char *jsonstring)
{
	redisReply *r;

	redis_ping(redis);

	r = redisCommand(*redis, "SET lastpos:%s-%s %s", username, device, jsonstring);
	if (r) /* FIXME */
		return;
}
int ghash_get_redis_cache(redisContext **redis, char *ghash, UT_string *addr, UT_string *cc)
{
	redisReply *reply;
	int found = FALSE;

	redis_ping(redis);

	reply = redisCommand(*redis, "HGETALL ghash:%s", ghash);
	if (reply == NULL) {
		fprintf(stderr, "REDIS: %d  %s\n", (*redis)->err, (*redis)->errstr);
		return (FALSE);

	}
	if ( reply->type == REDIS_REPLY_ERROR ) {
	  fprintf(stderr, "Error: %s\n", reply->str );
	  return (FALSE);
	}
	else if ( reply->type != REDIS_REPLY_ARRAY )
	  printf( "Unexpected type: %d\n", reply->type );
	if (reply->type == REDIS_REPLY_ARRAY) {
		int i;
		char *key, *val;

		if (reply->elements >= 1) {
			for (i = 0; i < (reply->elements - 1); i += 2) {
				key = reply->element[i]->str;
				val = reply->element[i+1]->str;

				if (!strcmp(key, "addr"))
					utstring_printf(addr, "%s", val);
				else if (!strcmp(key, "cc"))
					utstring_printf(cc, "%s", val);
			}
			found = TRUE;
		}
	}

	freeReplyObject(reply);

	return (found);
}

void monitor_update(struct udata *ud, time_t now, char *topic)
{
	redisReply *r;

	redis_ping(&ud->redis);

	r = redisCommand(ud->redis, "HMSET ot-recorder-monitor time %ld topic %s", now, topic);
	if (r) /* FIXME */
		return;

}

#endif /* !HAVE_REDIS */

int ghash_readcache(struct udata *ud, char *ghash, UT_string *addr, UT_string *cc)
{
	int cached = FALSE;
	char gfile[BUFSIZ];
	FILE *fp;

	/* FIXME */

#ifdef HAVE_REDIS
	if (ud->useredis) {
		if (ghash_get_redis_cache( &ud->redis, ghash, addr, cc) == TRUE) {
			return (TRUE);
		}
	}
#endif

	if (ud->usefiles) {
		/* if ghash file is available, read cc:addr into that */
		snprintf(gfile, BUFSIZ, "%s/ghash/%-3.3s/%s.json", STORAGEDIR, ghash, ghash);

		// fprintf(stderr, "Reading GhashCache from %s\n", gfile);
		if ((fp = fopen(gfile, "r")) != NULL) {
			char buf[BUFSIZ];

			/* FIXME: read JSON */
			if (fgets(buf, sizeof(buf), fp) != NULL) {
				JsonNode *json, *j;

				if ((json = json_decode(buf)) == NULL) {
					puts("  FIXME: can't decode JSON");
				} else {

					if ((j = json_find_member(json, "cc")) != NULL) {
						if (j->tag == JSON_STRING) {
							utstring_printf(cc, "%s", j->string_);
						}
					}
					if ((j = json_find_member(json, "addr")) != NULL) {
						if (j->tag == JSON_STRING) {
							utstring_printf(addr, "%s", j->string_);
						}
					}

					json_delete(json);
					cached = TRUE;
				}

				fclose(fp);
			}
		} else {
			// fprintf(stderr, "Not cached: ghash: %s\n", gfile);
		}
	}

	return (cached);
}


void ghash_storecache(struct udata *ud, JsonNode *geo, char *ghash, char *addr, char *cc)
{
	char gfile[BUFSIZ];
	FILE *fp;

#ifdef HAVE_REDIS
	if (ud->useredis) {
		ghash_store_redis(&ud->redis, ghash, addr, cc);
	}
#endif

	if (ud->usefiles) {
		snprintf(gfile, BUFSIZ, "%s/ghash", STORAGEDIR);
		if (mkpath(gfile) < 0) {
			perror(gfile);
		} else {
			char *js;

			if ((js = json_stringify(geo, NULL)) != NULL) {
				snprintf(gfile, BUFSIZ, "%s/ghash/%-3.3s", STORAGEDIR, ghash);
				if (mkpath(gfile) != 0) {
					perror(gfile);
					return;
				}
				snprintf(gfile, BUFSIZ, "%s/ghash/%-3.3s/%s.json", STORAGEDIR, ghash, ghash);
				if ((fp = fopen(gfile, "w")) != NULL) {
					fprintf(fp, "%s\n", js);
					fclose(fp);
				}

				free(js);
			}
		}
	}
}
