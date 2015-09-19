#ifndef UDATA_H_INCLUDED
# define UDATA_H_INCLUDED

#include "json.h"

#ifdef HAVE_HTTP
# include "mongoose.h"
#endif
#ifdef HAVE_LMDB
# include "gcache.h"
#endif


struct udata {
	JsonNode *topics;		/* Array of topics to subscribe to */
	int ignoreretained;		/* True if retained messages should be ignored */
	char *pubprefix;		/* If not NULL (default), republish modified payload to <pubprefix>/topic */
	int skipdemo;			/* True if _demo users are to be skipped */
	int revgeo;			/* True (default) if we should do reverse Geo lookups */
	int qos;			/* Subscribe QoS */
#ifdef HAVE_LMDB
	struct gcache *gc;
#endif
#ifdef HAVE_HTTP
	struct mg_server *mgserver;	/* Mongoose */
#endif
#ifdef WITH_LUA
	struct luadata *luadata;	/* Lua stuff */
#endif
};

#endif
