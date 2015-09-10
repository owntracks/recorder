#ifndef _HTTP_H_INCLUDED_
# define _HTTP_H_INCLUDED_

#include "mongoose.h"
#include "storage.h"
#include "json.h"

#define API_PREFIX	"/api/0/"
#define MONITOR_URI	"/api/0/monitor"

int ev_handler(struct mg_connection *conn, enum mg_event ev);
void http_ws_push_json(struct mg_server *server, JsonNode *obj);

#endif
