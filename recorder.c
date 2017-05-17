/*
 * OwnTracks Recorder
 * Copyright (C) 2015-2016 Jan-Piet Mens <jpmens@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#if WITH_MQTT
# include <mosquitto.h>
#endif
#include <getopt.h>
#include <time.h>
#include <math.h>
#include "json.h"
#include <sys/utsname.h>
#include <regex.h>
#include "recorder.h"
#include "udata.h"
#include "utstring.h"
#include "geo.h"
#include "geohash.h"
#include "base64.h"
#include "misc.h"
#include "util.h"
#include "storage.h"
#include "fences.h"
#include "gcache.h"
#ifdef WITH_HTTP
# include "http.h"
#endif
#ifdef WITH_LUA
# include "hooks.h"
#endif
#if WITH_ENCRYPT
# include <sodium.h>
#endif
#include "version.h"


#define SSL_VERIFY_PEER (1)
#define SSL_VERIFY_NONE (0)

#define TOPIC_PARTS     (4)             /* owntracks/user/device/info */
#define DEFAULT_QOS	(2)
#define CLEAN_SESSION	false
#define GWNUMBERSMAX	50		/* number of batt,ext,status in array */

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
			json_delete(m);
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
	size_t imglen;

	utstring_renew(name);
	utstring_renew(face);

	/* I know the payload is valid JSON: write card */

	if ((fp = pathn("wb", "cards", username, NULL, "json", time(0))) != NULL) {
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
	if ((img = base64_decode(UB(face), &imglen)) != NULL) {
		if ((fp = pathn("wb", "photos", username, NULL, "png", time(0))) != NULL) {
			fwrite(img, sizeof(char), imglen, fp);
			fclose(fp);
		}
		free(img);
	}

	return (rc);
}

#ifdef WITH_MQTT
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
#endif /* WITH_MQTT */

/*
 * Quickly check wheterh the payload looks like
 * Greenwich CSV with a regex. We could use this
 * to split out the fields, instead of reverting
 * to sscanf
 */

//                TID          , TST           , T         , LAT        , LON        , COG        , VEL        , ALT        , DIST       , TRIP
#define CSV_RE "^([[:alnum:]]+),([[:xdigit:]]+),[[:alnum:]],-?[[:digit:]]+,-?[[:digit:]]+,[[:digit:]]+,[[:digit:]]+,[[:digit:]]+,[[:digit:]]+,[[:digit:]]+$"

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
 * we shouldn't for this particular user/device combo. Use the epoch
 * time to construct path name and "key"
 */

