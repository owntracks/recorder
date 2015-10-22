/*
 * Copyright (C) 2015 Jan-Piet Mens <jpmens@gmail.com> and OwnTracks
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * JAN-PIET MENS OR OWNTRACKS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <mosquitto.h>
#include <getopt.h>
#include <time.h>
#include <math.h>
#include "json.h"
#include <sys/utsname.h>
#include <regex.h>
#include "utstring.h"
#include "geo.h"
#include "geohash.h"
#include "base64.h"
#include "misc.h"
#include "util.h"
#include "storage.h"
#ifdef WITH_LMDB
# include "gcache.h"
#endif
#ifdef WITH_HTTP
# include "http.h"
#endif
#ifdef WITH_LUA
# include "hooks.h"
#endif


#define SSL_VERIFY_PEER (1)
#define SSL_VERIFY_NONE (0)

#define TOPIC_PARTS     (4)             /* owntracks/user/device/info */
#define DEFAULT_QOS	(2)
#define CLEAN_SESSION	false

static int run = 1;

double number(JsonNode *j, char *element)
{
	JsonNode *m;
	double d;

	if ((m = json_find_member(j, element)) != NULL) {
		if (m->tag == JSON_NUMBER) {
			return (m->number_);
		} else if (m->tag == JSON_STRING) {
			d = atof(m->string_);
			/* Normalize to number */
			json_remove_from_parent(m);
			json_append_member(j, element, json_mknumber(d));
			return (d);
		}
	}

	return (NAN);
}

static const char *ltime(time_t t) {
	static char buf[] = "HH:MM:SS";

	strftime(buf, sizeof(buf), "%T", localtime(&t));
	return(buf);
}

/*
 * Process info/ message containing a CARD. If the payload is a card, return TRUE.
 */

int do_info(void *userdata, UT_string *username, UT_string *device, JsonNode *json)
{
	struct udata *ud = (struct udata *)userdata;
	JsonNode *j;
	static UT_string *name = NULL, *face = NULL;
	FILE *fp;
	char *img;
	int rc = FALSE;

	utstring_renew(name);
	utstring_renew(face);

	/* I know the payload is valid JSON: write card */

	if ((fp = pathn("wb", "cards", username, NULL, "json")) != NULL) {
		char *js = json_stringify(json, NULL);
		if (js) {
			fprintf(fp, "%s\n", js);
			free(js);
		}
		fclose(fp);
	}
	rc = TRUE;

	if ((j = json_find_member(json, "name")) != NULL) {
		if (j->tag == JSON_STRING) {
			// printf("I got: [%s]\n", j->string_);
			utstring_printf(name, "%s", j->string_);
		}
	}

	if ((j = json_find_member(json, "face")) != NULL) {
		if (j->tag == JSON_STRING) {
			// printf("I got: [%s]\n", j->string_);
			utstring_printf(face, "%s", j->string_);

		}
	}

	if (ud->verbose) {
		printf("* CARD: %s-%s %s\n", UB(username), UB(device), UB(name));
	}


	/* We have a base64-encoded "face". Decode it and store binary image */
	if ((img = malloc(utstring_len(face))) != NULL) {
		int imglen;

		if ((imglen = base64_decode(UB(face), img)) > 0) {
			if ((fp = pathn("wb", "photos", username, NULL, "png")) != NULL) {
				fwrite(img, sizeof(char), imglen, fp);
				fclose(fp);
			}
		}
		free(img);
	}


	return (rc);
}

void do_msg(void *userdata, UT_string *username, UT_string *device, JsonNode *json)
{
	struct udata *ud = (struct udata *)userdata;
	FILE *fp;

	/* I know the payload is valid JSON: write message */

	if ((fp = pathn("ab", "msg", username, NULL, "json")) != NULL) {
		char *js = json_stringify(json, NULL);

		if (js) {
			fprintf(fp, "%s\n", js);
			free(js);
		}
		fclose(fp);
	}

	if (ud->verbose) {
		printf("* MSG: %s-%s\n", UB(username), UB(device));
	}
}

void republish(struct mosquitto *mosq, struct udata *userdata, char *username, char *topic, double lat, double lon, char *cc, char *addr, long tst, char *t)
{
	struct udata *ud = (struct udata *)userdata;
        JsonNode *json;
	static UT_string *newtopic = NULL;
        char *payload;

	if (ud->pubprefix == NULL)
		return;

        if ((json = json_mkobject()) == NULL) {
                return;
        }

	utstring_renew(newtopic);

	utstring_printf(newtopic, "%s/%s", ud->pubprefix, topic);

        json_append_member(json, "username",	json_mkstring(username));
        json_append_member(json, "topic",	json_mkstring(topic));
        json_append_member(json, "cc",		json_mkstring(cc));
        json_append_member(json, "addr",	json_mkstring(addr));
        json_append_member(json, "t",		json_mkstring(t));
        json_append_member(json, "tst",		json_mknumber(tst));
        json_append_member(json, "lat",		json_mknumber(lat));
        json_append_member(json, "lon",		json_mknumber(lon));


        if ((payload = json_stringify(json, NULL)) != NULL) {
                mosquitto_publish(mosq, NULL, UB(newtopic),
                                strlen(payload), payload, 1, true);
		fprintf(stderr, "%s %s\n", UB(newtopic), payload);
                free(payload);
        }

        json_delete(json);

}

/*
 * Quickly check wheterh the payload looks like
 * Greenwich CSV with a regex. We could use this
 * to split out the fields, instead of reverting
 * to sscanf
 */

//                TID          , TST           , T         , LAT        , LON        , COG        , VEL        , ALT        , DIST       , TRIP
#define CSV_RE "^([[:alnum:]]+),([[:xdigit:]]+),[[:alnum:]],[[:digit:]]+,[[:digit:]]+,[[:digit:]]+,[[:digit:]]+,[[:digit:]]+,[[:digit:]]+,[[:digit:]]+$"

