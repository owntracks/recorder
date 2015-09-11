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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "json.h"
#include "config.h"
#include "util.h"
#include "misc.h"
#include "storage.h"
#include "udata.h"
#ifdef HAVE_HTTP
# include "http.h"
#endif

#ifdef HAVE_HTTP

static void lowercase(char *s)
{
	char *bp;

	for (bp = s; bp && *bp; bp++) {
		if (isupper(*bp))
			*bp = tolower(*bp);
	}
}

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
	JsonNode *j;
	int len;
	char *js, *u = NULL, *d = NULL;

	if (!obj || obj->tag != JSON_OBJECT)
		return;

	/*
	 * Iterate over connections and push message to the WS connections.
	 */

	for (c = mg_next(server, NULL); c != NULL; c = mg_next(server, c)) {
		if (c->is_websocket) {
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


			if ((js = json_stringify(obj, NULL)) != NULL) {
				len = strlen(js);
				mg_websocket_write(c, 1, js, len);
				free(js);
			}
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

	if ((user_array = last_users(u, d)) != NULL) {

		if (u) free(u);
		if (d) free(d);

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

	mg_send_header(conn, "Content-Type", "application/json");
	mg_send_header(conn, "Access-Control-Allow-Origin", "*");

	if ((js = json_stringify(json, JSON_INDENT)) != NULL) {
		mg_printf_data(conn, js);
		free(js);
	}
	json_delete(json);
	return (MG_TRUE);
}


/*
 * We are being called with the portion behind /api/0/ as in
 * /users/ or /list
 */

#define MAXPARTS 40

#define CLEANUP do {\
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
	// struct udata *ud = (struct udata *)conn->server_param;


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

	if (nparts == 1 && !strcmp(uparts[0], "last")) {
                JsonNode *user_array;

                if ((user_array = last_users(u, d)) != NULL) {
			CLEANUP;
			return (json_response(conn, user_array));
                }
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

	fprintf(stderr, "user=[%s], device=[%s]\n", (u) ? u : "<nil>", (d) ? d : "<NIL>");

	/* /list			[<username>[<device>]] */

	if (nparts == 1 && !strcmp(uparts[0], "list")) {
		if ((json = lister(u, d, 0, s_hi, FALSE)) != NULL) {
			CLEANUP;
			return (json_response(conn, json));
		}
	}

	/* /locations			[<username>[<device>]] */

	if (nparts == 1 && !strcmp(uparts[0], "locations")) {
                /*
                 * Obtain a list of .rec files from lister(), possibly limited by s_lo/s_hi times,
                 * process each and build the JSON `obj' with an array of locations.
                 */

		obj = json_mkobject();
		locs = json_mkarray();

                if ((json = lister(u, d, s_lo, s_hi, (limit > 0) ? TRUE : FALSE)) != NULL) {
			JsonNode *arr;

			CLEANUP;

                        if ((arr = json_find_member(json, "results")) != NULL) { // get array
				JsonNode *f;
                                json_foreach(f, arr) {
                                        locations(f->string_, obj, locs, s_lo, s_hi, otype, limit, NULL);
                                        // printf("%s\n", f->string_);
                                }
                        }
                        json_delete(json);
                }
		json_append_member(obj, "locations", locs);

		if (otype == JSON) {
			return (json_response(conn, obj));
		} else if (otype == LINESTRING) {
			JsonNode *geolinestring = geo_linestring(locs);

			if (geolinestring != NULL) {
				json_delete(obj);
				return (json_response(conn, geolinestring));
			}

		} else if (otype == GEOJSON) {
			JsonNode *geojson = geo_json(locs);

			json_delete(obj);
			if (geojson != NULL) {
				return (json_response(conn, geojson));
			}
		}
        }

	// mg_printf_data(conn, "user=[%s], device=[%s]\n", (u) ? u : "<nil>", (d) ? d : "<NIL>");
	mg_printf_data(conn, "no comprendo");

	return (MG_TRUE);
}

int ev_handler(struct mg_connection *conn, enum mg_event ev)
{
	struct udata *ud = (struct udata *)conn->server_param;

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

			if (strncmp(conn->uri, API_PREFIX, strlen(API_PREFIX)) == 0) {
				return dispatch(conn, conn->uri + strlen(API_PREFIX) - 1);
			}


			if (!strcmp(conn->request_method, "POST")) {

				if (!strcmp(conn->uri, "/block")) {
					int ret, blocked = TRUE;
					char buf[BUFSIZ];

					if ((ret = mg_get_var(conn, "user", buf, sizeof(buf))) > 0) {
						if (gcache_put(ud->gc, "blockme", buf) != 0) {
							fprintf(stderr, "HTTP: gcahce put error\n");
							blocked = FALSE;
						}
					}
					mg_printf_data(conn, "User %s %s", buf, (blocked) ? "BLOCKED" : "UNblocked");
					return (MG_TRUE);
				}
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

#endif /* HAVE_HTTP */

