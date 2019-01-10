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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "recorder.h"
#include "json.h"
#include "util.h"
#include "misc.h"
#include "fences.h"
#include "gcache.h"
#include "storage.h"
#include "geohash.h"
#include "udata.h"
#include "version.h"
#ifdef WITH_HTTP
# include "http.h"
#endif
#if WITH_ENCRYPT
# include <sodium.h>
# include "base64.h"
#endif
#if WITH_LUA
# include "hooks.h"
#endif

#ifdef WITH_HTTP

#define MAXPARTS 40
#define VIEWSUBDIR	"views"

/* A transparent 40x40 PNG image with a black border */
static unsigned char border40x40png[] = {
	0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00,
	0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x28,
	0x00, 0x00, 0x00, 0x28, 0x08, 0x06, 0x00, 0x00, 0x00, 0x8C,
	0xFE, 0xB8, 0x6D, 0x00, 0x00, 0x00, 0x06, 0x62, 0x4B, 0x47,
	0x44, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0xA0, 0xBD, 0xA7,
	0x93, 0x00, 0x00, 0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00,
	0x00, 0x0B, 0x13, 0x00, 0x00, 0x0B, 0x13, 0x01, 0x00, 0x9A,
	0x9C, 0x18, 0x00, 0x00, 0x00, 0x07, 0x74, 0x49, 0x4D, 0x45,
	0x07, 0xDF, 0x0A, 0x15, 0x0C, 0x31, 0x28, 0xF8, 0x39, 0xED,
	0x7A, 0x00, 0x00, 0x00, 0x46, 0x49, 0x44, 0x41, 0x54, 0x58,
	0xC3, 0xED, 0xD8, 0x21, 0x0E, 0x00, 0x20, 0x0C, 0x04, 0xC1,
	0x2B, 0xE1, 0xFF, 0x5F, 0x06, 0x8B, 0x06, 0x53, 0x92, 0x59,
	0x55, 0x39, 0x39, 0xD9, 0x4A, 0xB2, 0xD2, 0xB8, 0x91, 0xE6,
	0xCD, 0xE3, 0xAE, 0x66, 0xB6, 0xF5, 0xC5, 0x82, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xB7,
	0x55, 0x7C, 0xF9, 0xDF, 0xDA, 0xCB, 0x3B, 0x03, 0x4F, 0x9E,
	0xFE, 0xB9, 0x44, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
	0x44, 0xAE, 0x42, 0x60, 0x82 };

/*
 * Return a lowercased copy of a GET/POST parameter or NULL. The parameter
 * may be overriden by an HTTP header called X-Limit-<fieldname>.
 * Caller must free if return is non-NULL.
 */

static char *field(struct mg_connection *conn, char *fieldname)
{
	char buf[BUFSIZ], *p, *h;
	int ret;

	snprintf(buf, sizeof(buf), "X-Limit-%s", fieldname);

	if ((h = (char *)mg_get_header(conn, buf)) != NULL) {
		p = strdup(h);
		lowercase(p);
		return (p);
	}

	if ((ret = mg_get_var(conn, fieldname, buf, sizeof(buf))) > 0) {
		p = strdup(buf);
		lowercase(p);
		return (p);
	}
	return (NULL);
}

static double field_d(struct mg_connection *conn, char *fieldname)
{
	char buf[BUFSIZ];
	int ret;

	if ((ret = mg_get_var(conn, fieldname, buf, sizeof(buf))) < 0) {
		return (NAN);
	}
	return (atof(buf));
}


static long *field_n(struct mg_connection *conn, char *fieldname)
{
	char buf[BUFSIZ];
	int ret;
	static long l;

	if ((ret = mg_get_var(conn, fieldname, buf, sizeof(buf))) < 0) {
		return (NULL);
	}
	l = atol(buf);
	return (&l);
}

/*
 * Open a view.json file, parse the JSON and return the object
 * or NULL.
 */

static JsonNode *loadview(struct udata *ud, const char *viewname)
{
	static UT_string *fpath = NULL;
	const char *doc_root = mg_get_option(ud->mgserver, "document_root");
	JsonNode *view;

	utstring_renew(fpath);
	utstring_printf(fpath, "%s/%s/%s.json", doc_root, VIEWSUBDIR, viewname);
	debug(ud, "loadview fpath=%s", UB(fpath));

	view = json_mkobject();
	if (json_copy_from_file(view, UB(fpath)) != TRUE) {
		json_delete(view);
		return (NULL);
	}

	return (view);
}

static void http_debug(char *event_name, struct mg_connection *conn)
{
	int n;
	char authuser[64];
	const char *hdr;

	*authuser = 0;

	if ((hdr = mg_get_header(conn, "Authorization")) != NULL && strncasecmp(hdr, "Digest ", 7) == 0) {
		mg_parse_header(hdr, "username", authuser, sizeof(authuser));
	}

	fprintf(stderr, "--- %s --------------------------- %s (%ld) %.*s [auth=%s]\n",
		event_name,
		conn->uri,
		(long)conn->content_len,
		(int)conn->content_len,
		conn->content,
		authuser);
	for (n = 0; n < conn->num_headers; n++) {
		struct mg_header *hh;

		hh = &conn->http_headers[n];
		fprintf(stderr, "  %s=%s\n", hh->name, hh->value);

	}
}

/*
 * Send a message into the HTTP server; this will be dispatched
 * to listening WS clients. Check whether the JSON obj contains
 * a user/device pair which match the X-Limit headers we've been
 * given. If so push it, otherwise discard because it's not
 * meant to be seen by a particular connection.
 */