static int csv_looks_sane(char *payload)
{
	static int virgin = 1;
	static regex_t regex;
	int nomatch;
	int cflags = REG_EXTENDED | REG_ICASE | REG_NOSUB;

	if (virgin) {
		virgin = !virgin;

		if (regcomp(&regex, CSV_RE, cflags)) {
			olog(LOG_ERR, "Cannot compile CSV RE");
			return (FALSE);
		}
	}

	nomatch = regexec(&regex, payload, 0, NULL, 0);

	return (nomatch ? FALSE : TRUE);
}

/*
 * Decode OwnTracks CSV (Greenwich) and return a new JSON object
 * of _type = location.
 * #define CSV "X0,542A46AA,k,30365854,7575769,26,4,7,5,872"
 */

#define MILL 1000000.0

JsonNode *csv_to_json(char *payload)
{
	JsonNode *json;
	char tid[64], t[10];
        double dist = 0, lat, lon, vel, trip, alt, cog;
	long tst;
	char tmptst[40];

	if (!csv_looks_sane(payload))
		return (NULL);

        if (sscanf(payload, "%[^,],%[^,],%[^,],%lf,%lf,%lf,%lf,%lf,%lf,%lf", tid, tmptst, t, &lat, &lon, &cog, &vel, &alt, &dist, &trip) != 10) {
		// fprintf(stderr, "**** payload not CSV: %s\n", payload);
                return (NULL);
        }

        lat /= MILL;
        lon /= MILL;
        cog *= 10;
        alt *= 10;
        trip *= 1000;

	tst = strtoul(tmptst, NULL, 16);

	json = json_mkobject();
	json_append_member(json, "_type", json_mkstring("location"));
	json_append_member(json, "t",	  json_mkstring(t));
	json_append_member(json, "tid",	  json_mkstring(tid));
	json_append_member(json, "tst",	  json_mknumber(tst));
	json_append_member(json, "lat",	  json_mknumber(lat));
	json_append_member(json, "lon",	  json_mknumber(lon));
	json_append_member(json, "cog",	  json_mknumber(cog));
	json_append_member(json, "vel",	  json_mknumber(vel));
	json_append_member(json, "alt",	  json_mknumber(alt));
	json_append_member(json, "dist",  json_mknumber(dist));
	json_append_member(json, "trip",  json_mknumber(trip));
	json_append_member(json, "csv",   json_mkbool(1));

	return (json);
}

#define RECFORMAT "%s\t%-18s\t%s\n"

/*
 * Store payload in REC file unless our Lua putrec() function says
 * we shouldn't for this particular user/device combo.
 */

static void putrec(struct udata *ud, time_t now, UT_string *reltopic, UT_string *username, UT_string *device, char *string)
{
	FILE *fp;
	int rc = 0;

#ifdef WITH_LUA
	rc = hooks_norec(ud, UB(username), UB(device), string);
#endif

	if (rc == 0) {
		if ((fp = pathn("a", "rec", username, device, "rec")) == NULL) {
			olog(LOG_ERR, "Cannot write REC for %s/%s: %m",
				UB(username), UB(device));
		}

		fprintf(fp, RECFORMAT, isotime(now),
			UB(reltopic), string);
		fclose(fp);
	}
}

/*
 * Payload contains JSON string with a configuration obtained
 * via cmd `dump' to the device. Store it "pretty".
 */

static char *prettyfy(char *payloadstring)
{
	JsonNode *json;
	char *pretty_js;

	if ((json = json_decode(payloadstring)) == NULL) {
		olog(LOG_ERR, "Cannot decode JSON from %s", payloadstring);
		return (NULL);
	}
	pretty_js = json_stringify(json, "\t");
	json_delete(json);

	return (pretty_js);
}

static void xx_dump(struct udata *ud, UT_string *username, UT_string *device, char *payloadstring, char *type)
{
	static UT_string *ts = NULL;
	char *pretty_js = prettyfy(payloadstring);

	utstring_renew(ts);
	utstring_printf(ts, "%s/%s/%s/%s",
				STORAGEDIR,
				type,
				UB(username),
				UB(device));
	if (mkpath(UB(ts)) < 0) {
		olog(LOG_ERR, "Cannot mkdir %s: %m", UB(ts));
		if (pretty_js) free(pretty_js);
		return;
	}

	utstring_printf(ts, "/%s-%s.otrc", UB(username), UB(device));
	if (ud->verbose) {
		printf("Received %s dump, storing at %s\n", type, UB(ts));
	}
	safewrite(UB(ts), (pretty_js) ? pretty_js : payloadstring);
	if (pretty_js) free(pretty_js);
}

/* Dump a config payload */
void config_dump(struct udata *ud, UT_string *username, UT_string *device, char *payloadstring)
{
	xx_dump(ud, username, device, payloadstring, "config");
}

/* Dump a waypoints (plural) payload */
void waypoints_dump(struct udata *ud, UT_string *username, UT_string *device, char *payloadstring)
{
	xx_dump(ud, username, device, payloadstring, "waypoints");
}

#ifdef WITH_RONLY
static int is_ronly(struct udata *ud, UT_string *basetopic)
{
	JsonNode *json, *j;
	char *key = UB(basetopic);
	int active = FALSE;

	if ((json = gcache_json_get(ud->ronlydb, key)) == NULL)
		return (FALSE);

	if ((j = json_find_member(json, "active")) != NULL) {
		active = j->bool_;
	}

	printf("**--- %s: return active = %d\n", key, active);
	return (active);
}

/*
 * Make an RONLYdb entry for basetopic, updating timestamp in the JSON
 * active is TRUE if the user is an RONLY user, else FALSE.
 */

