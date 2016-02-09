/*
 * Copyright (C) 2015-2016 Jan-Piet Mens <jpmens@gmail.com> and OwnTracks
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "json.h"
#include "util.h"
#include "misc.h"
#include "storage.h"
#include "geohash.h"
#include "udata.h"
#include "version.h"
#ifdef WITH_HTTP
# include "http.h"
#endif

#ifdef WITH_HTTP

#define MAXPARTS 40

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
 */

static void send_last(struct mg_connection *conn)
{
	struct udata *ud = (struct udata *)conn->server_param;
	JsonNode *user_array, *o, *one;
	char *u = NULL, *d = NULL;

	u	  = field(conn, "user");
	d	  = field(conn, "device");

	if ((user_array = last_users(u, d, NULL)) != NULL) {

		json_foreach(one, user_array) {
			JsonNode *f;

			o = json_mkobject();

			json_append_member(o, "_type", json_mkstring("location"));
			if ((f = json_find_member(one, "lat")) != NULL)
				json_copy_element_to_object(o, "lat", f);
			if ((f = json_find_member(one, "lon")) != NULL)
				json_copy_element_to_object(o, "lon", f);

			if ((f = json_find_member(one, "tst")) != NULL)
				json_copy_element_to_object(o, "tst", f);
			if ((f = json_find_member(one, "tid")) != NULL)
				json_copy_element_to_object(o, "tid", f);
			if ((f = json_find_member(one, "addr")) != NULL)
				json_copy_element_to_object(o, "addr", f);
			if ((f = json_find_member(one, "topic")) != NULL)
				json_copy_element_to_object(o, "topic", f);

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
	xml_output(obj, XML, NULL, emit_xml_line, conn);

	json_delete(obj);
	return (MG_TRUE);
}

static int csv_response(struct mg_connection *conn, JsonNode *obj)
{
	csv_output(obj, CSV, NULL, emit_csv_line, conn);

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

static int dispatch(struct mg_connection *conn, const char *uri)
{
	output_type otype = JSON;
	int nparts, ret, limit = 0;
	char *uparts[MAXPARTS], buf[BUFSIZ], *u = NULL, *d = NULL;
	char *time_from = NULL, *time_to = NULL;
	time_t s_lo, s_hi;
	JsonNode *json, *obj, *locs;
#ifdef WITH_LMDB
	struct udata *ud = (struct udata *)conn->server_param;
#endif


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
		else {
			mg_send_status(conn, 400);
			mg_printf_data(conn, "unrecognized format\n");
			CLEANUP;
			return (MG_TRUE);
		}
	}

	if (make_times(time_from, &s_lo, time_to, &s_hi) != 1) {
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
		} else if (otype == LINESTRING) {
			JsonNode *geoline = geo_linestring(locs);

			if (geoline != NULL) {
				json_delete(obj);
				return (json_response(conn, geoline));
			}

		} else if (otype == GEOJSON) {
			JsonNode *geojson = geo_json(locs);

			json_delete(obj);
			if (geojson != NULL) {
				return (json_response(conn, geojson));
			}
		}
        }

#ifdef WITH_LMDB
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
#endif

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

int ev_handler(struct mg_connection *conn, enum mg_event ev)
{
#ifdef WITH_LMDB
	struct udata *ud = (struct udata *)conn->server_param;
#endif

	switch (ev) {
		case MG_AUTH:
			return (MG_TRUE);

		case MG_REQUEST:

#if 0
			{ int n;
			fprintf(stderr, "------------------------------ %s (%ld) %.*s\n",
					conn->uri,
					conn->content_len,
					(int)conn->content_len,
					conn->content);
			for (n = 0; n < conn->num_headers; n++) {
				struct mg_header *hh;

				hh = &conn->http_headers[n];
				fprintf(stderr, "  %s=%s\n", hh->name, hh->value);

			}
			}
#endif
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



			if (!strcmp(conn->request_method, "POST")) {

#ifdef WITH_LMDB
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
#endif
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

