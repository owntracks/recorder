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
#include "json.h"
#include "config.h"
#include "util.h"
#include "storage.h"
#include "udata.h"
#ifdef HAVE_HTTP
# include "http.h"
#endif

#ifdef HAVE_HTTP

/*
 * Send a message into the HTTP server; this will be dispatched
 * to listening WS clients.
 */

void http_ws_push(struct mg_server *server, char *text)
{
	struct mg_connection *c;
	char buf[4096];
	int len = snprintf(buf, sizeof(buf), "%s", text);

	/*
	 * Iterate over connections and push message to the WS connections.
	 */

	for (c = mg_next(server, NULL); c != NULL; c = mg_next(server, c)) {
		if (c->is_websocket) {
			mg_websocket_write(c, 1, buf, len);
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

	fprintf(stderr, "DISPATCH: %s\n", uri);

	if ((nparts = splitter((char *)uri, "/", uparts)) == -1) {
		mg_send_status(conn, 405);
		mg_printf_data(conn, "no way\n");
		return (MG_TRUE);
	}

	for (ret = 0; ret < nparts; ret++) {
		fprintf(stderr, "%d = %s\n", ret, uparts[ret]);
	}

	if ((ret = mg_get_var(conn, "user", buf, sizeof(buf))) > 0) {
		u = strdup(buf);
	}
	if ((ret = mg_get_var(conn, "device", buf, sizeof(buf))) > 0) {
		d = strdup(buf);
	}
	if ((ret = mg_get_var(conn, "limit", buf, sizeof(buf))) > 0) {
		limit = atoi(buf);
	}
	if ((ret = mg_get_var(conn, "from", buf, sizeof(buf))) > 0) {
		time_from = strdup(buf);
	}
	if ((ret = mg_get_var(conn, "to", buf, sizeof(buf))) > 0) {
		time_to = strdup(buf);
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
		mg_send_status(conn, 405);
		mg_printf_data(conn, "wrong times\n");
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

			/* Websockets URI ?*/
			if (strcmp(conn->uri, "/ws") == 0) {
				return send_reply(conn);
			}

			fprintf(stderr, "*** Request: %s [%s]\n", conn->request_method, conn->uri);

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