static void putrec(struct udata *ud, time_t epoch, UT_string *reltopic, UT_string *username, UT_string *device, char *string)
{
	FILE *fp;
	int rc = 0;

	if (ud->norec)
		return;

#ifdef WITH_LUA
	rc = hooks_norec(ud, UB(username), UB(device), string);
#endif

	if (rc == 0) {
		if ((fp = pathn("a", "rec", username, device, "rec", epoch)) == NULL) {
			olog(LOG_ERR, "Cannot write REC for %s/%s: %m",
				UB(username), UB(device));
			return;
		}

		fprintf(fp, RECFORMAT, isotime(epoch),
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

static void xx_dump(struct udata *ud, UT_string *username, UT_string *device, char *payloadstring, char *type, char *extension)
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

	utstring_printf(ts, "/%s-%s.%s", UB(username), UB(device), extension);
	if (ud->verbose) {
		printf("Received %s dump, storing at %s\n", type, UB(ts));
	}
	safewrite(UB(ts), (pretty_js) ? pretty_js : payloadstring);
	if (pretty_js) free(pretty_js);
}

/* Dump a config payload; get the 'configuration' element out of the dumped payloadstring */
void config_dump(struct udata *ud, UT_string *username, UT_string *device, char *payloadstring)
{
	JsonNode *json = json_decode(payloadstring), *config;

	if (json == NULL)
		return;

	if ((config = json_find_member(json, "configuration")) != NULL) {
		char *js_string = json_stringify(config, NULL);

		if (js_string) {
			xx_dump(ud, username, device, js_string, "config", "otrc");
			json_delete(json);
			free(js_string);
		}
	}
}

/* Dump a waypoints (plural) payload */
void waypoints_dump(struct udata *ud, UT_string *username, UT_string *device, char *payloadstring)
{
	JsonNode *json = json_decode(payloadstring), *j;
	char *js = NULL;

	if (json == NULL)
		return;

	if ((j = json_find_member(json, "r")) != NULL) {
		json_remove_from_parent(j);
		json_delete(j);
		js = json_stringify(json, NULL);
		json_delete(json);
	}

	xx_dump(ud, username, device, (js) ? js : payloadstring, "waypoints", "otrw");
	load_otrw_from_string(ud, UB(username), UB(device), (js) ? js : payloadstring);

	if (js)
		free(js);
}

#ifdef WITH_GREENWICH

/*
 * key is "batt", "ext", or "status"
 * value is a string which contains a number
 *
 * Open/create a file at gw/user/device/user-device.json. Append to the existing array,
 * limiting the number of array entries.
 */

void store_gwvalue(char *username, char *device, time_t tst, char *key, char *value)
{
	static UT_string *ts = NULL, *u = NULL, *d = NULL;
	JsonNode *array, *o, *j;
	int count = 0;
	char *js;

	utstring_renew(ts);
	utstring_renew(u);
	utstring_renew(d);
	utstring_printf(u, "%s", username);
	utstring_printf(d, "%s", device);
	lowercase(UB(u));
	lowercase(UB(d));
	utstring_printf(ts, "%s/last/%s/%s",
				STORAGEDIR,
				UB(u),
				UB(d));
	if (mkpath(UB(ts)) < 0) {
		olog(LOG_ERR, "Cannot mkdir %s: %m", UB(ts));
		return;
	}

	utstring_printf(ts, "/%s.json", key);

	/* Read file into array or create array on error */
	if ((js = slurp_file(UB(ts), TRUE)) != NULL) {
		if ((array = json_decode(js)) == NULL) {
			array = json_mkarray();
		}
		free(js);
	} else {
		array = json_mkarray();
	}


	/* Count elements in array and pop first if too long */
	json_foreach(j, array) {
		++count;
	}
	if (count >= GWNUMBERSMAX) {
		j = json_first_child(array);
		json_delete(j);
	}

	o = json_mkobject();

	json_append_member(o, "tst", json_mknumber(tst));
	json_append_member(o, key, json_mknumber(atof(value)));

	json_append_element(array, o);

	if ((js = json_stringify(array, NULL)) != NULL) {
		safewrite(UB(ts), js);
		free(js);
	}

	json_delete(array);
}
#endif /* GREENWICH */

#if WITH_ENCRYPT
/*
 * Decrypt the payload and return a pointer to allocated space containing
 * the clear text.
 * p64 contains the base64-encoded payload from the device. `username'
 * and `device' are needed to obtain the decryption key for this object.
 */

unsigned char *decrypt(struct udata *ud, char *topic, char *p64, char *username, char *device)
{
	unsigned char key[crypto_secretbox_KEYBYTES];
	unsigned char *ciphertext, *cleartext;
	size_t ciphertext_len;
	int n, klen;
	static UT_string *userdev = NULL;

	utstring_renew(userdev);
	utstring_printf(userdev, "%s-%s", username, device);

	lowercase(UB(userdev));
	for (n = 0; n < strlen(UB(userdev)); n++) {
		if (UB(userdev)[n] == ' ')
			UB(userdev)[n] = '-';
	}

	memset(key, 0, sizeof(key));
	klen = gcache_get(ud->keydb, (char *)UB(userdev), (char *)key, sizeof(key));
	if (klen < 1) {
		olog(LOG_ERR, "no decryption key for %s in %s", UB(userdev), topic);
		return (NULL);
	}

	debug(ud, "Key for %s is [%s]", UB(userdev), key);

	n = strlen(p64);		/* This is more than enough */

	if ((ciphertext = base64_decode(p64, &ciphertext_len)) == NULL) {
		olog(LOG_ERR, "payload of %s cannot be base64-decoded", topic);
		return (NULL);
	}

	debug(ud, "START DECRYPT. clen==%lu", ciphertext_len);

	if ((cleartext = calloc(n, sizeof(unsigned char))) == NULL) {
		free(ciphertext);
		return (NULL);
	}

	if (crypto_secretbox_open_easy(cleartext,			// message
			ciphertext + crypto_secretbox_NONCEBYTES,	// skip over nonce
			ciphertext_len - crypto_secretbox_NONCEBYTES,	// len (- nonce)
			ciphertext,					// nonce
			key) != 0)
	{
		olog(LOG_ERR, "payload of %s cannot be decrypted; forged?", topic);
		free(ciphertext);
		free(cleartext);
		return (NULL);
	}

	debug(ud, "DECRYPTED: %s", (char *)cleartext);
	free(ciphertext);

	return (cleartext);
}
#endif /* ENCRYPT */

void handle_message(void *userdata, char *topic, char *payload, size_t payloadlen, int retain, int httpmode, int was_encrypted)
{
	JsonNode *json, *j, *geo = NULL;
	char *tid = NULL, *t = NULL, *p;
	double lat, lon, acc;
	long tst;
	struct udata *ud = (struct udata *)userdata;
        char *topics[42];
        int count = 0, cached;
	static UT_string *basetopic = NULL, *username = NULL, *device = NULL, *addr = NULL, *cc = NULL, *ghash = NULL, *ts = NULL;
	static UT_string *reltopic = NULL, *filename = NULL;
	char *jsonstring, *_typestr = NULL;
	time_t now, epoch;
	int pingping = FALSE, skipslash = 0, geoprec = geohash_prec();
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
	monitorhook(ud, now, topic);

	chomp(payload);

	debug(ud, "%s (plen=%d, r=%d) [%s]", topic, payloadlen, retain, payload);
	if (payloadlen == 0) {
		return;
	}

	if (retain == TRUE && ud->ignoreretained) {
		return;
	}

	// printf("%s %s\n", m->topic, bindump(m->payload, m->payloadlen)); fflush(stdout);


	utstring_renew(ts);
	utstring_renew(basetopic);
	utstring_renew(username);
	utstring_renew(device);

        if ((count = splitter(topic, "/", topics)) == -1) {
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
		fprintf(stderr, "Ignoring short topic %s\n", topic);
		splitterfree(topics);
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
			utstring_printf(reltopic, "%s%c", topics[j], (j < count - 1) ? '/' : 0);
		}
        } else {
		utstring_printf(reltopic, "*");
	}
	if (utstring_len(reltopic) == 0)
		utstring_printf(reltopic, "-");


	utstring_printf(basetopic, "%s/%s/%s", topics[0 + skipslash], topics[1 + skipslash], topics[2 + skipslash]);
	utstring_printf(username, "%s", topics[1 + skipslash]);
	utstring_printf(device, "%s", topics[2 + skipslash]);


#ifdef WITH_PING
	if (!strcmp(UB(username), "ping") && !strcmp(UB(device), "ping")) {
		pingping = TRUE;
	}
#endif

#ifdef WITH_GREENWICH
	/*
	 * For Greenwich: handle owntracks/user/device/voltage/batt, voltage/ext, and
	 * status all of which have a numeric payload.
	 */

	if ((count == 5+skipslash && !strcmp(topics[3+skipslash], "voltage")) &&
		(!strcmp(topics[4+skipslash], "batt") || !strcmp(topics[4+skipslash], "ext"))) {
		store_gwvalue(UB(username), UB(device), now, topics[4+skipslash], payload);
	}

	if (count == 4+skipslash && !strcmp(topics[3+skipslash], "status")) {
		store_gwvalue(UB(username), UB(device), now, "status", payload);
	}

	/* Fall through to store this payload in the REC file as well. */

#endif

	splitterfree(topics);

	/*
	 * Now let's see if this contains some sort of valid JSON
	 * or an OwnTracks CSV. If it doesn't, just store this payload because
	 * there's nothing left for us to do with it.
	 */

	if ((json = json_decode(payload)) == NULL) {
		if ((json = csv_to_json(payload)) == NULL) {
			/* It's not JSON or it's not a location CSV; store it using
			 * now as time -- we have no other */
			putrec(ud, now, reltopic, username, device, bindump(payload, payloadlen));
			return;
		}
	}

	if (ud->skipdemo && (json_find_member(json, "_demo") != NULL)) {
		json_delete(json);
		return;
	}

	_type = T_UNKNOWN;
	if ((j = json_find_member(json, "_type")) != NULL) {
		if (j->tag == JSON_STRING) {
			_typestr = strdup(j->string_);
			if (!strcmp(j->string_, "location"))		_type = T_LOCATION;
			else if (!strcmp(j->string_, "beacon"))		_type = T_BEACON;
			else if (!strcmp(j->string_, "card"))		_type = T_CARD;
			else if (!strcmp(j->string_, "cmd"))		_type = T_CMD;
			else if (!strcmp(j->string_, "lwt"))		_type = T_LWT;
			else if (!strcmp(j->string_, "steps"))		_type = T_STEPS;
			else if (!strcmp(j->string_, "transition"))	_type = T_TRANSITION;
			else if (!strcmp(j->string_, "waypoint"))	_type = T_WAYPOINT;
			else if (!strcmp(j->string_, "waypoints"))	_type = T_WAYPOINTS;
			else if (!strcmp(j->string_, "dump"))		_type = T_CONFIG;
#if WITH_ENCRYPT
			else if (!strcmp(j->string_, "encrypted"))	_type = T_ENCRYPTED;
#endif /* WITH_ENCRYPT */
		}
	}

	switch (_type) {
		case T_CARD:
			do_info(ud, username, device, json);
			goto cleanup;
		case T_BEACON:
#ifdef WITH_HTTP
			if (ud->mgserver && !pingping) {
				json_append_member(json, "topic", json_mkstring(topic));
				json_append_member(json, "username", json_mkstring(UB(username)));
				json_append_member(json, "device", json_mkstring(UB(device)));
				http_ws_push_json(ud->mgserver, json);
			}
#endif
			if (r_ok) {
				putrec(ud, now, reltopic, username, device, bindump(payload, payloadlen));
			}
			goto cleanup;
		case T_LWT:
			/*
			 * LWT gets a pseudo-reltopic called 'lwt'; reason: if we keep the
			 * empty original reltopic, it would become '*' in the .rec file
			 * which confuses the recorder when serving location data.
			 * Also, this appears to be sensible, and it probably should have
			 * been that way all along in the apps.
			 */

			utstring_clear(reltopic);
			utstring_printf(reltopic, "lwt");

			/* Fall through */

		case T_CMD:
		case T_STEPS:
			if (r_ok) {
				putrec(ud, now, reltopic, username, device, bindump(payload, payloadlen));
			}
			goto cleanup;
		case T_WAYPOINTS:
			waypoints_dump(ud, username, device, payload);
			goto cleanup;
		case T_CONFIG:
			config_dump(ud, username, device, payload);
			goto cleanup;
		case T_WAYPOINT:
		case T_TRANSITION:
		case T_LOCATION:
			break;
#if WITH_ENCRYPT
		case T_ENCRYPTED:
			/*
			 * Obtain the `data' element from JSON, and try and decrypt
			 * that. If successful, we use the decrypted message as
			 * payload, and invoke this function again to do the
			 * heavy lifting.
			 */

			if ((j = json_find_member(json, "data")) != NULL) {
				if (j->tag == JSON_STRING) {
					char *cleartext;

					cleartext = (char *)decrypt(ud, topic, j->string_, UB(username), UB(device));
					if (cleartext != NULL) {
						handle_message(ud, topic, cleartext, strlen(cleartext), retain, httpmode, TRUE);
						free(cleartext);
					}
					if (_typestr) free(_typestr);
					json_delete(json);
					return;
				}
			}
			olog(LOG_ERR, "no `data' in encrypted %s", topic);
			json_delete(json);
			return;
			break;
#endif /* WITH_ENCRYPT */
		default:
			if (r_ok) {
				putrec(ud, now, reltopic, username, device, bindump(payload, payloadlen));
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
		if (j->tag == JSON_STRING) {
			tst = strtoul(j->string_, NULL, 10);
			json_remove_from_parent(j);
			json_delete(j);
			json_append_member(json, "tst", json_mknumber(tst));
		} else {
			tst = (unsigned long)j->number_;
		}
	}

	lat = number(json, "lat");
	lon = number(json, "lon");
	if (isnan(lat) || isnan(lon)) {
		olog(LOG_ERR, "lat or lon for %s are NaN: %s", topic, bindump(payload, payloadlen));
		goto cleanup;
	}

/*
	if (impossible_rule(ud, UB(username), UB(device), tst, lat, lon) == TRUE) {
		goto cleanup;
	}
*/

	if ((j = json_find_member(json, "acc")) != NULL) {
		if (j->tag == JSON_STRING) {
			acc = atof(j->string_);
			json_remove_from_parent(j);
			json_delete(j);
			json_append_member(json, "acc", json_mknumber(acc));
		}
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

	if ((j = json_find_member(json, "_geoprec")) != NULL) {
		if (j->tag == JSON_STRING) {
			geoprec = atoi(j->string_);
		} else {
			geoprec = j->number_;
		}
	}


	/*
	 * If the topic we are handling is in topic2tid, replace the TID
	 * in this payload with that from the database.
	 */

	if (ud->t2t) {
		char newtid[BUFSIZ];
		long blen;

		if ((blen = gcache_get(ud->t2t, topic, newtid, sizeof(newtid))) > 0) {
			if ((j = json_find_member(json, "tid")) != NULL) {
				json_remove_from_parent(j);
				json_delete(j);
			}
			json_append_member(json, "tid", json_mkstring(newtid));
		}
	}

	/*
	 * Chances are high that what we have now contains lat, lon. Attempt to
	 * perform or retrieve reverse-geo.
	 */

	utstring_renew(ghash);
	utstring_renew(addr);
	utstring_renew(cc);
        p = geohash_encode(lat, lon, abs(geoprec));
	if (p != NULL) {
		utstring_printf(ghash, "%s", p);
		free(p);
	}


	cached = FALSE;
	if (ud->revgeo == TRUE) {
		if ((geo = hook_revgeo(ud, topic, UB(username), UB(device), lat, lon)) != NULL) {
			if ((j = json_find_member(geo, "_rec")) != NULL) {
				if (j->bool_ == true) {
					json_remove_from_parent(j);
					json_copy_to_object(json, geo, false);
					geo = NULL;	/* Reset so it's not copied again later */
				}
			}
		} else {
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
				if (geoprec > 0) {
					if ((geo = revgeo(ud, lat, lon, addr, cc)) != NULL) {
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
			}
		}
	} else {
		utstring_printf(cc, "??");
		utstring_printf(addr, "n.a.");
	}


	if (httpmode) {
		json_append_member(json, "_http", json_mkbool(1));
	}

	if (was_encrypted) {
		json_append_member(json, "_decrypted", json_mkbool(1));
	}



	/*
	 * We have normalized data in the JSON, so we can now write it
	 * out to the REC file.
	 */

	if (!pingping) {
		if ((jsonstring = json_stringify(json, NULL)) != NULL) {
			double d_epoch = number(json, "tst");

			epoch = (isnan(d_epoch)) ? now : d_epoch;
			putrec(ud, epoch, reltopic, username, device, jsonstring);
			free(jsonstring);
		}
	}

	/*
	 * Append a few bits to the location type to add to LAST and
	 * for Lua / Websockets.
	 * I need a unique "key" in the Websocket clients to keep track
	 * of which device is being updated; use topic.
	 */

#if WITH_GREENWICH
	if (strcmp(UB(reltopic), "alarm") == 0) {
		json_append_member(json, "topic", json_mkstring(UB(basetopic)));
		json_append_member(json, "_reltopic", json_mkstring("alarm"));
	} else {
		json_append_member(json, "topic", json_mkstring(topic));
	}
#else
	json_append_member(json, "topic", json_mkstring(topic));
#endif

	/*
	 * We have to know which user/device this is for in order to
	 * determine whether a connected Websocket client is authorized
	 * to see this. Add user/device
	 */

	json_append_member(json, "username", json_mkstring(UB(username)));
	json_append_member(json, "device", json_mkstring(UB(device)));

	json_append_member(json, "ghash",    json_mkstring(UB(ghash)));

	if (_type == T_LOCATION || _type == T_WAYPOINT) {
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
	if (ud->luadata && !pingping) {
		hooks_hook(ud, topic, json);
	}
#endif

	if (ud->verbose) {
		if (_type == T_LOCATION) {
			printf("%c %s %-35s t=%-1.1s tid=%-2.2s loc=%.5f,%.5f [%s] %s (%s)\n",
				(cached) ? '*' : '-',
				ltime(tst),
				topic,
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

	check_fences(ud, UB(username), UB(device), lat, lon, json, topic);
	check_fences(ud, "_", "_", lat, lon, json, topic);

    cleanup:
	if (geo)	json_delete(geo);
	if (json)	json_delete(json);
	if (tid)	free(tid);
	if (t)		free(t);
	if (_typestr)	free(_typestr);
}

#ifdef WITH_MQTT

void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *m)
{
	struct udata *ud = (struct udata *)userdata;

	handle_message(ud, m->topic, m->payload, m->payloadlen, m->retain, FALSE, FALSE);
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
		"Connection refused: code=0x04",			/* 0x04 */
		"Connection refused: bad username or password",		/* 0x05 */
		"Connection refused: not authorized",			/* 0x06 */
		"Connection refused: TLS error",			/* 0x07 */
	};

	return ((rc >= 0 && rc <= 0x07) ? reasons[rc] : "unknown reason");
}


void on_disconnect(struct mosquitto *mosq, void *userdata, int reason)
{
	// struct udata *ud = (struct udata *)userdata;

	if (reason == 0) { 	// client wish
		;
	} else {
		olog(LOG_INFO, "Disconnected. Reason: 0x%X [%s]", reason, mosquitto_reason(reason));
	}
}

#endif /* WITH_MQTT */

static void catcher(int sig)
{
        fprintf(stderr, "Going down on signal %d\n", sig);
	run = 0;
}

void usage(char *prog)
{
	printf("Usage: %s [options..] ", prog);
#ifdef WITH_MQTT
	printf("topic [topic ...]\n");
#else
	printf("\n");
#endif
	printf("  --help		-h	this message\n");
	printf("  --storage		-S     storage dir (%s)\n", STORAGEDEFAULT);
	printf("  --norevgeo		-G     disable ghash to reverge-geo lookups\n");
	printf("  --noskipdemo 		-D     do handle objects with _demo (default: don't)\n");
#if WITH_MQTT
	printf("  --useretained		-R     process retained messages (default: no)\n");
	printf("  --clientid		-i     MQTT client-ID\n");
	printf("  --qos			-q     MQTT QoS (dflt: 2)\n");
	printf("  --pubprefix		-P     republish prefix (dflt: no republish)\n");
	printf("  --host		-H     MQTT host (localhost)\n");
	printf("  --port		-p     MQTT port (1883)\n");
	printf("  --psk                        PSK hint\n");
	printf("  --identity                   PSK identity\n");
#endif
	printf("  --logfacility		       syslog facility (local0)\n");
	printf("  --quiet		       disable printing of messages to stdout\n");
	printf("  --initialize		       initialize storage\n");
	printf("  --label <label>	       server label (dflt: OwnTracks)\n");
#ifdef WITH_HTTP
	printf("  --http-host <host>	       HTTP addr to bind to (localhost)\n");
	printf("  --http-port <port>	-A     HTTP port (8083); 0 to disable HTTP\n");
	printf("  --doc-root <directory>       document root (%s)\n", DOCROOT);
	printf("  --http-logdir <directory>    directory in which to store access.log\n");
	printf("  --browser-apikey <key>       Google maps browser API key\n");
#endif
#ifdef WITH_LUA
	printf("  --lua-script <script.lua>    path to Lua script. If unset, no Lua hooks\n");
#endif
	printf("  --precision		       ghash precision (dflt: %d)\n", GHASHPREC);
	printf("  --norec		       don't maintain REC files\n");
	printf("  --geokey		       optional Google reverse-geo API key\n");
	printf("  --debug  		       additional debugging\n");
	printf("\n");
	printf("Options override these environment variables:\n");
	printf("  $OTR_STORAGEDIR\n");
	printf("  $OTR_BROWSERAPIKEY\n");
#ifdef WITH_MQTT
	printf("  $OTR_HOST		MQTT hostname\n");
	printf("  $OTR_PORT		MQTT port\n");
	printf("  $OTR_USER\n");
	printf("  $OTR_PASS\n");
	printf("  $OTR_CAFILE		PEM CA certificate chain\n");
#endif

	exit(1);
}


int main(int argc, char **argv)
{
#if WITH_MQTT
	struct mosquitto *mosq = NULL;
	UT_string *clientid;
	int rc, i;
	struct utsname uts;
#endif /* WITH_MQTT */
	char err[1024], *p;
	char *logfacility = "local0";
#if WITH_MQTT
	int loop_timeout = 1000;
#endif
	int ch, initialize = FALSE;
	static struct udata udata, *ud = &udata;
#ifdef WITH_HTTP
	char *doc_root		= DOCROOT;
	int http_pollms		= 50;
#endif
	char *progname = *argv;

#if WITH_MQTT
	udata.mosq		= NULL;
	udata.qos		= DEFAULT_QOS;
	udata.pubprefix		= NULL;
	udata.username		= NULL;
	udata.password		= NULL;
	udata.hostname		= strdup("localhost");
	udata.port		= 1883;
	udata.clientid		= NULL;
	udata.topics		= NULL;
	udata.cafile		= NULL;
	udata.capath		= NULL;
	udata.certfile		= NULL;
	udata.keyfile		= NULL;
	udata.psk		= NULL;
	udata.identity		= NULL;
#endif
	udata.ignoreretained	= TRUE;
	udata.skipdemo		= TRUE;
	udata.revgeo		= TRUE;
	udata.verbose		= TRUE;
	udata.norec		= FALSE;
	udata.gc		= NULL;
	udata.t2t		= NULL;		/* Topic to TID */
#ifdef WITH_HTTP
	udata.mgserver		= NULL;
	udata.http_host		= strdup("localhost");
	udata.http_port		= 8083;
	udata.http_logdir	= NULL;
	udata.browser_apikey	= NULL;
#endif
#ifdef WITH_LUA
	udata.luascript		= NULL;
	udata.luadata		= NULL;
	udata.luadb		= NULL;
#endif /* WITH_LUA */
	udata.label		= strdup("OwnTracks");
	udata.geokey		= NULL;		/* default: no API key */
	udata.debug		= FALSE;

	openlog("ot-recorder", LOG_PID | LOG_PERROR, syslog_facility_code(logfacility));

#if WITH_MQTT
	utstring_new(clientid);
	utstring_printf(clientid, "ot-recorder");
	if (uname(&uts) == 0) {
		utstring_printf(clientid, "-%s", uts.nodename);
	}
	utstring_printf(clientid, "-%d", getpid());

	ud->clientid = strdup(UB(clientid));
#endif /* WITH_MQTT */

	get_defaults(CONFIGFILE, &udata);


#if WITH_MQTT
	if ((p = getenv("OTR_HOST")) != NULL) {
		ud->hostname = strdup(p);
	}

	if ((p = getenv("OTR_PORT")) != NULL) {
		ud->port = atoi(p);
	}
#endif

	if ((p = getenv("OTR_STORAGEDIR")) != NULL) {
		strcpy(STORAGEDIR, p);
	}

#if WITH_MQTT
	if ((p = getenv("OTR_USER")) != NULL) {
		if (ud->username)
			free(ud->username);
		ud->username = strdup(p);
	}
	if ((p = getenv("OTR_PASS")) != NULL) {
		if (ud->password)
			free(ud->password);
		ud->password = strdup(p);
	}

	if ((p = getenv("OTR_CAFILE")) != NULL) {
		if (ud->cafile)
			free(ud->cafile);
		ud->cafile = strdup(p);
	}

	if ((p = getenv("OTR_CAPATH")) != NULL) {
		if (ud->capath)
			free(ud->capath);
		ud->capath = strdup(p);
	}

	if ((p = getenv("OTR_CERTFILE")) != NULL) {
		if (ud->certfile)
			free(ud->certfile);
		ud->certfile = strdup(p);
	}

	if ((p = getenv("OTR_KEYFILE")) != NULL) {
		if (ud->keyfile)
			free(ud->keyfile);
		ud->keyfile = strdup(p);
	}

#endif
	if ((p = getenv("OTR_BROWSERAPIKEY")) != NULL) {
		if (ud->browser_apikey)
			free(ud->browser_apikey);
		ud->browser_apikey = strdup(p);
	}

	while (1) {
		static struct option long_options[] = {
			{ "help",	no_argument,		0, 	'h'},
			{ "skipdemo",	no_argument,		0, 	'D'},
			{ "norevgeo",	no_argument,		0, 	'G'},
#if WITH_MQTT
			{ "useretained",	no_argument,		0, 	'R'},
			{ "clientid",	required_argument,	0, 	'i'},
			{ "pubprefix",	required_argument,	0, 	'P'},
			{ "qos",	required_argument,	0, 	'q'},
			{ "host",	required_argument,	0, 	'H'},
			{ "port",	required_argument,	0, 	'p'},
			{ "psk",	required_argument,	0, 	20},
			{ "identity",	required_argument,	0, 	21},
#endif /* !MQTT */
			{ "storage",	required_argument,	0, 	'S'},
			{ "logfacility",	required_argument,	0, 	4},
			{ "precision",	required_argument,	0, 	5},
			{ "quiet",	no_argument,		0, 	8},
			{ "initialize",	no_argument,		0, 	9},
			{ "label",	required_argument,	0, 	10},
			{ "norec",	no_argument,		0, 	11},
			{ "geokey",	required_argument,	0, 	12},
			{ "debug",	no_argument,		0, 	13},
#ifdef WITH_LUA
			{ "lua-script",	required_argument,	0, 	7},
#endif
#ifdef WITH_HTTP
			{ "http-host",	required_argument,	0, 	3},
			{ "http-port",	required_argument,	0, 	'A'},
			{ "doc-root",	required_argument,	0, 	2},
			{ "http-logdir",	required_argument,	0, 	14},
			{ "browser-apikey",	required_argument,	0, 	15},
#endif
			{0, 0, 0, 0}
		  };
		int optindex = 0;

		ch = getopt_long(argc, argv, "hDGRi:P:q:S:H:p:A:", long_options, &optindex);
		if (ch == -1)
			break;

		switch (ch) {
			case 13:
				udata.debug = TRUE;
				break;
			case 12:
				if (udata.geokey) free(udata.geokey);
				udata.geokey = strdup(optarg);
				break;
			case 11:
				udata.norec = TRUE;
				break;
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
				if (ud->luascript) free(ud->luascript);
				ud->luascript = strdup(optarg);
				break;
#endif
#ifdef WITH_MQTT
			case 'i':
				utstring_clear(clientid);
				utstring_printf(clientid, "%s", optarg);
				free(ud->clientid);
				ud->clientid = strdup(UB(clientid));
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
				free(ud->hostname);
				ud->hostname = strdup(optarg);
				break;
			case 'p':
				ud->port = atoi(optarg);
				break;
#endif /* WITH_MQTT */
			case 5:
				geohash_setprec(atoi(optarg));
				break;
			case 4:
				logfacility = strdup(optarg);
				break;
#ifdef WITH_HTTP
			case 'A':	/* API */
				ud->http_port = atoi(optarg);
				break;
			case 2:		/* no short char */
				doc_root = strdup(optarg);
				break;
			case 3:		/* no short char */
				free(ud->http_host);
				ud->http_host = strdup(optarg);
				break;
			case 14:
				if (ud->http_logdir) free(ud->http_logdir);
				ud->http_logdir = strdup(optarg);
				break;
			case 15:
				if (ud->browser_apikey) free(ud->browser_apikey);
				ud->browser_apikey = strdup(optarg);
				break;

			case 20:
				ud->psk = strdup(optarg);
				break;
			case 21:
				ud->identity = strdup(optarg);
				break;
#endif
			case 'D':
				ud->skipdemo = FALSE;
				break;
			case 'G':
				ud->revgeo = FALSE;
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
		struct gcache *gt;

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
#ifdef WITH_ENCRYPT
		if ((gt = gcache_open(path, "keys", FALSE)) == NULL) {
			fprintf(stderr, "Cannot lmdb-open `keys'\n");
			exit(2);
		}
		gcache_close(gt);
#endif /* !ENCRYPT */
		if ((gt = gcache_open(path, "friends", FALSE)) == NULL) {
			fprintf(stderr, "Cannot lmdb-open `friends'\n");
			exit(2);
		}
		gcache_close(gt);

		if ((gt = gcache_open(path, "wp", FALSE)) == NULL) {
			fprintf(stderr, "Cannot lmdb-open `wp'\n");
			exit(2);
		}
		gcache_close(gt);
		exit(0);
	}

	argc -= (optind);
	argv += (optind);

#ifdef WITH_MQTT
	if (ud->port) {
		if (ud->topics == NULL && argc < 1) {	/* no topics set via config file */
			usage(progname);
			return (-1);
		}

		if (argc >= 1) {			/* topics on command line override config */

			/*
			 * Push list of topics into the array so that we can
			 * (re)subscribe in on_connect()
			 */

			if (ud->topics != NULL) {
				json_delete(ud->topics);
				ud->topics = NULL;
			}

			ud->topics = json_mkarray();

			for (i = 0; i < argc; i++) {
				json_append_element(ud->topics, json_mkstring(argv[i]));
			}
		}
	}
#endif

#ifdef WITH_HTTP
	if (ud->http_port) {
		if (!is_directory(doc_root)) {
			olog(LOG_ERR, "%s is not a directory", doc_root);
			exit(1);
		}
		/* First arg is user data which I can grab via conn->server_param  */
		udata.mgserver = mg_create_server(ud, ev_handler);
	}
#endif
	olog(LOG_DEBUG, "version %s starting with STORAGEDIR=%s", VERSION, STORAGEDIR);

	if (ud->revgeo == TRUE) {
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
		revgeo_init();
	}

	snprintf(err, sizeof(err), "%s/ghash", STORAGEDIR);
	ud->t2t = gcache_open(err, "topic2tid", TRUE);
# ifdef WITH_LUA
	ud->luadb = gcache_open(err, "luadb", FALSE);
# endif
# ifdef WITH_ENCRYPT
	ud->keydb = gcache_open(err, "keys", TRUE);
# endif
	ud->httpfriends = gcache_open(err, "friends", TRUE);
	ud->wpdb = gcache_open(err, "wp", FALSE);

	load_fences(ud);

#if WITH_ENCRYPT
	if (sodium_init() == -1) {
		olog(LOG_ERR, "cannot initialize libsodium");
	}
#endif

	signal(SIGINT, catcher);
	signal(SIGTERM, catcher);
	signal(SIGPIPE, SIG_IGN);

#ifdef WITH_MQTT
	mosquitto_lib_init();

	if (ud->port) {
		mosq = mosquitto_new(ud->clientid, CLEAN_SESSION, (void *)&udata);
		if (!mosq) {
			fprintf(stderr, "Error: Out of memory.\n");
			mosquitto_lib_cleanup();
			return 1;
		}


		mosquitto_reconnect_delay_set(mosq,
			2, 	/* delay */
			20,	/* delay_max */
			0);	/* exponential backoff */

		mosquitto_message_callback_set(mosq, on_message);
		mosquitto_connect_callback_set(mosq, on_connect);
		mosquitto_disconnect_callback_set(mosq, on_disconnect);

		if (ud->username && ud->password) {
			mosquitto_username_pw_set(mosq, ud->username, ud->password);
		}

		if (ud->psk && ud->cafile) {
			olog(LOG_ERR, "Configuring TLS together with PSK is an error");
			exit(2);
		}

		if (ud->psk && *ud->psk && ud->identity && *ud->identity) {
			rc = mosquitto_tls_psk_set(mosq,
				ud->psk,
				ud->identity,
				NULL);			/* Ciphers */
		}

		if (ud->cafile && *ud->cafile) {

			if (access(ud->cafile, R_OK) != 0) {
				olog(LOG_ERR, "cafile configured as `%s' can't be opened: %m", ud->cafile);
				exit(2);
			}

			rc = mosquitto_tls_set(mosq,
				ud->cafile,		/* cafile */
				ud->capath,		/* capath */
				ud->certfile,		/* certfile */
				ud->keyfile,		/* keyfile */
				NULL			/* pw_callback() */
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

		olog(LOG_INFO, "connecting to MQTT on %s:%d as clientID %s %s %s",
			ud->hostname, ud->port,
			ud->clientid,
			((ud->cafile && *ud->cafile) || (ud->psk && *ud->psk)) ? "with" : "without",
			(ud->psk && *ud->identity) ? "PSK" : "TLS");

		rc = mosquitto_connect(mosq, ud->hostname, ud->port, 60);
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
		/* Explicitly set MQTT connection for Lua's otr_publish() */
		ud->mosq = mosq;
	} else {
		olog(LOG_INFO, "Not using MQTT: disabled by port=0");
		ud->mosq = NULL;
	}
#endif /* WITH_MQTT */

#if WITH_LUA
	/*
	 * If option for lua-script has not been given, ignore all hooks.
	 */

	if (ud->luascript) {
		if ((udata.luadata = hooks_init(ud, ud->luascript)) == NULL) {
			olog(LOG_ERR, "Stopping because loading of Lua script %s failed", ud->luascript);
			exit(1);
		}
	}
#endif

#ifdef WITH_HTTP
	if (ud->http_port) {
		char address[BUFSIZ], logdir[BUFSIZ];
		const char *addressinfo;

		sprintf(address, "%s:%d", ud->http_host, ud->http_port);

		mg_set_option(udata.mgserver, "listening_port", address);
		// mg_set_option(udata.mgserver, "listening_port", "8090,ssl://8091:cert.pem");

		// mg_set_option(udata.mgserver, "ssl_certificate", "cert.pem");
		// mg_set_option(udata.mgserver, "listening_port", "8091");

		mg_set_option(udata.mgserver, "document_root", doc_root);
		mg_set_option(udata.mgserver, "enable_directory_listing", "yes");
		mg_set_option(udata.mgserver, "auth_domain", "owntracks-recorder");

		if (ud->http_logdir) {
			sprintf(logdir, "%s/access.log", ud->http_logdir);
			mg_set_option(udata.mgserver, "access_log_file", logdir);
			olog(LOG_INFO, "Using access log at %s", logdir);
		}
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
#ifdef WITH_MQTT
		if (ud->port != 0) {
#if WITH_HTTP
			if (ud->http_port != 0)
#endif /* WITH_HTTP */
				loop_timeout = 0; /* this belongs to above `if' */
			rc = mosquitto_loop(mosq, loop_timeout, /* max-packets */ 1);
			if (run && rc) {
				olog(LOG_INFO, "MQTT connection: rc=%d [%s] (errno=%d; %s). Sleeping...", rc, mosquitto_strerror(rc), errno, strerror(errno));
				sleep(10);
				mosquitto_reconnect(mosq);
			}
		} else {
			http_pollms = 10000;
		}
#endif
#ifdef WITH_HTTP
		if (udata.mgserver) {
			mg_poll_server(udata.mgserver, http_pollms);
		}
#endif
	}


	gcache_close(ud->gc);
	gcache_close(ud->t2t);
	gcache_close(ud->httpfriends);
	gcache_close(ud->wpdb);
#ifdef WITH_LUA
	if (ud->luadb)
		gcache_close(ud->luadb);
#endif
# ifdef WITH_ENCRYPT
	gcache_close(ud->keydb);
# endif

	free(ud->label);

#ifdef WITH_HTTP
	mg_destroy_server(&udata.mgserver);
	free(ud->http_host);
	free(ud->browser_apikey);
	if (ud->http_logdir) free(ud->http_logdir);
#endif

#if WITH_LUA
	hooks_exit(ud->luadata, "recorder stops");
#endif

	revgeo_free();

#ifdef WITH_MQTT
	if (ud->port) {
		mosquitto_disconnect(mosq);
		if (ud->topics)
			json_delete(ud->topics);
	}

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	free(ud->hostname);
	if (ud->clientid) free(ud->clientid);
	if (ud->cafile) free(ud->cafile);
	if (ud->capath) free(ud->capath);
	if (ud->certfile) free(ud->certfile);
	if (ud->keyfile) free(ud->keyfile);
#endif

	return (0);
}
