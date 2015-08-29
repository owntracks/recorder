#ifndef _HTTP_H_INCLUDED_
# define _HTTP_H_INCLUDED_

#include "mongoose.h"
#include "storage.h"

int ev_handler(struct mg_connection *conn, enum mg_event ev);

#endif