static void ronly_set(struct udata *ud, UT_string *basetopic, int active)
{
	JsonNode *json, *j;
	char *key = UB(basetopic);
	int rc, touch = FALSE;

	json = gcache_json_get(ud->ronlydb, key);
	if (json == NULL) {

		if (active == FALSE)		/* Has never been r:true b/c not in RONLYdb */
			return;

		json = json_mkobject();
	}

	if ((j = json_find_member(json, "first")) == NULL) {
		json_append_member(json, "first", json_mknumber(time(0)));
		touch = TRUE;
	}

	if ((j = json_find_member(json, "last")) != NULL)
		json_remove_from_parent(j);

	if ((j = json_find_member(json, "active")) != NULL) {
		if (active != j->bool_) {
			json_remove_from_parent(j);
			json_append_member(json, "active", json_mkbool(active));
			json_append_member(json, "last", json_mknumber(time(0)));
			touch = TRUE;
		} else if (active == TRUE) {
			json_append_member(json, "last", json_mknumber(time(0)));
			touch = TRUE;
		}
	} else {
		json_append_member(json, "active", json_mkbool(active));
		touch = TRUE;
	}


	if (touch) {

		if ((rc = gcache_json_put(ud->ronlydb, key, json)) != 0)
			olog(LOG_ERR, "Cannot store %s in ronlydb: rc==%d", key, rc);

		printf("+++++++++ TOUCH db for %s\n", key);
	}

	json_delete(json);
}
#endif

