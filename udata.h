#ifndef UDATA_H_INCLUDED
# define UDATA_H_INCLUDED

#include "json.h"

#ifdef WITH_HTTP
# include "mongoose.h"
#endif
#ifdef WITH_LMDB
# include "gcache.h"
#endif


struct udata {
	JsonNode *topics;		/* Array of topics to subscribe to */
	int ignoreretained;		/* True if retained messages should be ignored */
	char *pubprefix;		/* If not NULL (default), republish modified payload to <pubprefix>/topic */
	int skipdemo;			/* True if _demo users are to be skipped */
	int revgeo;			/* True (default) if we should do reverse Geo lookups */
	int qos;			/* Subscribe QoS */
	int verbose;			/* TRUE if print verbose messages to stdout */
	int norec;			/* If TRUE, no .REC files are written to */
#ifdef WITH_LMDB
	struct gcache *gc;
	struct gcache *t2t;		/* topic to tid */
# ifdef WITH_RONLY
	struct gcache *ronlydb;		/* RONLY db */
# endif
#endif
#ifdef WITH_HTTP
	struct mg_server *mgserver;	/* Mongoose */
#endif
#ifdef WITH_LUA
	struct luadata *luadata;	/* Lua stuff */
# ifdef WITH_LMDB
	struct gcache *luadb;		/* lmdb named database 'luadb' */
# endif
#endif
#ifdef WITH_ENCRYPT
	struct gcache *keydb;		/* encryption keys */
#endif
	char *label;			/* Server label */
	char *geokey;			/* Google reverse-geo API key */
};

#endif
