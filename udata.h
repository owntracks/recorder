#ifndef UDATA_H_INCLUDED
# define UDATA_H_INCLUDED

#include "json.h"

#ifdef WITH_HTTP
# include <stdarg.h>
# include "mongoose.h"
#endif
// #include "gcache.h"


struct udata {
	JsonNode *topics;		/* Array of topics to subscribe to */
	int ignoreretained;		/* True if retained messages should be ignored */
#if WITH_MQTT
	struct mosquitto *mosq;		/* MQTT connection */
	char *pubprefix;		/* If not NULL (default), republish modified payload to <pubprefix>/topic */
	int qos;			/* Subscribe QoS */
	char *hostname;			/* MQTT broker */
	int port;			/* MQTT port */
	char *username;			/* MQTT user */
	char *password;			/* MQTT password */
	char *clientid;			/* MQTT clientid */
	char *cafile;			/* path to CA PEM for MQTT */
	char *capath;			/* CA path */
	char *certfile;			/* certificate (client) */
	char *keyfile;			/* client key */
	char *identity;			/* PSK identity (hint) */
	char *psk;			/* PSK */
#endif
	int skipdemo;			/* True if _demo users are to be skipped */
	int revgeo;			/* True (default) if we should do reverse Geo lookups */
	int verbose;			/* TRUE if print verbose messages to stdout */
	int norec;			/* If TRUE, no .REC files are written to */
	struct gcache *gc;
	struct gcache *t2t;		/* topic to tid */
#ifdef WITH_HTTP
	struct mg_server *mgserver;	/* Mongoose */
	char *http_host;		/* address of http bind */
	int http_port;			/* port number for above */
	char *http_logdir;		/* full path to http access log */
	char *browser_apikey;		/* Google maps browser API key */
	char *viewsdir;			/* path to views directory */
#endif
#ifdef WITH_LUA
	char *luascript;		/* Path to Lua script */
	struct luadata *luadata;	/* Lua stuff */
	struct gcache *luadb;		/* lmdb named database 'luadb' */
#endif
#ifdef WITH_ENCRYPT
	struct gcache *keydb;		/* encryption keys */
#endif
	char *label;			/* Server label */
	char *geokey;			/* reverse-geo API key */
	int debug;			/* enable for debugging */
	struct gcache *httpfriends;	/* lmdb named database 'friends' */
	struct gcache *wpdb;		/* lmdb named database 'wp' (waypoints) */
};

#endif