void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *m)
{
	JsonNode *json, *j, *geo = NULL;
	char *tid = NULL, *t = NULL, *p;
	double lat, lon;
	long tst;
	struct udata *ud = (struct udata *)userdata;
        char **topics;
        int count = 0, cached;
	static UT_string *basetopic = NULL, *username = NULL, *device = NULL, *addr = NULL, *cc = NULL, *ghash = NULL, *ts = NULL;
	static UT_string *reltopic = NULL;
	char *jsonstring, *_typestr = NULL;
	time_t now;
	int pingping = FALSE, skipslash = 0;
	int r_ok = TRUE;			/* True if recording enabled for a publish */
	payload_type _type;

	/*
	 * mosquitto_message->
	 * 	 int mid;
	 * 	 char *topic;
	 * 	 void *payload;
	 * 	 int payloadlen;
	 * 	 int qos;
	 * 	 bool retain;
	 */

	time(&now);
	monitorhook(ud, now, m->topic);

	if (m->payloadlen == 0) {
		return;
	}

	if (m->retain == TRUE && ud->ignoreretained) {
		return;
	}

	// printf("%s %s\n", m->topic, bindump(m->payload, m->payloadlen)); fflush(stdout);


	utstring_renew(ts);
	utstring_renew(basetopic);
	utstring_renew(username);
	utstring_renew(device);

        if (mosquitto_sub_topic_tokenise(m->topic, &topics, &count) != MOSQ_ERR_SUCCESS) {
		return;
	}

	/*
	 * Do we have a leading / in topic?
	 * Also, if topic is too short, ignore and return. We *demand* 3 parts
	 * i.e. "owntracks/user/device"
	 */

	if (topics[0] == NULL) {
		/* Topic has leading / */
		skipslash = 1;
	}
	if (count - skipslash < 3) {
		fprintf(stderr, "Ignoring short topic %s\n", m->topic);
		mosquitto_sub_topic_tokens_free(&topics, count);
		return;
	}

	/*
	 * Determine "relative topic", relative to base, i.e. whatever comes
	 * behind ownntracks/user/device/. If it's the base topic, use "*".
	 */

	utstring_renew(reltopic);
	if (count != (3 + skipslash)) {
		int j;

		for (j = 3 + skipslash; j < count; j++) {
			utstring_printf(reltopic, "%s%c", topics[j], (j < count - 1) ? '/' : ' ');
		}
        } else {
		utstring_printf(reltopic, "*");
	}
	if (utstring_len(reltopic) == 0)
		utstring_printf(reltopic, "-");


	utstring_printf(basetopic, "%s/%s/%s", topics[0 + skipslash], topics[1 + skipslash], topics[2 + skipslash]);
	utstring_printf(username, "%s", topics[1 + skipslash]);
	utstring_printf(device, "%s", topics[2 + skipslash]);

	mosquitto_sub_topic_tokens_free(&topics, count);

#ifdef WITH_PING
	if (!strcmp(UB(username), "ping") && !strcmp(UB(device), "ping")) {
		pingping = TRUE;
	}
#endif

	/*
	 * First things first: let's see if this contains some sort of valid JSON
	 * or an OwnTracks CSV. If it doesn't, just store this payload because
	 * there's nothing left for us to do with it.
	 */

	if ((json = json_decode(m->payload)) == NULL) {
		if ((json = csv_to_json(m->payload)) == NULL) {
#ifdef WITH_RONLY
			/*
			 * If the base topic belongs to an RONLY user, store
			 * the payload.
			 */

			if (is_ronly(ud, basetopic)) {
				// puts("*** storing plain publis");
				putrec(ud, now, reltopic, username, device, bindump(m->payload, m->payloadlen));
			}
#else
			/* It's not JSON or it's not a location CSV; store it */
			putrec(ud, now, reltopic, username, device, bindump(m->payload, m->payloadlen));
#endif
			return;
		}
	}

	if (ud->skipdemo && (json_find_member(json, "_demo") != NULL)) {
		json_delete(json);
		return;
	}

#ifdef WITH_RONLY

	/*
	 * This is a special mode in which location (and a few other)
	 * publishes will be recorded only if r:true in the payload.
	 * If we cannot find `r' in the JSON, or if `r' isn't true,
	 * set r_ok to FALSE. We cannot just bail out here, because
	 * we still want info, cards &c.
	 */

	if ((j = json_find_member(json, "r")) == NULL) {

		r_ok = FALSE;

		/*
		 * This JSON payload might actually belong to an RONLY user
		 * but it doesn't have an `r:true' in it. Determine whether
		 * the basetopic belongs to such a user, and force r_ok
		 * accordingly.
		 */

		if (is_ronly(ud, basetopic)) {
			r_ok = TRUE;
			// printf("*** forcing TRUE b/c ronlydb (blen=%ld)\n", blen);
		}
	} else {
		r_ok = TRUE;
		if ((j->tag != JSON_BOOL) || (j->bool_ == FALSE)) {
			r_ok = FALSE;
		}
	}

	/*
	 * Record the RONLY basetopic in RONLYdb, and indicate active or not
	 */

	ronly_set(ud, basetopic, r_ok);

#endif

	_type = T_UNKNOWN;
	if ((j = json_find_member(json, "_type")) != NULL) {
		if (j->tag == JSON_STRING) {
			_typestr = strdup(j->string_);
			if (!strcmp(j->string_, "location"))		_type = T_LOCATION;
			else if (!strcmp(j->string_, "beacon"))		_type = T_BEACON;
			else if (!strcmp(j->string_, "card"))		_type = T_CARD;
			else if (!strcmp(j->string_, "cmd"))		_type = T_CMD;
			else if (!strcmp(j->string_, "configuration"))	_type = T_CONFIG;
			else if (!strcmp(j->string_, "lwt"))		_type = T_LWT;
			else if (!strcmp(j->string_, "msg"))		_type = T_MSG;
			else if (!strcmp(j->string_, "steps"))		_type = T_STEPS;
			else if (!strcmp(j->string_, "transition"))	_type = T_TRANSITION;
			else if (!strcmp(j->string_, "waypoint"))	_type = T_WAYPOINT;
			else if (!strcmp(j->string_, "waypoints"))	_type = T_WAYPOINTS;
			else if (!strcmp(j->string_, "dump"))		_type = T_DUMP;
		}
	}

	switch (_type) {
		case T_CARD:
			do_info(ud, username, device, json);
			goto cleanup;
		case T_MSG:
			do_msg(ud, username, device, json);
			goto cleanup;
		case T_BEACON:
		case T_CMD:
		case T_CONFIG:
		case T_LWT:
		case T_STEPS:
			if (r_ok) {
				putrec(ud, now, reltopic, username, device, bindump(m->payload, m->payloadlen));
			}
			goto cleanup;
		case T_WAYPOINTS:
			waypoints_dump(ud, username, device, m->payload);
			goto cleanup;
		case T_DUMP:
			config_dump(ud, username, device, m->payload);
			goto cleanup;
		case T_WAYPOINT:
		case T_TRANSITION:
		case T_LOCATION:
			break;
		default:
			if (r_ok) {
				putrec(ud, now, reltopic, username, device, bindump(m->payload, m->payloadlen));
			}
			goto cleanup;
	}


	if (r_ok == FALSE)
		goto cleanup;

	/*
	 * We are now handling location-related JSON.  Normalize tst, lat, lon
	 * to numbers, particularly for Greenwich which produces strings
	 * currently. We're normalizing *in* json which replaces strings by
	 * numbers.
	 */

	tst = time(NULL);
	if ((j = json_find_member(json, "tst")) != NULL) {
		if (j && j->tag == JSON_STRING) {
			tst = strtoul(j->string_, NULL, 10);
			json_remove_from_parent(j);
			json_append_member(json, "tst", json_mknumber(tst));
		} else {
			tst = (unsigned long)j->number_;
		}
	}

	if (isnan(lat = number(json, "lat")) || isnan(lon = number(json, "lon"))) {
		olog(LOG_ERR, "lat or lon for %s are NaN: %s", m->topic, bindump(m->payload, m->payloadlen));
		goto cleanup;
	}

	if ((j = json_find_member(json, "tid")) != NULL) {
		if (j->tag == JSON_STRING) {
			tid = strdup(j->string_);
		}
	}

	if ((j = json_find_member(json, "t")) != NULL) {
		if (j && j->tag == JSON_STRING) {
			t = strdup(j->string_);
		}
	}

#ifdef WITH_LMDB
	/*
	 * If the topic we are handling is in topic2tid, replace the TID
	 * in this payload with that from the database.
	 */

	if (ud->t2t) {
		char newtid[BUFSIZ];
		long blen;

		if ((blen = gcache_get(ud->t2t, m->topic, newtid, sizeof(newtid))) > 0) {
			if ((j = json_find_member(json, "tid")) != NULL)
				json_remove_from_parent(j);
			json_append_member(json, "tid", json_mkstring(newtid));
		}
	}
#endif

	/*
	 * Chances are high that what we have now contains lat, lon. Attempt to
	 * perform or retrieve reverse-geo.
	 */

	utstring_renew(ghash);
	utstring_renew(addr);
	utstring_renew(cc);
        p = geohash_encode(lat, lon, geohash_prec());
	if (p != NULL) {
		utstring_printf(ghash, "%s", p);
		free(p);
	}


	cached = FALSE;
	if (ud->revgeo == TRUE) {
#ifdef WITH_LMDB
		if ((geo = gcache_json_get(ud->gc, UB(ghash))) != NULL) {
			/* Habemus cached data */
			
			cached = TRUE;

			if ((j = json_find_member(geo, "cc")) != NULL) {
				utstring_printf(cc, "%s", j->string_);
			}
			if ((j = json_find_member(geo, "addr")) != NULL) {
				utstring_printf(addr, "%s", j->string_);
			}
		} else {
			if ((geo = revgeo(lat, lon, addr, cc)) != NULL) {
				gcache_json_put(ud->gc, UB(ghash), geo);
			} else {
				/* We didn't obtain reverse Geo, maybe because of over
				 * quota; make a note of the missing geohash */

				char gfile[BUFSIZ];
				FILE *fp;

				snprintf(gfile, BUFSIZ, "%s/ghash/missing", STORAGEDIR);
				if ((fp = fopen(gfile, "a")) != NULL) {
					fprintf(fp, "%s %lf %lf\n", UB(ghash), lat, lon);
					fclose(fp);
				}
			}
		}
#else /* !LMDB */
		if ((geo = revgeo(lat, lon, addr, cc)) != NULL) {
			;
		}
#endif /* LMDB */
	} else {
		utstring_printf(cc, "??");
		utstring_printf(addr, "n.a.");
	}


	/*
	 * We have normalized data in the JSON, so we can now write it
	 * out to the REC file.
	 */

	if (!pingping) {
		if ((jsonstring = json_stringify(json, NULL)) != NULL) {
			putrec(ud, now, reltopic, username, device, jsonstring);
			free(jsonstring);
		}
	}

	/*
	 * Append a few bits to the location type to add to LAST and
	 * for Lua / Websockets.
	 * I need a unique "key" in the Websocket clients to keep track
	 * of which device is being updated; use topic.
	 */

	json_append_member(json, "topic", json_mkstring(m->topic));

	/*
	 * We have to know which user/device this is for in order to
	 * determine whether a connected Websocket client is authorized
	 * to see this. Add user/device
	 */

	json_append_member(json, "username", json_mkstring(UB(username)));
	json_append_member(json, "device", json_mkstring(UB(device)));

	json_append_member(json, "ghash",    json_mkstring(UB(ghash)));

	if (_type == T_LOCATION || _type == T_WAYPOINT) {
		UT_string *filename = NULL;
		char *component;

		utstring_renew(filename);

		if (_type == T_LOCATION) {
				component = "last";
				utstring_printf(filename, "%s-%s.json",
					UB(username), UB(device));
		} else if (_type == T_WAYPOINT) {
				component = "waypoints";
				utstring_printf(filename, "%s.json", isotime(tst));
		}

		if ((jsonstring = json_stringify(json, NULL)) != NULL) {
			utstring_printf(ts, "%s/%s/%s/%s",
				STORAGEDIR,
				component,
				UB(username),
				UB(device));
			if (mkpath(UB(ts)) < 0) {
				olog(LOG_ERR, "Cannot mkdir %s: %m", UB(ts));
			}

			utstring_printf(ts, "/%s", UB(filename));
			safewrite(UB(ts), jsonstring);
			free(jsonstring);
		}
	}

	/*
	 * Now add more bits for Lua and Websocket, in particular the
	 * Geo data.
	 */

	if (geo) {
		json_copy_to_object(json, geo, FALSE);
	}

#ifdef WITH_HTTP
	if (ud->mgserver && !pingping) {
		http_ws_push_json(ud->mgserver, json);
	}
#endif

#ifdef WITH_LUA
# ifdef WITH_LMDB
	if (ud->luadata && !pingping) {
		hooks_hook(ud, m->topic, json);
	}
# endif /* LMDB */
#endif

	if (ud->verbose) {
		if (_type == T_LOCATION) {
			printf("%c %s %-35s t=%-1.1s tid=%-2.2s loc=%.5f,%.5f [%s] %s (%s)\n",
				(cached) ? '*' : '-',
				ltime(tst),
				m->topic,
				(t) ? t : " ",
				(tid) ? tid : "",
				lat, lon,
				UB(cc),
				UB(addr),
				UB(ghash)
			);
		} else if (_type == T_TRANSITION) {
			JsonNode *e, *d;

			e = json_find_member(json, "event");
			d = json_find_member(json, "desc");
			printf("transition: %s %s\n",
				(e) ? e->string_ : "unknown",
				(d) ? d->string_ : "unknown");

		} else if (_type == T_WAYPOINT) {
			j = json_find_member(json, "desc");
			printf("waypoint: %s\n", (j) ? j->string_ : "unknown desc");
		} else {
			if ((jsonstring = json_stringify(json, NULL)) != NULL) {
				printf("%s %s\n", _typestr, jsonstring);
				free(jsonstring);
			} else {
				printf("%s received\n", _typestr);
			}
		}
	}
	

    cleanup:
	if (geo)	json_delete(geo);
	if (json)	json_delete(json);
	if (tid)	free(tid);
	if (t)		free(t);
	if (_typestr)	free(_typestr);
}

