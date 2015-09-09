#ifndef _HTTP_H_INCLUDED_
# define _HTTP_H_INCLUDED_

#include "mongoose.h"
#include "storage.h"

#define API_PREFIX	"/api/0/"
#define MONITOR_URI	"/api/0/monitor"

int ev_handler(struct mg_connection *conn, enum mg_event ev);
void http_ws_push(struct mg_server *server, char *text);

#endif
