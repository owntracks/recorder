#ifndef HOOKS_H_INCLUDED
# define HOOKS_H_INCLUDED

#ifdef WITH_LUA
# include <lua.h>


struct luadata {
	char *script;			/* Path to Lua script in --lua-script  */
	lua_State *L;			/* The Lua machine */
};

struct luadata *hooks_init(struct udata *ud, char *luascript);
void hooks_exit(struct luadata *, char *reason);
void hooks_hook(struct udata *ud, char *topic, JsonNode *obj);
int hooks_norec(struct udata *ud, char *user, char *device, char *payload);
JsonNode *hooks_http(struct udata *ud, char *user, char *device, char *payload);
void hooks_transition(struct udata *ud, char *user, char *device, int event, char *desc, double wplat, double wplon, double lat, double lon, char *topic, JsonNode *json, long meters);
JsonNode *hook_revgeo(struct udata *ud, char *luafunc, char *topic, char *user, char *device, double lat, double lon);

#endif /* WITH_LUA */

#endif
