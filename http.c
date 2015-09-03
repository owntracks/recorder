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
extern struct mg_server *mgserver;

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

void push_geojson(struct mg_connection *conn)
{

	JsonNode *obj, *locs, *arr, *f, *geojson, *json, *fields = NULL;
	time_t s_lo, s_hi;
	char *time_from = "2015-08-24", *time_to = NULL;
	char *js;

	char *username = "jpm", *device = "test";

	if (make_times(time_from, &s_lo, time_to, &s_hi) != 1) {
		return;
	}

	obj = json_mkobject();
	locs = json_mkarray();

	/*
	 * Obtain a list of .rec files from lister(), possibly limited by s_lo/s_hi times,
	 * process each and build the JSON `obj' with an array of locations.
	 */

	if ((json = lister(username, device, s_lo, s_hi, FALSE)) != NULL) {
		if ((arr = json_find_member(json, "results")) != NULL) { // get array
			json_foreach(f, arr) {
				// fprintf(stderr, "%s\n", f->string_);
				locations(f->string_, obj, locs, s_lo, s_hi, GEOJSON, 0, fields);
			}
		}
		json_delete(json);
	}

	json_append_member(obj, "locations", locs);


	geojson = geo_json(locs);


	if (geojson != NULL) {
		js = json_stringify(geojson, JSON_INDENT);
		if (js != NULL) {
			static char buf[40];
			mg_send_header(conn, "Content-type", "application/json");
			mg_send_header(conn, "Access-Control-Allow-Origin", "*");
			// sprintf(buf, "%ld", strlen(js));
			// mg_send_header(conn, "Content-length", buf);
			//mg_printf_data(conn, js);
			mg_send_data(conn, js, strlen(js));
			strcpy(buf, "{\"name\": \"Chasey\"}");
			// mg_printf(conn, "%s\n", buf);
			free(js);
		}
		json_delete(geojson);
	}

	json_delete(obj);
}

/*
 * We are being called with the portion behind /api/0/ as in
 * /users/ or /list
 */

static int dispatch(struct mg_connection *conn, const char *uri)
{
	int ret;

	char user[BUFSIZ];

			ret = mg_get_var(conn, "user", user, sizeof(user));
			if (ret < 0) {
				puts("*** no USER specified ***");
			} else {
				printf("USER: ret=%d, user=[%s]\n", ret, user);
			}

	mg_send_status(conn, 405);
	mg_printf_data(conn, "no way\n");
	return MG_TRUE;

	mg_send_header(conn, "Content-Type", "text/plain");


	printf("DISPATCH: %s\n", uri);
	return MG_FALSE;
	mg_printf_data(conn, "Ta.\n");
	return send_reply(conn);
	return (MG_TRUE);
}

int ev_handler(struct mg_connection *conn, enum mg_event ev)
{
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