void http_ws_push_json(struct mg_server *server, JsonNode *obj)
{
	struct mg_connection *c;
	JsonNode *j, *tmpo;
	int len;
	char *js, *u = NULL, *d = NULL;

	if (!obj || obj->tag != JSON_OBJECT)
		return;

	/*
	 * Iterate over connections and push message to the WS connections.
	 */

	for (c = mg_next(server, NULL); c != NULL; c = mg_next(server, c)) {
		if (c->is_websocket) {
			struct udata *ud = (struct udata *)c->server_param;
#if 0
			{
				int n;
				for (n = 0; n < c->num_headers; n++) {
					struct mg_header *hh;

					hh = &c->http_headers[n];
					if (*hh->name == 'X') {
						fprintf(stderr, "WEBSOCKET-HEADER: %s=%s\n", hh->name, hh->value);
					}
				}
			}
			puts(json_stringify(obj, NULL));
#endif

			u = field(c, "user");
			d = field(c, "device");

			if (u) {
				if ((j = json_find_member(obj, "user")) != NULL) {
					if (strcasecmp(u, j->string_) != 0) {
						fprintf(stderr, "not for %s; skip\n", u);
						free(u);
						continue;
					}
				}

				if (d) {
					if ((j = json_find_member(obj, "device")) != NULL) {
						if (strcasecmp(d, j->string_) != 0) {
							fprintf(stderr, "not for %s/%s; skip\n", u, d);
							free(u);
							free(d);
							continue;
						}
					}
				}

				free(u);
				if (d) free(d);
			}


			tmpo = json_mkobject();
			json_copy_to_object(tmpo, obj, TRUE);

			if (ud->label != NULL) {
				json_append_member(tmpo, "_label", json_mkstring(ud->label));
			}

			if ((js = json_stringify(tmpo, NULL)) != NULL) {
				len = strlen(js);
				mg_websocket_write(c, 1, js, len);
				free(js);
			}
			json_delete(tmpo);
		}
	}
}

static int send_reply(struct mg_connection *conn)
{
	if (conn->is_websocket) {
		// This handler is called for each incoming websocket frame, one or more
		// times for connection lifetime.
		// Echo websocket data back to the client.
		mg_websocket_write(conn, 1, conn->content, conn->content_len);
		return conn->content_len == 4 && !memcmp(conn->content, "exit", 4) ?  MG_FALSE : MG_TRUE;
	} else {
		mg_send_file(conn, "jp.html", NULL);

		return MG_MORE;
	}
}

/*
 * Push a list of LAST users down the Websocket. We send individual
 * JSON objects (not an array of them) because these are what the
 * WS client gets when we the recorder sees a publish.
 * Ignore the ping/ping user.
 */

static void send_last(struct mg_connection *conn)
{
	struct udata *ud = (struct udata *)conn->server_param;
	JsonNode *user_array, *o, *one;
	char *u = NULL, *d = NULL, *ghash;
	double lat, lon;

	u	  = field(conn, "user");
	d	  = field(conn, "device");

	if ((user_array = last_users(u, d, NULL)) != NULL) {

		json_foreach(one, user_array) {
			JsonNode *f;

			o = json_mkobject();

			if ((f = json_find_member(one, "username")) != NULL) {
				JsonNode *d = json_find_member(one, "device");

				/* check for pingping user (ping/ping) and skip */
				if (f && d) {
					if (!strcmp(f->string_, "ping") &&
						!strcmp(d->string_, "ping")) {
						json_delete(o);
						continue;
					}
				}

				/* Add CARD details */
				append_card_to_object(o, f->string_, d->string_);
			}

			json_append_member(o, "_type", json_mkstring("location"));
			if ((f = json_find_member(one, "lat")) != NULL)
				lat = f->number_;
				json_copy_element_to_object(o, "lat", f);
			if ((f = json_find_member(one, "lon")) != NULL)
				lon = f->number_;
				json_copy_element_to_object(o, "lon", f);

			if ((f = json_find_member(one, "tst")) != NULL)
				json_copy_element_to_object(o, "tst", f);
			if ((f = json_find_member(one, "tid")) != NULL)
				json_copy_element_to_object(o, "tid", f);
			if ((f = json_find_member(one, "addr")) != NULL)
				json_copy_element_to_object(o, "addr", f);
			if ((f = json_find_member(one, "topic")) != NULL)
				json_copy_element_to_object(o, "topic", f);

			if ((ghash = geohash_encode(lat, lon, geohash_prec())) != NULL) {
				json_append_member(o, "ghash", json_mkstring(ghash));
				free(ghash);
			}

			http_ws_push_json(ud->mgserver, o);
			json_delete(o);
		}
		json_delete(user_array);
	}
	if (u) free(u);
	if (d) free(d);
}

static int monitor(struct mg_connection *conn)
{
	char *m = monitor_get();

	mg_printf_data(conn, "%s\n", (m) ? m : "not available");
	return (MG_TRUE);
}

static int json_response(struct mg_connection *conn, JsonNode *json)
{
	char *js;

	mg_send_header(conn, "Content-Type", "application/json; charset=utf-8");
	mg_send_header(conn, "Access-Control-Allow-Origin", "*");

	if (json == NULL) {
		mg_printf_data(conn, "{}");
	} else {
		if ((js = json_stringify(json, JSON_INDENT)) != NULL) {
			mg_printf_data(conn, js);
			free(js);
		}
		json_delete(json);
	}
	return (MG_TRUE);
}

static void emit_xml_line(char *line, void *param)
{
	struct mg_connection *conn = (struct mg_connection *)param;

	mg_printf_data(conn, line);
	mg_printf_data(conn, "\n");
}

static void emit_csv_line(char *line, void *param)
{
	struct mg_connection *conn = (struct mg_connection *)param;

	mg_printf_data(conn, line);
}

static int xml_response(struct mg_connection *conn, JsonNode *obj)
{
	JsonNode *array = json_find_member(obj, "data");

	xml_output(array, XML, NULL, emit_xml_line, conn);

	json_delete(obj);
	return (MG_TRUE);
}

static int csv_response(struct mg_connection *conn, JsonNode *obj)
{
	JsonNode *array = json_find_member(obj, "data");

	csv_output(array, CSV, NULL, emit_csv_line, conn);

	json_delete(obj);
	return (MG_TRUE);
}

/*
 * We are being called with the portion behind /api/0/ as in
 * /users/ or /list
 */


#define CLEANUP do {\
		int k; \
		for (k = 0; k < nparts; k++) free(uparts[k]);\
		if (u) free(u);\
		if (d) free(d);\
		if (time_from) free(time_from);\
		if (time_to) free(time_to);\
	} while(0)


static int send_status(struct mg_connection *conn, int status, char *text)
{
	mg_send_status(conn, status);
	mg_printf_data(conn, text);
	return (MG_TRUE);
}

