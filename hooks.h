#ifndef HOOKS_H_INCLUDED
# define HOOKS_H_INCLUDED

#ifdef WITH_LUA
# include <lua.h>


struct luadata {
	char *script;			/* Path to Lua script in --lua-script  */
	lua_State *L;			/* The Lua machine */
};

struct luadata *hooks_init(char *luascript);
void hooks_exit(struct luadata *, char *reason);
void hooks_hook(struct udata *ud, char *topic, JsonNode *obj);

#endif /* WITH_LUA */

#endif