void on_connect(struct mosquitto *mosq, void *userdata, int rc)
{
	struct udata *ud = (struct udata *)userdata;
	int mid;
	JsonNode *t;

	json_foreach(t, ud->topics) {
		if (t->tag == JSON_STRING) {
			olog(LOG_DEBUG, "Subscribing to %s (qos=%d)", t->string_, ud->qos);
			mosquitto_subscribe(mosq, &mid, t->string_, ud->qos);
		}
	}
}

static char *mosquitto_reason(int rc)
{
	static char *reasons[] = {
		"Connection accepted",					/* 0x00 */
		"Connection refused: incorrect protocol version",	/* 0x01 */
		"Connection refused: invalid client identifier",	/* 0x02 */
		"Connection refused: server unavailable",		/* 0x03 */
		"Connection refused: bad username or password",		/* 0x05 */
		"Connection refused: not authorized",			/* 0x06 */
		"Connection refused: TLS error",			/* 0x07 */
	};

	return ((rc >= 0 && rc <= 0x07) ? reasons[rc] : "unknown reason");
}


void on_disconnect(struct mosquitto *mosq, void *userdata, int reason)
{
#ifdef WITH_LMDB
	struct udata *ud = (struct udata *)userdata;
#endif

	if (reason == 0) { 	// client wish
#ifdef WITH_LMDB
		gcache_close(ud->gc);
#endif
	} else {
		olog(LOG_INFO, "Disconnected. Reason: 0x%X [%s]", reason, mosquitto_reason(reason));
	}
}