/*
 * Create an array of OwnTracks objects of locations and cards of
 * friends of user `u` and device `d`. Each of the objects in this
 * array *must* contain a TID as the apps will use that to construct
 * a ficticious topic name (owntracks/_http/<tid>) internally.
 * If this user/device combo has no friends, return an empty array.
 *
 * 	[ { _type: card }, {_type: location} ... ]
 */

JsonNode *populate_friends(struct mg_connection *conn, char *u, char *d)
{
	struct udata *ud = (struct udata *)conn->server_param;
	JsonNode *results = json_mkarray(), *lastuserlist;
	JsonNode *friends, *obj, *jud, *newob, *jtid;
	int np;
	char *pairs[3];
	static UT_string *userdevice = NULL;


	utstring_renew(userdevice);
	utstring_printf(userdevice, "%s-%s", u, d);

	friends = gcache_json_get(ud->httpfriends, UB(userdevice));

	if (ud->debug) {
		char *js = NULL;

		if (friends)
			js = json_stringify(friends, NULL);
		debug(ud, "Friends of %s: %s", UB(userdevice), js ? js : "<nil>");
		if (js)
			free(js);
	}

	if (friends == NULL) {
		return (results);
	}
	if (friends->tag != JSON_ARRAY) {
		olog(LOG_ERR, "expecting value of friends:%s to be an array", UB(userdevice));
		return (results);
	}

	/*
	 * Run through the array of friends of this user. Get LAST object,
	 * which contains CARD and LOCATION data. Create an array of
	 * separate location and card objects to return in HTTP mode.
	 */

	json_foreach(jud, friends) {
		if ((np = splitter(jud->string_, "/:-", pairs)) != 2) {
			continue;
		}
		if ((lastuserlist = last_users(pairs[0], pairs[1], NULL)) == NULL) {
			splitterfree(pairs);
			continue;
		}
		splitterfree(pairs);

		if ((obj = json_find_element(lastuserlist, 0)) == NULL) {
			json_delete(lastuserlist);
			continue;
		}

		/* TID is mandatory; if we don't have that, skip */
		if ((jtid = json_find_member(obj, "tid")) == NULL) {
			json_delete(lastuserlist);
			continue;
		}

		/* CARD */
		if (json_find_member(obj, "face") && json_find_member(obj, "name")) {
			newob = json_mkobject();
			json_append_member(newob, "_type", json_mkstring("card"));
			json_copy_element_to_object(newob, "tid", jtid);
			json_copy_element_to_object(newob, "face", json_find_member(obj, "face"));
			json_copy_element_to_object(newob, "name", json_find_member(obj, "name"));
			json_append_element(results, newob);
		}

		/* LOCATION */
		newob = json_mkobject();
		json_append_member(newob, "_type", json_mkstring("location"));
		json_copy_element_to_object(newob, "tid", jtid);
		json_copy_element_to_object(newob, "lat", json_find_member(obj, "lat"));
		json_copy_element_to_object(newob, "lon", json_find_member(obj, "lon"));
		json_copy_element_to_object(newob, "tst", json_find_member(obj, "tst"));
		json_copy_element_to_object(newob, "vel", json_find_member(obj, "vel"));
		json_copy_element_to_object(newob, "alt", json_find_member(obj, "alt"));
		json_copy_element_to_object(newob, "cog", json_find_member(obj, "cog"));
		json_copy_element_to_object(newob, "acc", json_find_member(obj, "acc"));
		json_copy_element_to_object(newob, "vac", json_find_member(obj, "vac"));
		json_append_element(results, newob);

		json_delete(lastuserlist);
	}

	json_delete(friends);

	return (results);
}

#ifdef WITH_ENCRYPT
char *j_encrypt(struct udata *ud, JsonNode *json, char *userdevice)
{
	unsigned char nonce[crypto_secretbox_NONCEBYTES];
	unsigned char key[crypto_secretbox_KEYBYTES];
	unsigned char *ciphertext;
	unsigned long ciphertext_len, mlen;
	char *js_string, *b64, *encrypted;
	int klen;

	if ((js_string = json_stringify(json, NULL)) == NULL) {
		olog(LOG_ERR, "Cannot decode JSON array for encryption");
		return (NULL);
	}

	memset(key, 0, sizeof(key));
	klen = gcache_get(ud->keydb, userdevice, (char *)key, sizeof(key));
	if (klen < 1) {
		debug(ud, "no encryption key for %s; not encrypting response", userdevice);
		free(js_string);
		return (NULL);
	}
	debug(ud, "encryption key for %s is {%s}", userdevice, key);

	mlen = strlen(js_string) + 0;
	ciphertext_len = mlen + crypto_box_MACBYTES;
	if ((ciphertext = malloc(ciphertext_len)) == NULL) {
		olog(LOG_ERR, "out of memory in encrypt()");
		free(js_string);
		return (NULL);
	}

	if ((encrypted = malloc(ciphertext_len + 40)) == NULL) {
		olog(LOG_ERR, "out of memory in encrypt() two");
		free(ciphertext);
		free(js_string);
		return (NULL);
	}

	/* Create random nonce */
	randombytes_buf(nonce, sizeof nonce);

	if (crypto_secretbox_easy((unsigned char *)ciphertext, (unsigned char *)js_string, mlen, nonce, key) != 0) {
		olog(LOG_ERR, "payload cannot be encrypted");
		free(js_string);
		free(ciphertext);
		free(encrypted);
		return (NULL);
	}

	memcpy(encrypted, nonce, crypto_secretbox_NONCEBYTES);
	memcpy(encrypted + crypto_secretbox_NONCEBYTES, ciphertext, ciphertext_len);
	b64 = base64_encode(encrypted, ciphertext_len + crypto_secretbox_NONCEBYTES);
	free(js_string);
	free(ciphertext);
	free(encrypted);

	return (b64);

}
#endif /* WITH_ENCRYPT */

/*
 * Invoked from an HTTP POST to /pub?u=username&d=devicename
 * We need u and d in order to contruct a topic name. Obtain
 * the content of the POST request and give it to the recorder
 * to do the needful. :)
 */

