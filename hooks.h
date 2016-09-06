#ifndef HOOKS_H_INCLUDED
# define HOOKS_H_INCLUDED

#ifdef WITH_LUA
# include <lua.h>
#  ifdef WITH_MQTT
#   include <mosquitto.h>
#  endif


struct luadata {
	char *script;			/* Path to Lua script in --lua-script  */
	lua_State *L;			/* The Lua machine */
};

struct luadata *hooks_init(struct udata *ud, char *luascript);
void hooks_exit(struct luadata *, char *reason);
void hooks_hook(struct udata *ud, char *topic, JsonNode *obj);
int hooks_norec(struct udata *ud, char *user, char *device, char *payload);
JsonNode *hooks_http(struct udata *ud, char *user, char *device, char *payload);

#ifdef WITH_MQTT
# include <mosquitto.h>
void hooks_setmosq(struct mosquitto *);
#endif

#endif /* WITH_LUA */

#endif