static void catcher(int sig)
{
        fprintf(stderr, "Going down on signal %d\n", sig);
	run = 0;
}

void usage(char *prog)
{
	printf("Usage: %s [options..] topic [topic ...]\n", prog);
	printf("  --help		-h	this message\n");
	printf("  --storage		-S     storage dir (%s)\n", STORAGEDEFAULT);
	printf("  --norevgeo		-G     disable ghash to reverge-geo lookups\n");
	printf("  --skipdemo 		-D     do not handle objects with _demo\n");
	printf("  --useretained		-R     process retained messages (default: no)\n");
	printf("  --clientid		-i     MQTT client-ID\n");
	printf("  --qos			-q     MQTT QoS (dflt: 2)\n");
	printf("  --pubprefix		-P     republish prefix (dflt: no republish)\n");
	printf("  --host		-H     MQTT host (localhost)\n");
	printf("  --port		-p     MQTT port (1883)\n");
	printf("  --logfacility		       syslog facility (local0)\n");
	printf("  --quiet		       disable printing of messages to stdout\n");
	printf("  --initialize		       initialize storage\n");
	printf("  --label <label>	       server label (dflt: Recorder)\n");
#ifdef WITH_HTTP
	printf("  --http-host <host>	       HTTP addr to bind to (localhost)\n");
	printf("  --http-port <port>	-A     HTTP port (8083); 0 to disable HTTP\n");
	printf("  --doc-root <directory>       document root (%s)\n", DOCROOT);
#endif
#ifdef WITH_LUA
	printf("  --lua-script <script.lua>    path to Lua script. If unset, no Lua hooks\n");
#endif
	printf("  --precision		       ghash precision (dflt: %d)\n", GHASHPREC);
	printf("  --hosted		       use OwnTracks Hosted\n");
	printf("\n");
	printf("Options override these environment variables:\n");
	printf("  $OTR_HOST		MQTT hostname\n");
	printf("  $OTR_PORT		MQTT port\n");
	printf("  $OTR_STORAGEDIR\n");
	printf("  $OTR_USER\n");
	printf("  $OTR_PASS\n");
	printf("  $OTR_CAFILE		PEM CA certificate chain\n");
	printf("For --hosted:\n");
	printf("  $OTR_USER		username as registered on Hosted\n");
	printf("  $OTR_DEVICE		connect as device\n");
	printf("  $OTR_TOKEN		device token\n");

	exit(1);
}