static int dopublish(struct mg_connection *conn, const char *uri)
{
	struct udata *ud = (struct udata *)conn->server_param;
	char *payload, *u, *d;
#ifdef WITH_ENCRYPT
	char *enc;
#endif
	static UT_string *topic = NULL, *userdevice = NULL;
	JsonNode *jarray;
#if WITH_LUA
	JsonNode *htobj;
#endif

	if ((u = field(conn, "u")) == NULL) {
		u = strdup("owntracks");
	}

	if ((d = field(conn, "d")) == NULL) {
		d = strdup("phone");
	}

	utstring_renew(topic);
	utstring_printf(topic, "owntracks/%s/%s", u, d);

	utstring_renew(userdevice);
	utstring_printf(userdevice, "%s-%s", u, d);

	/* We need a nul-terminated payload in handle_message() */
	payload = calloc(sizeof(char), conn->content_len + 1);
	memcpy(payload, conn->content, conn->content_len);

	debug(ud, "HTTPPUB clen=%zu, topic=%s", conn->content_len, UB(topic));

	handle_message(ud, UB(topic), payload, conn->content_len, 0, TRUE, FALSE);


	jarray = populate_friends(conn, u, d);
	extra_http_json(jarray, u, d);
#if WITH_LUA
	if ((htobj = hooks_http(ud, u, d, payload)) != NULL) {
		json_append_element(jarray, htobj);
	}
#endif
	free(payload);
	free(u);
	free(d);

#ifdef WITH_ENCRYPT
	/*
	 * The array of data to be returned is ready. Verify if the user
	 * this pertains to has an encryption key, and if so, encrypt
	 * the array in whole and return _type: encrypted
	 */

	if ((enc = j_encrypt(ud, jarray, UB(userdevice))) != NULL) {
		JsonNode *json = json_mkobject();

		json_delete(jarray);

		json_append_member(json, "_type", json_mkstring("encrypted"));
		json_append_member(json, "data", json_mkstring(enc));
		free(enc);

		return json_response(conn, json);
	}

#endif /* WITH_ENCRYPT */

	return json_response(conn, jarray);
}

#if WITH_GREENWICH

/*
 # OLD
 * username=XXX&password=YYY&tid=ZZ&nrecs=NNNN&topic=owntracks/gw/K2
 *
 * NEW
 * user=XXX&device=YYYY&limit=NNNN
 *
 */

static int ctrl_track(struct mg_connection *conn)
{
	struct udata *ud = (struct udata *)conn->server_param;
	int limit = 500;
	char *p, *username = NULL, *device = NULL;
	char *from = NULL, *to = NULL;
	time_t s_lo, s_hi;
	JsonNode *response, *obj, *locs, *fields, *json, *arr;

	if ((p = field(conn, "user")) != NULL) {
		username = strdup(p);
	}

	if ((p = field(conn, "device")) != NULL) {
		device = strdup(p);
	}

	if ((p = field(conn, "limit")) != NULL) {
		limit = atoi(p);
		free(p);
	}

	if (!username || !device) {
		send_status(conn, 416, "user and/or device missing");
		return (MG_TRUE);
	}

	if (make_times(from, &s_lo, to, &s_hi, 0) != 1) {
		send_status(conn, 416, "impossible date/time ranges");
		return (MG_TRUE);
	}

	s_lo -= (30 * 24 * 60 * 60);	/* move start point to a month prior */

	response = json_mkobject();
	json_append_member(response, "message", json_mkstring("OK"));

	fields = json_mkarray();
	json_append_element(fields, json_mkstring("tst"));
	json_append_element(fields, json_mkstring("lat"));
	json_append_element(fields, json_mkstring("lon"));

	/*
	 * Obtain a list of .rec files from lister(), possibly limited
	 * by s_lo/s_hi times, process each and build the JSON object
	 * `obj` containing an array of locations.
	 */

	obj = json_mkobject();
	locs = json_mkarray();

	debug(ud, "ctrl_track u=%s, d=%s, f=%ld, t=%ld", username, device, s_lo, s_hi);

	if ((json = lister(username, device, s_lo, s_hi, (limit > 0) ? TRUE : FALSE)) != NULL) {
		int i_have = 0;

		if ((arr = json_find_member(json, "results")) != NULL) {
			JsonNode *f;
			json_foreach(f, arr) {
				locations(f->string_, obj, locs, s_lo, s_hi, JSON, limit, fields, NULL, NULL);
				if (limit) {
					i_have += limit;
					if (i_have >= limit) {
						break;
					}
				}
			}
		}
		json_delete(json);
	}
	json_delete(obj);
	json_delete(fields);

	json_append_member(response, "track", locs);

	return (json_response(conn, response));

}


#endif /* WITH_GREENWICH */

/*
 * Procure back-end data for a VIEW. `view' is the JSON view from which
 * we obtain who/what to get. This returns a JSON array of location
 * objects obtained from REC files:
 *
 *	[
 *	 {
 *	  "_type": "location",
 *	  "dist": 11,
 *	  "cc": "DE",
 *	  "lon": xxx,
 *	  "trip": 18659000,
 *	  "lat": yyy,
 *	  "alt": 380,
 *	  "vel": 0,
 *	  "t": "T",
 *	  "cog": 0,
 *	  "tid": "hK",
 *	  "tst": 1442609999,
 *	  "ghash": "zzzzz",
 *	  "addr": "zzzzzzzzzzzzzzzzz15C, 000000 Example, Germany",
 *	  "locality": "Example",
 *	  "isorcv": "2015-09-18T22:59:59Z",
 *	  "isotst": "2015-09-18T20:59:59Z",
 *	  "disptst": "2015-09-18 20:59:59"
 *	 }
 *	]
 *
 * If `limit' then just one, and we add CARD details (face and name).
 */

static JsonNode *viewdata(struct mg_connection *conn, JsonNode *view, int limit)
{
	struct udata *ud = (struct udata *)conn->server_param;
	JsonNode *j, *json, *obj, *locs, *ju, *jd, *arr;
	char *from = NULL, *to = NULL;
	time_t s_lo, s_hi;
	int hours = 0;

	ju = json_find_member(view, "user");
	jd = json_find_member(view, "device");
	if ((j = json_find_member(view, "from")) != NULL)
		from = j->string_;
	if ((j = json_find_member(view, "to")) != NULL)
		to = j->string_;
	if ((j = json_find_member(view, "hours")) != NULL) {
		hours = j->number_;
	}

	if (!ju || !jd)
		return (NULL);

	if (make_times(from, &s_lo, to, &s_hi, hours) != 1) {
		send_status(conn, 416, "impossible date/time ranges");
		return (NULL);
	}
	/*
	 * Obtain a list of .rec files from lister(), possibly limited
	 * by s_lo/s_hi times, process each and build the JSON object
	 * `obj` containing an array of locations.
	 */

	obj = json_mkobject();
	locs = json_mkarray();

	debug(ud, "u=%s, d=%s, f=%ld, t=%ld", ju->string_, jd->string_, s_lo, s_hi);

	if ((json = lister(ju->string_, jd->string_, s_lo, s_hi, (limit > 0) ? TRUE : FALSE)) != NULL) {
		int i_have = 0;

		if ((arr = json_find_member(json, "results")) != NULL) {
			JsonNode *f;
			json_foreach(f, arr) {
				locations(f->string_, obj, locs, s_lo, s_hi, JSON, limit, NULL, NULL, NULL);
				if (limit) {
					i_have += limit;
					if (i_have >= limit) {
						JsonNode *o, *c = json_mkobject();
						append_card_to_object(c, ju->string_, jd->string_);

						/* Add card details to 0th locs element */
						if ((o = json_find_element(locs, 0)) != NULL) {
							JsonNode *face, *name;

							face = json_find_member(c, "face");
							name = json_find_member(c, "name");

							if (face)
								json_append_member(o, "face", json_mkstring(face->string_));
							if (name)
								json_append_member(o, "name", json_mkstring(name->string_));
							// puts(json_stringify(o, " "));
						}
						json_delete(c);
						break;
					}
				}
			}
		}
		json_delete(json);
	}
	json_delete(obj);

	return (locs);
}

/*
 * Build and return JavaScript file containing an API key variable in it.
 */

static int apikey(struct mg_connection *conn)
{
	struct udata *ud = (struct udata *)conn->server_param;
	static UT_string *sbuf = NULL;

	utstring_renew(sbuf);
	utstring_printf(sbuf, "var apiKey = '%s';",
		ud->browser_apikey ? ud->browser_apikey : "");

	mg_send_header(conn, "Content-type", "application/javascript");
	mg_printf_data(conn, "%s", UB(sbuf));
	return (MG_TRUE);
}

/*
 * We're being asked for a view. `viewname' contains the ID for this view.
 * A view is a JSON file in docroot. The JSON describes which file
 * should actually be served as well as a bunch of other things.
 */

static int view(struct mg_connection *conn, const char *viewname)
{
	struct udata *ud = (struct udata *)conn->server_param;
	int limit;
	char *p, buf[BUFSIZ];
	const char *doc_root = mg_get_option(ud->mgserver, "document_root");
	static UT_string *fpath = NULL, *sbuf = NULL;
	FILE *fp;
	JsonNode *view, *j, *locarray, *obj, *loc, *geoline;
	viewtype vtype = PAGE;

	if ((p = field(conn, "geodata")) != NULL) {
		vtype = GEODATA;
		free(p);
	}

	if ((p = field(conn, "lastpos")) != NULL) {
		vtype = LASTPOS;
		free(p);
	}

	debug(ud, "view: [%s]: => viewtype=%d", viewname, vtype);

	if (!viewname || !*viewname) {
		return send_status(conn, 404, "Not found");
	}


	if ((view = loadview(ud, viewname)) == NULL) {
		return send_status(conn, 404, "View not found");
	}

	switch (vtype) {
	    case PAGE:

		/*
		 * Find the page we're going to serve and serve it,
		 * replacing occurrences of "@@@" with our special URIs.
		 */

		if ((j = json_find_member(view, "page")) == NULL) {
			json_delete(view);
			return send_status(conn, 401, "no page in view");
		}

		utstring_renew(fpath);
		utstring_printf(fpath, "%s/%s/%s", doc_root, VIEWSUBDIR, j->string_);
		debug(ud, "page file=%s", UB(fpath));

		if ((fp = fopen(UB(fpath), "r")) == NULL) {
			json_delete(view);
			return send_status(conn, 404, "Cannot open view page");
		}

		mg_send_header(conn, "Content-type", "text/html");
		utstring_renew(sbuf);
		while (fgets(buf, sizeof(buf), fp) != NULL) {
			if ((p = strstr(buf, "@@@GEO@@@")) != NULL) {
				*p = 0;
				utstring_clear(sbuf);
				utstring_printf(sbuf, "%s%s?geodata=1%s",
					buf,
					viewname, // conn->uri,
					p+strlen("@@@GEO@@@"));
				mg_printf_data(conn, "%s", UB(sbuf));
			} else if ((p = strstr(buf, "@@@LASTPOS@@@")) != NULL) {
				*p = 0;
				utstring_clear(sbuf);
				utstring_printf(sbuf, "%s%s?lastpos=1%s",
					buf,
					viewname, // conn->uri,
					p+strlen("@@@LASTPOS@@@"));
				mg_printf_data(conn, "%s", UB(sbuf));
			} else {
				mg_printf_data(conn, "%s", buf);
			}
		}
		fclose(fp);
		return (MG_TRUE);
		/* NOTREACHED */
		break;

	    case GEODATA:

			/*
			 * We're being asked for the GeoJSON track data for this view.
			 */

			if ((locarray = viewdata(conn, view, limit=0)) == NULL) {
				return (MG_TRUE);
			}

			obj = json_mkobject();

			json_delete(view);

			if ((geoline = geo_linestring(locarray)) != NULL) {
				json_delete(obj);
				return (json_response(conn, geoline));
			}

			/* Return empty object */

			return (json_response(conn, obj));
			/* NOTREACHED */
			break;

	    case LASTPOS:

			/*
			 * Last position being requested. We're going to search with
			 * limit=1 in order to get the very last position in the
			 * requested time frame. Furthermore, we add all elements
			 * of the actual view.json to the object we return to the
			 * Web browser to allow those for page configuration if
			 * desired.
			 */

			if ((locarray = viewdata(conn, view, limit=1)) == NULL) {
				return (MG_TRUE);
			}

			obj = json_mkobject();

			/*
			 * Append members of the view object into the location object
			 * which is being sent back to the view's page, in particular
			 * things like 'label' and 'zoom'.
			 */

			json_foreach(loc, locarray) {
				JsonNode *v;
				json_foreach(v, view) {
					if (!strcmp(v->key, "auth"))
						continue;
					// printf("%d %s\n", v->tag, v->key);
					if (v->tag == JSON_OBJECT)
						json_copy_to_object(loc, v, FALSE);
					else
						json_copy_element_to_object(loc, v->key, v);
				}
			}

			json_append_member(obj, "data", locarray);

			json_delete(view);

			return (json_response(conn, obj));
			/* NOTREACHED */
			break;
	}

	return (MG_TRUE);
}