int main(int argc, char **argv)
{
	struct mosquitto *mosq = NULL;
	char err[1024], *p, *username, *password, *cafile, *device;
	char *hostname = "localhost", *logfacility = "local0";
#ifdef WITH_LUA
	char *luascript = NULL;
#endif
	int port = 1883;
	int rc, i, ch, hosted = FALSE, initialize = FALSE;
	static struct udata udata, *ud = &udata;
	struct utsname uts;
	UT_string *clientid;
#ifdef WITH_HTTP
	int http_port = 8083;
	char *doc_root = DOCROOT;
	char *http_host = "localhost";
#endif
	char *progname = *argv;

	udata.qos		= DEFAULT_QOS;
	udata.ignoreretained	= TRUE;
	udata.pubprefix		= NULL;
	udata.skipdemo		= TRUE;
	udata.revgeo		= TRUE;
	udata.verbose		= TRUE;
#ifdef WITH_LMDB
	udata.gc		= NULL;
	udata.t2t		= NULL;		/* Topic to TID */
# ifdef WITH_RONLY
	udata.ronlydb		= NULL;		/* RONLY db */
# endif
#endif
#ifdef WITH_HTTP
	udata.mgserver		= NULL;
#endif
#ifdef WITH_LUA
# ifdef WITH_LMDB
	udata.luadata		= NULL;
	udata.luadb		= NULL;
# endif /* WITH_LMDB */
#endif /* WITH_LUA */
	udata.label		= strdup("Recorder");

	if ((p = getenv("OTR_HOST")) != NULL) {
		hostname = strdup(p);
	}

	if ((p = getenv("OTR_PORT")) != NULL) {
		port = atoi(p);
	}

	if ((p = getenv("OTR_STORAGEDIR")) != NULL) {
		strcpy(STORAGEDIR, p);
	}

	utstring_new(clientid);
	utstring_printf(clientid, "ot-recorder");
	if (uname(&uts) == 0) {
		utstring_printf(clientid, "-%s", uts.nodename);
	}
	utstring_printf(clientid, "-%d", getpid());

	while (1) {
		static struct option long_options[] = {
			{ "help",	no_argument,		0, 	'h'},
			{ "skipdemo",	no_argument,		0, 	'D'},
			{ "norevgeo",	no_argument,		0, 	'G'},
			{ "useretained",	no_argument,		0, 	'R'},
			{ "clientid",	required_argument,	0, 	'i'},
			{ "pubprefix",	required_argument,	0, 	'P'},
			{ "qos",	required_argument,	0, 	'q'},
			{ "host",	required_argument,	0, 	'H'},
			{ "port",	required_argument,	0, 	'p'},
			{ "storage",	required_argument,	0, 	'S'},
			{ "logfacility",	required_argument,	0, 	4},
			{ "precision",	required_argument,	0, 	5},
			{ "hosted",	no_argument,		0, 	6},
			{ "quiet",	no_argument,		0, 	8},
			{ "initialize",	no_argument,		0, 	9},
			{ "label",	required_argument,	0, 	10},
#ifdef WITH_LUA
			{ "lua-script",	required_argument,	0, 	7},
#endif
#ifdef WITH_HTTP
			{ "http-host",	required_argument,	0, 	3},
			{ "http-port",	required_argument,	0, 	'A'},
			{ "doc-root",	required_argument,	0, 	2},
#endif
			{0, 0, 0, 0}
		  };
		int optindex = 0;

		ch = getopt_long(argc, argv, "hDGRi:P:q:S:H:p:A:", long_options, &optindex);
		if (ch == -1)
			break;

		switch (ch) {
			case 10:
				free(udata.label);
				udata.label = strdup(optarg);
				break;
			case 9:
				initialize = TRUE;
				break;
			case 8:
				ud->verbose = FALSE;
				break;
#ifdef WITH_LUA
			case 7:
				luascript = strdup(optarg);
				break;
#endif
			case 6:
				hosted = TRUE;
				break;
			case 5:
				geohash_setprec(atoi(optarg));
				break;
			case 4:
				logfacility = strdup(optarg);
				break;
#ifdef WITH_HTTP
			case 'A':	/* API */
				http_port = atoi(optarg);
				break;
			case 2:		/* no short char */
				doc_root = strdup(optarg);
				break;
			case 3:		/* no short char */
				http_host = strdup(optarg);
				break;
#endif
			case 'D':
				ud->skipdemo = FALSE;
				break;
			case 'G':
				ud->revgeo = FALSE;
				break;
			case 'i':
				utstring_clear(clientid);
				utstring_printf(clientid, "%s", optarg);
				break;
			case 'P':
				udata.pubprefix = strdup(optarg);	/* TODO: do we want this? */
				break;
			case 'q':
				ud->qos = atoi(optarg);
				if (ud->qos < 0 || ud->qos > 2) {
					fprintf(stderr, "%s: illegal qos\n", progname);
					exit(2);
				}
				break;
			case 'R':
				ud->ignoreretained = FALSE;
				break;
			case 'H':
				hostname = strdup(optarg);
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'S':
				strcpy(STORAGEDIR, optarg);
				break;
			case 'h':
				usage(progname);
				exit(0);
			default:
				exit(1);
		}

	}

	/*
	 * If requested to, attempt to create ghash storage and
	 * initialize (non-destructively -- just open for write)
	 * the LMDB databases.
	 */

	if (initialize == TRUE) {
#ifdef WITH_LMDB
		struct gcache *gt;
#endif

		char path[BUFSIZ], *pp;
		snprintf(path, BUFSIZ, "%s/ghash", STORAGEDIR);

		if (!is_directory(path)) {
			pp = strdup(path);
			if (mkpath(pp) < 0) {
				fprintf(stderr, "Cannot mkdir %s: %s", path, strerror(errno));
				exit(2);
			}
			free(pp);

		}

#ifdef WITH_LMDB
		if ((gt = gcache_open(path, NULL, FALSE)) == NULL) {
			fprintf(stderr, "Cannot lmdb-open MainDB\n");
			exit(2);
		}
		gcache_close(gt);

		if ((gt = gcache_open(path, "topic2tid", FALSE)) == NULL) {
			fprintf(stderr, "Cannot lmdb-open `topic2tid'\n");
			exit(2);
		}
		gcache_close(gt);
#ifdef WITH_LUA
		if ((gt = gcache_open(path, "luadb", FALSE)) == NULL) {
			fprintf(stderr, "Cannot lmdb-open `luadb'\n");
			exit(2);
		}
		gcache_close(gt);
#endif /* !LUA */
#ifdef WITH_RONLY
		if ((gt = gcache_open(path, "ronlydb", FALSE)) == NULL) {
			fprintf(stderr, "Cannot lmdb-open `ronly'\n");
			exit(2);
		}
		gcache_close(gt);
#endif /* !RONLY */
#endif
		exit(0);
	}

	argc -= (optind);
	argv += (optind);

	if (argc < 1) {
		usage(progname);
		return (-1);
	}

	if (hosted) {
		char tmp[BUFSIZ];

		hostname = strdup("hosted-mqtt.owntracks.org");
		port = 8883;

		if ((username = getenv("OTR_USER")) == NULL) {
			fprintf(stderr, "%s requires $OTR_USER\n", progname);
			exit(1);
		}
		if ((device = getenv("OTR_DEVICE")) == NULL) {
			fprintf(stderr, "%s requires $OTR_DEVICE\n", progname);
			exit(1);
		}
		if ((password = getenv("OTR_TOKEN")) == NULL) {
			fprintf(stderr, "%s requires $OTR_TOKEN\n", progname);
			exit(1);
		}
		if ((cafile = getenv("OTR_CAFILE")) == NULL) {
			fprintf(stderr, "%s requires $OTR_CAFILE\n", progname);
			exit(1);
		}

		utstring_renew(clientid);
		utstring_printf(clientid, "ot-RECORDER-%s-%s", username, device);
		if (uname(&uts) == 0) {
			utstring_printf(clientid, "-%s", uts.nodename);
		}

		snprintf(tmp, sizeof(tmp), "%s|%s", username, device);
		username = strdup(tmp);
	} else {
		username = getenv("OTR_USER");
		password = getenv("OTR_PASS");
	}

	openlog("ot-recorder", LOG_PID | LOG_PERROR, syslog_facility_code(logfacility));

#ifdef WITH_HTTP
	if (http_port) {
		if (!is_directory(doc_root)) {
			olog(LOG_ERR, "%s is not a directory", doc_root);
			exit(1);
		}
		/* First arg is user data which I can grab via conn->server_param  */
		udata.mgserver = mg_create_server(ud, ev_handler);
	}
#endif
	olog(LOG_DEBUG, "starting");

	if (ud->revgeo == TRUE) {
#ifdef WITH_LMDB
		char db_filename[BUFSIZ], *pa;

		snprintf(db_filename, BUFSIZ, "%s/ghash", STORAGEDIR);
		pa = strdup(db_filename);
		mkpath(pa);
		free(pa);
		udata.gc = gcache_open(db_filename, NULL, FALSE);
		if (udata.gc == NULL) {
			olog(LOG_ERR, "Can't initialize gcache in %s", db_filename);
			exit(1);
		}
		storage_init(ud->revgeo);	/* For the HTTP server */
#endif
		revgeo_init();
	}

#ifdef WITH_LMDB
	snprintf(err, sizeof(err), "%s/ghash", STORAGEDIR);
	ud->t2t = gcache_open(err, "topic2tid", TRUE);
# ifdef WITH_LUA
	ud->luadb = gcache_open(err, "luadb", FALSE);
# endif
# ifdef WITH_RONLY
	ud->ronlydb = gcache_open(err, "ronlydb", FALSE);
# endif
#endif

#if WITH_LUA && WITH_LMDB
	/*
	 * If option for lua-script has not been given, ignore all hooks.
	 */

	if (luascript) {
		if ((udata.luadata = hooks_init(ud, luascript)) == NULL) {
			olog(LOG_ERR, "Stopping because Lua load failed");
			exit(1);
		}
	}
#endif

	mosquitto_lib_init();


	signal(SIGINT, catcher);
	signal(SIGTERM, catcher);

	mosq = mosquitto_new(UB(clientid), CLEAN_SESSION, (void *)&udata);
	if (!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		mosquitto_lib_cleanup();
		return 1;
	}

	/*
	 * Pushing list of topics into the array so that we can (re)subscribe on_connect()
	 */

	ud->topics = json_mkarray();
	for (i = 0; i < argc; i++) {
		json_append_element(ud->topics, json_mkstring(argv[i]));
	}

	mosquitto_reconnect_delay_set(mosq,
			2, 	/* delay */
			20,	/* delay_max */
			0);	/* exponential backoff */

	mosquitto_message_callback_set(mosq, on_message);
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_disconnect_callback_set(mosq, on_disconnect);

	if (username && password) {
			mosquitto_username_pw_set(mosq, username, password);
	}

	cafile = getenv("OTR_CAFILE");

	if (cafile && *cafile) {

                rc = mosquitto_tls_set(mosq,
                        cafile,                 /* cafile */
                        NULL,                   /* capath */
                        NULL,                   /* certfile */
                        NULL,                   /* keyfile */
                        NULL                    /* pw_callback() */
                        );
                if (rc != MOSQ_ERR_SUCCESS) {
                        fprintf(stderr, "Cannot set TLS CA: %s (check path names)\n",
                                mosquitto_strerror(rc));
                        exit(3);
                }

                mosquitto_tls_opts_set(mosq,
                        SSL_VERIFY_PEER,
                        NULL,                   /* tls_version: "tlsv1.2", "tlsv1" */
                        NULL                    /* ciphers */
                        );

	}

	if (hosted) {
		olog(LOG_INFO, "connecting to Hosted as clientID %s", UB(clientid));
	} else {
		olog(LOG_INFO, "connecting to MQTT on %s:%d as clientID %s %s TLS",
			hostname, port,
			UB(clientid),
			(cafile) ? "with" : "without");
	}

	rc = mosquitto_connect(mosq, hostname, port, 60);
	if (rc) {
		if (rc == MOSQ_ERR_ERRNO) {
			strerror_r(errno, err, 1024);
			fprintf(stderr, "Error: %s\n", err);
		} else {
			fprintf(stderr, "Unable to connect (%d) [%s]: %s.\n",
				rc, mosquitto_strerror(rc), mosquitto_reason(rc));
		}
		mosquitto_lib_cleanup();
		return rc;
	}

#ifdef WITH_HTTP
	if (http_port) {
		char address[BUFSIZ];
		const char *addressinfo;

		sprintf(address, "%s:%d", http_host, http_port);

		mg_set_option(udata.mgserver, "listening_port", address);
		// mg_set_option(udata.mgserver, "listening_port", "8090,ssl://8091:cert.pem");

		// mg_set_option(udata.mgserver, "ssl_certificate", "cert.pem");
		// mg_set_option(udata.mgserver, "listening_port", "8091");

		mg_set_option(udata.mgserver, "document_root", doc_root);
		mg_set_option(udata.mgserver, "enable_directory_listing", "yes");
		mg_set_option(udata.mgserver, "auth_domain", "owntracks-recorder");
		// mg_set_option(udata.mgserver, "access_log_file", "access.log");
		// mg_set_option(udata.mgserver, "cgi_pattern", "**.cgi");

		addressinfo = mg_get_option(udata.mgserver, "listening_port");
		olog(LOG_INFO, "HTTP listener started on %s", addressinfo);
		if (addressinfo == NULL || *addressinfo == 0) {
			olog(LOG_ERR, "HTTP port is in use. Exiting.");
			exit(2);
		}

	}
#endif

	olog(LOG_INFO, "Using storage at %s with precision %d", STORAGEDIR, geohash_prec());

	while (run) {
		rc = mosquitto_loop(mosq, /* timeout */ 200, /* max-packets */ 1);
		if (run && rc) {
			olog(LOG_INFO, "MQTT connection: rc=%d [%s]. Sleeping...", rc, mosquitto_strerror(rc));
			sleep(10);
			mosquitto_reconnect(mosq);
		}
#ifdef WITH_HTTP
		if (udata.mgserver) {
			mg_poll_server(udata.mgserver, 100);
		}
#endif
	}

	json_delete(ud->topics);

#ifdef WITH_LMDB
	if (ud->t2t)
		gcache_close(ud->t2t);
# ifdef WITH_LUA
	if (ud->luadb)
		gcache_close(ud->luadb);
# endif
#endif

	free(ud->label);

#ifdef WITH_HTTP
	mg_destroy_server(&udata.mgserver);
#endif

#if WITH_LUA && WITH_LMDB
	hooks_exit(ud->luadata, "recorder stops");
#endif
	mosquitto_disconnect(mosq);

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

	return (0);
}