static int dispatch(struct mg_connection *conn, const char *uri)
{
	output_type otype = JSON;
	int nparts, ret, limit = 0;
	char *uparts[MAXPARTS], buf[BUFSIZ], *u = NULL, *d = NULL;
	char *time_from = NULL, *time_to = NULL;
	time_t s_lo, s_hi;
	JsonNode *json, *obj, *locs;
	struct udata *ud = (struct udata *)conn->server_param;


	if ((nparts = splitter((char *)uri, "/", uparts)) == -1) {
		mg_send_status(conn, 405);
		mg_printf_data(conn, "no way\n");
		return (MG_TRUE);
	}

#if 0
	fprintf(stderr, "DISPATCH: %s\n", uri);
	for (ret = 0; ret < nparts; ret++) {
		fprintf(stderr, "%d = %s\n", ret, uparts[ret]);
	}
#endif

	u	  = field(conn, "user");
	d	  = field(conn, "device");
	time_from = field(conn, "from");
	time_to	  = field(conn, "to");

#if WITH_KILL
	if (nparts == 1 && !strcmp(uparts[0], "kill")) {
		JsonNode *deleted;

		if (!u || !*u || !d || !*d) {
			CLEANUP;
			mg_send_status(conn, 416);
			mg_printf_data(conn, "user and device are required\n");
			return (MG_TRUE);
		}

		if ((deleted = kill_datastore(u, d)) == NULL) {
			mg_send_status(conn, 416);
			mg_printf_data(conn, "cannot kill data for %s/%s\n", u, d);
			CLEANUP;
			return (MG_TRUE);
		}
		CLEANUP;
		return (json_response(conn, deleted));
	}
#endif /* WITH_KILL */

	if (nparts == 1 && !strcmp(uparts[0], "version")) {
		JsonNode *json = json_mkobject();

		json_append_member(json, "version", json_mkstring(VERSION));
		json_append_member(json, "git", json_mkstring(GIT_VERSION));
		CLEANUP;
		return (json_response(conn, json));
	}

	if (nparts == 1 && !strcmp(uparts[0], "last")) {
		JsonNode *user_array, *fields = NULL;
		char *flds = field(conn, "fields");

		if (flds != NULL) {
			fields = json_splitter(flds, ",");
			free(flds);
		}

		if ((user_array = last_users(u, d, fields)) != NULL) {
			CLEANUP;
			json_delete(fields);

			return (json_response(conn, user_array));
		}
		json_delete(fields);
	}

	if ((ret = mg_get_var(conn, "limit", buf, sizeof(buf))) > 0) {
		limit = atoi(buf);
	}

	if ((ret = mg_get_var(conn, "format", buf, sizeof(buf))) > 0) {
		if (!strcmp(buf, "geojson"))
			otype = GEOJSON;
		else if (!strcmp(buf, "json"))
			otype = JSON;
		else if (!strcmp(buf, "linestring"))
			otype = LINESTRING;
		else if (!strcmp(buf, "csv"))
			otype = CSV;
		else if (!strcmp(buf, "xml"))
			otype = XML;
		else if (!strcmp(buf, "gpx"))
			otype = GPX;
		else {
			mg_send_status(conn, 400);
			mg_printf_data(conn, "unrecognized format\n");
			CLEANUP;
			return (MG_TRUE);
		}
	}

	if (make_times(time_from, &s_lo, time_to, &s_hi, 0) != 1) {
		mg_send_status(conn, 416);
		mg_printf_data(conn, "impossible date/time ranges\n");
		return (MG_TRUE);
	}

	// fprintf(stderr, "user=[%s], device=[%s]\n", (u) ? u : "<nil>", (d) ? d : "<NIL>");

	/* /list			[<username>[<device>]] */

	if (nparts == 1 && !strcmp(uparts[0], "list")) {
		if ((json = lister(u, d, 0, s_hi, FALSE)) != NULL) {
			CLEANUP;
			return (json_response(conn, json));
		}
	}

	/* /locations			[<username>[<device>]][[fields=a,b,c] */

	if (nparts == 1 && !strcmp(uparts[0], "locations")) {
		if (!u || !d) {
			CLEANUP;
			mg_send_status(conn, 416);
			mg_printf_data(conn, "user and device are required\n");
			return (MG_TRUE);
		}
		/*
		 * Obtain a list of .rec files from lister(), possibly limited
		 * by s_lo/s_hi times, process each and build the JSON `obj'
		 * with an array of locations.
		 */

		obj = json_mkobject();
		locs = json_mkarray();

                if ((json = lister(u, d, s_lo, s_hi, (limit > 0) ? TRUE : FALSE)) != NULL) {
			JsonNode *arr, *fields = NULL;
			char *flds = field(conn, "fields");
			int i_have = 0;

			if (flds != NULL) {
				fields = json_splitter(flds, ",");
				free(flds);
			}

			CLEANUP;

                        if ((arr = json_find_member(json, "results")) != NULL) {
				JsonNode *f;
                                json_foreach(f, arr) {
                                        locations(f->string_, obj, locs, s_lo, s_hi, otype, limit, fields, NULL, NULL);
					if (limit) {
						i_have += limit;
						if (i_have >= limit)
							break;
					}
                                        // printf("%s\n", f->string_);
                                }
                        }
			json_delete(fields);
                        json_delete(json);
                }
		json_append_member(obj, "data", locs);
		json_append_member(obj, "status", json_mknumber(200));

		if (otype == JSON) {
			return (json_response(conn, obj));
		} else if (otype == CSV) {
			return (csv_response(conn, obj));
		} else if (otype == XML) {
			return (xml_response(conn, obj));
		} else if (otype == GPX) {
			char *xml = gpx_string(locs);

			if (xml) {
				mg_send_data(conn, xml, strlen(xml));
			}
			json_delete(obj);
			return (MG_TRUE);
		} else if (otype == LINESTRING) {
			JsonNode *geoline = geo_linestring(locs);

			json_delete(obj);
			return (json_response(conn, geoline));

		} else if (otype == GEOJSON) {
			JsonNode *geojson = geo_json(locs);

			json_delete(obj);
			if (geojson != NULL) {
				return (json_response(conn, geojson));
			}
			json_delete(obj);
			return send_status(conn, 422, "geojson failed");
		}
        }

	if (nparts == 1 && !strcmp(uparts[0], "q")) {
		JsonNode *geo = NULL;
		char *lat = field(conn, "lat");
		char *lon = field(conn, "lon");
		char *ghash;

		if (lat && lon) {
			if ((ghash = geohash_encode(atof(lat), atof(lon), geohash_prec())) != NULL) {
				geo = gcache_json_get(ud->gc, ghash);
				free(ghash);
			}
		}

		if (lat) free(lat);
		if (lon) free(lon);
		CLEANUP;

		return (json_response(conn, geo));
	}

	CLEANUP;
	// mg_printf_data(conn, "user=[%s], device=[%s]\n", (u) ? u : "<nil>", (d) ? d : "<NIL>");
	mg_printf_data(conn, "no comprendo");

	return (MG_TRUE);
}

/* /api/0/photo/?user=yyyy */
static int photo(struct mg_connection *conn)
{
	char *u, *userphoto;

	if ((u = field(conn, "user")) == NULL) {
		mg_send_status(conn, 416);
		mg_printf_data(conn, "missing username\n");
		return (MG_TRUE);
	}

	if (((userphoto = storage_userphoto(u)) != NULL) && access(userphoto, R_OK) == 0) {
		free(u);
		mg_send_file(conn, userphoto, NULL);
		return (MG_MORE);
	}
	free(u);
	mg_send_header(conn, "Content-type", "image/png");
	mg_send_data(conn, border40x40png, sizeof(border40x40png));
	return (MG_TRUE);
}

/*
 * Verify that the digest credentials presented in the request match
 * the on-file  `hash_ha1' we have for the user. (Most of this code
 * adapted from check_password() and authorize_digest() in mongoose.c.)
 * If they match, return MG_TRUE.
 */

static int authorize_digest(struct mg_connection *c, char *hash_ha1)
{
	const char *hdr;
	char ha2[32 + 1], expected_response[32 + 1], user[100], nonce[100];
	char uri[BUFSIZ], cnonce[100], resp[100], qop[100], nc[100];

	if (c == NULL || hash_ha1 == NULL) return (MG_FALSE);
	if ((hdr = mg_get_header(c, "Authorization")) == NULL ||
		strncasecmp(hdr, "Digest ", 7) != 0) return (MG_FALSE);
	if (!mg_parse_header(hdr, "username", user, sizeof(user))) return (MG_FALSE);
	if (!mg_parse_header(hdr, "cnonce", cnonce, sizeof(cnonce))) return (MG_FALSE);
	if (!mg_parse_header(hdr, "response", resp, sizeof(resp))) return (MG_FALSE);
	if (!mg_parse_header(hdr, "uri", uri, sizeof(uri))) return (MG_FALSE);
	if (!mg_parse_header(hdr, "qop", qop, sizeof(qop))) return (MG_FALSE);
	if (!mg_parse_header(hdr, "nc", nc, sizeof(nc))) return (MG_FALSE);
	if (!mg_parse_header(hdr, "nonce", nonce, sizeof(nonce))) return (MG_FALSE);

	mg_md5(ha2, c->request_method, ":", uri, NULL);
	mg_md5(expected_response, hash_ha1, ":", nonce, ":", nc,
		":", cnonce, ":", qop, ":", ha2, NULL);

#if 0
	printf("method = %s\n", c->request_method);
	printf("ha1 = %s\n", hash_ha1);
	printf("uri = %s\n", uri);
	printf("nonce = %s\n", nonce);
	printf("nc = %s\n", nc);
	printf("cnonce = %s\n", cnonce);
	printf("qop = %s\n", qop);
	printf("response          = %s\n", resp);
	printf("expected_response = %s\n", expected_response);
#endif

	return (strcasecmp(resp, expected_response) == 0 ? (MG_TRUE) : (MG_FALSE));
}

/*
 * Called from ev_handler() to determine whether a connection should be
 * authorized. Currently only supported for VIEWs.
 */

static int authorize(struct mg_connection *conn)
{
	struct udata *ud = (struct udata *)conn->server_param;
	const char *viewname;
	JsonNode *view, *j;
	int authorized = TRUE;

	if (strncmp(conn->uri, "/view/", strlen("/view/")) != 0) {
		return (MG_TRUE);
	}

	viewname = conn->uri + strlen("/view/");
	debug(ud, "In authorize() for view=%s", viewname);

	/* IDEA: we could have an expire= field in view.json to let this die automatically */

	/*
	 * Load view. Check if "auth" element exists, and if it does,
	 * verify digest auth against that. If that fails, access is
	 * denied, otherwise granted.
	 */

	if ((view = loadview(ud, viewname)) != NULL) {

		/*
		 * If we have no 'auth' element, access is granted because nothing to check.
		 * Otherwise, "auth" is an array of HA1 strings.
		 */

		if ((j = json_find_member(view, "auth")) != NULL) {
			JsonNode *ha1;

			json_foreach(ha1, j) {
				char *hash_ha1 = ha1->string_;

				authorized = authorize_digest(conn, hash_ha1);
				debug(ud, "AUTHTOKEN=%s, AUTHORIZED=%d", hash_ha1, authorized);
				if (authorized)
					break;
			}
		}
		json_delete(view);
	}

	return (authorized);
}

int ev_handler(struct mg_connection *conn, enum mg_event ev)
{
	struct udata *ud = (struct udata *)conn->server_param;

	switch (ev) {
		case MG_AUTH:
			if (ud->debug) http_debug("AUTH", conn);
			return (authorize(conn));

		case MG_REQUEST:

			if (ud->debug) http_debug("REQUEST", conn);
			if (ud->debug) {
				int n;
				fprintf(stderr, "------------------------------ %s (%ul) %.*s\n",
					conn->uri,
					(int)conn->content_len,
					(int)conn->content_len,
					conn->content);
				for (n = 0; n < conn->num_headers; n++) {
					struct mg_header *hh;

					hh = &conn->http_headers[n];
					fprintf(stderr, "  %s=%s\n", hh->name, hh->value);
				}
			}

			/* Websockets URI ?*/
			if (strcmp(conn->uri, "/ws/last") == 0) {

				/*
				 * WS client sends us "LAST" and we return all
				 * last locations to it.
				 */
				if (conn->content_len == 4 && !strncmp(conn->content, "LAST", 4)) {
					send_last(conn);
				}
				return send_reply(conn);
			}
			if (strcmp(conn->uri, "/ws") == 0) {
				fprintf(stderr, "WS: %s\n", conn->uri);
				return send_reply(conn);
			}

			olog(LOG_DEBUG, "http: %s %s", conn->request_method, conn->uri);

			if (strcmp(conn->uri, MONITOR_URI) == 0) {
				return monitor(conn);
			}

			if (strncmp(conn->uri, "/api/0/photo/", strlen("/api/0/photo/")) == 0) {
				return photo(conn);
			}

			if (strncmp(conn->uri, API_PREFIX, strlen(API_PREFIX)) == 0) {
				return dispatch(conn, conn->uri + strlen(API_PREFIX) - 1);
			}

			if (strncmp(conn->uri, "/view/", strlen("/view/")) == 0) {
				return view(conn, conn->uri + strlen("/view/"));
			}

			if (strstr(conn->uri, "/apikey.js") != NULL) {
				return apikey(conn);
			}


			if (!strcmp(conn->request_method, "POST")) {
				if (!strcmp(conn->uri, "/pub")) {
					return dopublish(conn, conn->uri + strlen("/pub"));
				}
			}

			if (!strcmp(conn->request_method, "POST")) {

#if WITH_GREENWICH
				if (!strncmp(conn->uri, "/ctrl/track", strlen("/ctrl/track"))) {
					return ctrl_track(conn);
				}
#endif /* WITH_GREENWICH */
				if (!strcmp(conn->uri, "/block")) {
					int blocked = TRUE;
					char buf[BUFSIZ], *u;

					if ((u = field(conn, "user")) != NULL) {
						if (gcache_put(ud->gc, "blockme", buf) != 0) {
							fprintf(stderr, "HTTP: gcahce put error\n");
							blocked = FALSE;
						}
						free(u);
					}
					mg_printf_data(conn, "User %s %s", buf, (blocked) ? "BLOCKED" : "UNblocked");
					return (MG_TRUE);
				}
			}

			/*
			 * Let's see if this is a GET request with particular fields, in which
			 * case we assume it's an osmand/traccar location report.
			 *
			 * http://demo.traccar.org:5055/?id=123456&lat={0}&lon={1}&timestamp={2}&hdop={3}&altitude={4}&speed={5}
			 * https://www.traccar.org/osmand/
			 */

			if (!strcmp(conn->request_method, "GET") && !strcmp(conn->uri, "/")) {
				JsonNode *obj = json_mkobject();
				static UT_string *topic = NULL;
				char *js, *parts[10], buf[512];
				double d;
				long *l;
				int n;

				if ((n = mg_get_var(conn, "id", buf, sizeof(buf))) > 0) {
					if ((n = splitter(buf, "/", parts)) != 2) {
						goto not_traccar;
					}
					olog(LOG_DEBUG, "Traccar requested for %s", buf);
					json_append_member(obj, "user", json_mkstring(parts[0]));
					json_append_member(obj, "device", json_mkstring(parts[1]));
					utstring_renew(topic);
					utstring_printf(topic, "owntracks/%s/%s", parts[0], parts[1]);
					splitterfree(parts);
				} else {
					goto not_traccar;
				}


				if ((d = field_d(conn, "lat")) == NAN) goto not_traccar;
				json_append_member(obj, "lat", json_mknumber(d));

				if ((d = field_d(conn, "lon")) == NAN) goto not_traccar;
				json_append_member(obj, "lon", json_mknumber(d));


				if ((l = field_n(conn, "timestamp")) == NULL) goto not_traccar;
				json_append_member(obj, "tst", json_mknumber(*l));

				/* The following are optional */
				if ((l = field_n(conn, "altitude")) != NULL) {
					json_append_member(obj, "alt", json_mknumber(*l));
				}

				/* document says 'hdop'; Traccar/iOS uses 'bearing' */
				if ((l = field_n(conn, "bearing")) != NULL) {
					json_append_member(obj, "cog", json_mknumber(*l));
				}

				if ((l = field_n(conn, "batt")) != NULL) {
					json_append_member(obj, "batt", json_mknumber(*l));
				}

				if ((l = field_n(conn, "speed")) != NULL) {
					/* is in knots; we (OwnTracks) want kph */

					*l *= 1.852;
					json_append_member(obj, "vel", json_mknumber(*l));
				}

				json_append_member(obj, "_type", json_mkstring("location"));
				json_append_member(obj, "_proto", json_mkstring("osmand"));

				if ((js = json_stringify(obj, NULL)) != NULL) {
					fprintf(stderr, "Traccar: %s\n", js);
					handle_message(ud, UB(topic), js, strlen(js), 0, TRUE, FALSE);
					free(js);
				}

				mg_printf_data(conn, "tak");
				json_delete(obj);

				return (MG_TRUE);

			   not_traccar:
				json_delete(obj);
			}

			/*
			 * We can't handle this request ourselves. Return
			 * to Mongoose and have it try document root.
			 */

			return (MG_FALSE);

		default:
			return (MG_FALSE);
	}
}

#endif /* WITH_HTTP */

