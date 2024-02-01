
/*
 * OwnTracks Recorder
 * Copyright (C) 2015-2024 Jan-Piet Mens <jpmens@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef WITH_LUA
# include <stdarg.h>
# include <stdint.h>
# include "util.h"
# include "udata.h"
# include "hooks.h"
# include <lua.h>
# include <lualib.h>
# include <lauxlib.h>
# include "fences.h"
# include "gcache.h"
# include "json.h"
# include "version.h"
# include "fences.h"

static int otr_log(lua_State *lua);
static int otr_strftime(lua_State *lua);
static int otr_putdb(lua_State *lua);
static int otr_getdb(lua_State *lua);
#ifdef WITH_MQTT
# include <mosquitto.h>
static int otr_publish(lua_State *lua);
static struct mosquitto *MQTTconn = NULL;
#endif

static struct gcache *LuaDB = NULL;


/*
 * Invoke the function `name' in the Lua script, which _may_ return
 * an integer that we, in turn, return to caller.
 */

static int l_function(lua_State *L, char *name)
{
	int rc = 0;

	lua_getglobal(L, name);
	if (lua_type(L, -1) != LUA_TFUNCTION) {
		olog(LOG_ERR, "Cannot invoke Lua function %s: %s\n", name, lua_tostring(L, -1));
		rc = 1;
	} else {
		lua_call(L, 0, 1);
		rc = (int)lua_tonumber(L, -1);
		lua_pop(L, 1);
	}
	lua_settop(L, 0);
	return (rc);
}

struct luadata *hooks_init(struct udata *ud, char *script)
{
	struct luadata *ld;
	int rc;

	if ((ld = malloc(sizeof(struct luadata))) == NULL)
		return (NULL);

	ld->script = strdup(script);
	// ld->L = lua_open();
	ld->L = luaL_newstate();
	luaL_openlibs(ld->L);

#ifdef WITH_MQTT
	MQTTconn = ud->mosq;
#endif

	/*
	 * Set up a global with some values.
	 */

	lua_newtable(ld->L);
		lua_pushstring(ld->L, VERSION);
		lua_setfield(ld->L, -2, "version");

		// lua_pushstring(ld->L, "/Users/jpm/Auto/projects/on-github/owntracks/recorder/lua");
		// lua_setfield(ld->L, -2, "luapath");

		lua_pushcfunction(ld->L, otr_log);
		lua_setfield(ld->L, -2, "log");

		lua_pushcfunction(ld->L, otr_strftime);
		lua_setfield(ld->L, -2, "strftime");

		lua_pushcfunction(ld->L, otr_putdb);
		lua_setfield(ld->L, -2, "putdb");

		lua_pushcfunction(ld->L, otr_getdb);
		lua_setfield(ld->L, -2, "getdb");

#ifdef WITH_MQTT
		lua_pushcfunction(ld->L, otr_publish);
		lua_setfield(ld->L, -2, "publish");
#endif

	lua_setglobal(ld->L, "otr");

	LuaDB = ud->luadb;

	olog(LOG_DEBUG, "initializing Lua hooks from `%s'", script);

	/* Load the Lua script */
	if (luaL_dofile(ld->L, ld->script)) {
		olog(LOG_ERR, "Cannot load Lua from %s: %s", ld->script, lua_tostring(ld->L, -1));
		hooks_exit(ld, "failed to load script");
		return (NULL);
	}

	rc = l_function(ld->L, "otr_init");
	if (rc != 0) {

		/*
		 * After all this work (sigh), the Script has decided we shouldn't use
		 * hooks, so unload the whole Lua stuff and NULL back.
		 */

		hooks_exit(ld, "otr_init() returned non-zero");
		ld = NULL;
	}

	return (ld);
}


void hooks_exit(struct luadata *ld, char *reason)
{
	if (ld && ld->script) {
		l_function(ld->L, "otr_exit");
		olog(LOG_DEBUG, "unloading Lua: %s", reason);
		free(ld->script);
		lua_close(ld->L);
		free(ld);
	}
}

static void do_hook(char *hookname, struct udata *ud, char *topic, JsonNode *fullo)
{
	struct luadata *ld = ud->luadata;
	char *_type = "unknown";
	JsonNode *j;

	if (!ld || !ld->script)
		return;

	lua_settop(ld->L, 0);
	lua_getglobal(ld->L, hookname);
	if (lua_type(ld->L, -1) != LUA_TFUNCTION) {
		olog(LOG_NOTICE, "cannot invoke %s in Lua script", hookname);
		return;
	}

	lua_pushstring(ld->L, topic);			/* arg1: topic */

	if ((j = json_find_member(fullo, "_type")) != NULL) {
		if (j->tag == JSON_STRING)
			_type = j->string_;
	}

	lua_pushstring(ld->L, _type);			/* arg2: record type */

	lua_newtable(ld->L);				/* arg3: table */
	json_foreach(j, fullo) {
		if (j->tag >= JSON_ARRAY)
			continue;
		lua_pushstring(ld->L, j->key);		/* table key */
		if (j->tag == JSON_STRING) {
			lua_pushstring(ld->L, j->string_);
		} else if (j->tag == JSON_NUMBER) {
			lua_pushnumber(ld->L, j->number_);
		} else if (j->tag == JSON_NULL) {
			lua_pushnil(ld->L);
		} else if (j->tag == JSON_BOOL) {
			lua_pushboolean(ld->L, j->bool_);
		}
		lua_rawset(ld->L, -3);

	}

	/* Invoke `hook' function in Lua with our args */
	if (lua_pcall(ld->L, 3, 1, 0)) {
		olog(LOG_ERR, "Failed to run script: %s", lua_tostring(ld->L, -1));
		exit(1);
	}

	// rc = (int)lua_tonumber(ld->L, -1);
	// printf("C: FILTER returns %d\n", rc);
}

static void hooks_hooklet(struct udata *ud, char *topic, JsonNode *fullo)
{
	JsonNode *j;
	struct luadata *ld = ud->luadata;
	char hookname[BUFSIZ];

	if (!ud->luascript)
		return;

	json_foreach(j, fullo) {
		snprintf(hookname, sizeof(hookname), "hooklet_%s", j->key);
		lua_getglobal(ld->L, hookname);
		if (lua_type(ld->L, -1) == LUA_TFUNCTION) {
			do_hook(hookname, ud, topic, fullo);
		}
	}
}

/*
 * Invoked from putrec() in storage. If the Lua putrec function is available
 * and it returns non-zero, do not putrec.
 */

int hooks_norec(struct udata *ud, char *user, char *device, char *payload)
{
	struct luadata *ld = ud->luadata;
	int rc;

	if (ld == NULL || !ld->script)
		return (0);

	lua_settop(ld->L, 0);
	lua_getglobal(ld->L, "otr_putrec");
	if (lua_type(ld->L, -1) != LUA_TFUNCTION) {
		return (0);
	}

	lua_pushstring(ld->L, user);
	lua_pushstring(ld->L, device);
	lua_pushstring(ld->L, payload);

	/* Invoke Lua function with our args */
	if (lua_pcall(ld->L, 3, 1, 0)) {
		olog(LOG_ERR, "Failed to run putrec in Lua: %s", lua_tostring(ld->L, -1));
		exit(1);
	}

	rc = (int)lua_tonumber(ld->L, -1);
	return (rc);
}


/*
 * Invoked in http to populate response FIXME
 */

JsonNode *hooks_http(struct udata *ud, char *user, char *device, char *payload)
{
	struct luadata *ld = ud->luadata;
	char *_type = "unknown";
	JsonNode *obj = NULL, *j, *fullo;

	debug(ud, "in hooks_http()");
	if (ld == NULL || !ld->script)
		return (0);

	fullo = json_decode(payload);

	lua_settop(ld->L, 0);
	lua_getglobal(ld->L, "otr_httpobject");
	if (lua_type(ld->L, -1) != LUA_TFUNCTION) {
		debug(ud, "no otr_httpobject function in Lua file: returning");
		return (0);
	}

	lua_pushstring(ld->L, user);			/* arg1 */
	lua_pushstring(ld->L, device);			/* arg2 */


	if ((j = json_find_member(fullo, "_type")) != NULL) {
		if (j->tag == JSON_STRING)
			_type = j->string_;
	}

	lua_pushstring(ld->L, _type);			/* arg3: record type */

	lua_newtable(ld->L);				/* arg4: table */
	json_foreach(j, fullo) {
		if (j->tag >= JSON_ARRAY)
			continue;
		lua_pushstring(ld->L, j->key);		/* table key */
		if (j->tag == JSON_STRING) {
			lua_pushstring(ld->L, j->string_);
		} else if (j->tag == JSON_NUMBER) {
			lua_pushnumber(ld->L, j->number_);
		} else if (j->tag == JSON_NULL) {
			lua_pushnil(ld->L);
		} else if (j->tag == JSON_BOOL) {
			lua_pushboolean(ld->L, j->bool_);
		}
		lua_rawset(ld->L, -3);

	}


	// lua_pushstring(ld->L, payload);

	/* Invoke Lua function with our args */
	/* return value is a TABLE; all else is ignored */
	if (lua_pcall(ld->L, 4, 1, 0)) {
		olog(LOG_ERR, "Failed to run hooks_http in Lua: %s", lua_tostring(ld->L, -1));
		exit(1);
	}

	/* Verify we have a table and create a JSON object from it. */

	if (lua_istable(ld->L, -1)) {
		obj = json_mkobject();

		int t = -2;

		lua_pushnil(ld->L);		/* first key */
		while (lua_next(ld->L, t) != 0) {
			const char *key, *val;
			size_t len;
			int type, bf, nil;
			lua_Number d;

			key = lua_tolstring(ld->L, -2, &len);
			type = lua_type(ld->L, -1);

			// printf("%s len=%zd vtype=%d\n", key, len, type);

			switch (type) {
				case LUA_TNUMBER:
					d = lua_tonumber(ld->L, -1);
					json_append_member(obj, key, json_mknumber(d));
					break;
				case LUA_TSTRING:
					val = lua_tostring(ld->L, -1);
					json_append_member(obj, key, json_mkstring(val));
					break;
				case LUA_TNIL:
					nil = lua_isnil(ld->L, -1);
					if (nil)
						json_append_member(obj, key, json_mknull());
					break;
				case LUA_TBOOLEAN:
					bf = lua_toboolean(ld->L, -1);
					json_append_member(obj, key, json_mkbool(bf));
					break;
				default:
					/* unsupported */
					break;
			}

			lua_pop(ld->L, 1);
		}
	}
	lua_settop(ld->L, 0);

	return (obj);
}

void hooks_hook(struct udata *ud, char *topic, JsonNode *fullo)
{
	do_hook("otr_hook", ud, topic, fullo);
	hooks_hooklet(ud, topic, fullo);
}

/*
 * This hook is invoked through fences.c when we determine that a movement
 * into or out of a geofence has caused a transition.
 * json is the original JSON we received enhanced with stuff from recorder.
 */

void hooks_transition(struct udata *ud, char *user, char *device, int event, char *desc, double wplat, double wplon, double lat, double lon, char *topic, JsonNode *json, long meters)
{
	JsonNode *j;

	if ((j = json_find_member(json, "_type")) != NULL) {
		json_delete(j);
	}
	json_append_member(json, "_type", json_mkstring("transition"));
	json_append_member(json, "event",
		event == ENTER ? json_mkstring("enter") : json_mkstring("leave"));
	json_append_member(json, "desc", json_mkstring(desc));
	json_append_member(json, "wplat", json_mknumber(wplat));
	json_append_member(json, "wplon", json_mknumber(wplon));
	json_append_member(json, "dist", json_mknumber(meters));

	olog(LOG_DEBUG, "**** Lua hook for %s %s\n",
		event == ENTER ? "ENTER" : "LEAVE", desc);


	do_hook("otr_transition", ud, topic, json);
}

/*
 * If the Lua function otr_revgeo() is defined, invoke that to obtain a result
 * of reverse geocoding. The function name proper (otr_revgeo) is contained in
 * `luafunc'
 */

JsonNode *hook_revgeo(struct udata *ud, char *luafunc, char *topic, char *user, char *device, double lat, double lon)
{
	struct luadata *ld = ud->luadata;
	JsonNode *obj = NULL;

	debug(ud, "in hook_revgeo()");
	if (ld == NULL || !ld->script)
		return (0);

	lua_settop(ld->L, 0);
	lua_getglobal(ld->L, luafunc);
	if (lua_type(ld->L, -1) != LUA_TFUNCTION) {
		debug(ud, "no %s function in Lua file: returning", luafunc);
		return (0);
	}

	lua_pushstring(ld->L, topic);			/* arg1 */
	lua_pushstring(ld->L, user);			/* arg2 */
	lua_pushstring(ld->L, device);			/* arg3 */
	lua_pushnumber(ld->L, lat);			/* arg4 */
	lua_pushnumber(ld->L, lon);			/* arg5 */
	
	/* Invoke Lua function with our args */
	/* return value is a TABLE; all else is ignored */
	if (lua_pcall(ld->L, 5, 1, 0)) {
		olog(LOG_ERR, "Failed to run hook_revgeo in Lua: %s", lua_tostring(ld->L, -1));
		exit(1);
	}

	/* Verify we have a table and create a JSON object from it. */

	if (lua_istable(ld->L, -1)) {
		obj = json_mkobject();

		int t = -2;

		lua_pushnil(ld->L);		/* first key */
		while (lua_next(ld->L, t) != 0) {
			const char *key, *val;
			size_t len;
			int type, bf, nil;
			lua_Number d;

			key = lua_tolstring(ld->L, -2, &len);
			type = lua_type(ld->L, -1);

			// printf("%s len=%zd vtype=%d\n", key, len, type);

			switch (type) {
				case LUA_TNUMBER:
					d = lua_tonumber(ld->L, -1);
					json_append_member(obj, key, json_mknumber(d));
					break;
				case LUA_TSTRING:
					val = lua_tostring(ld->L, -1);
					json_append_member(obj, key, json_mkstring(val));
					break;
				case LUA_TNIL:
					nil = lua_isnil(ld->L, -1);
					if (nil)
						json_append_member(obj, key, json_mknull());
					break;
				case LUA_TBOOLEAN:
					bf = lua_toboolean(ld->L, -1);
					json_append_member(obj, key, json_mkbool(bf));
					break;
				default:
					/* unsupported */
					break;
			}

			lua_pop(ld->L, 1);
		}
	}
	lua_settop(ld->L, 0);
	return (obj);
}


/*
 * --- Here come the functions we provide to Lua scripts.
 */

static int otr_log(lua_State *lua)
{
	const char *str;

	if (lua_gettop(lua) >= 1) {
		str =  lua_tostring(lua, 1);
		olog(LOG_INFO, "%s", str);
		lua_pop(lua, 1);
	}
	return 0;
}

/*
 * otr.strftime(format, seconds)
 * Perform a strtime(3) for Lua with the specified format and
 * seconds, and return the string result to Lua. As a special
 * case, if `seconds' is negative, use current time.
 */

static int otr_strftime(lua_State *lua)
{
	const char *fmt;
	time_t secs;
	struct tm *tm;
	char buf[BUFSIZ];

	if (lua_gettop(lua) >= 1) {
		fmt =  lua_tostring(lua, 1);
		if ((secs =  (time_t)lua_tonumber(lua, 2)) < 1)
			secs = time(0);

		if ((tm = gmtime(&secs)) != NULL) {
			strftime(buf, sizeof(buf), fmt, tm);

			lua_pushstring(lua, buf);
			return (1);
		}
	}
	return (0);
}

/*
 * Requires two string arguments: key, value
 * These are written into the named LMDB database
 * called `luadb'.
 */

static int otr_putdb(lua_State *lua)
{
	const char *key, *value;
	int rc = 0;

	if (lua_gettop(lua) >= 1) {
		key =  lua_tostring(lua, 1);
		value =  lua_tostring(lua, 2);

		rc = gcache_put(LuaDB, (char *)key, (char *)value);
		// olog(LOG_DEBUG, "LUA_PUT (%s, %s) == %d\n", key, value, rc);
	}
	return (rc);
}

static int otr_getdb(lua_State *lua)
{
	char buf[BUFSIZ];
	const char *key;
	int rc = 0, blen;

	if (lua_gettop(lua) >= 1) {
		key =  lua_tostring(lua, 1);

		blen = gcache_get(LuaDB, (char *)key, buf, sizeof(buf));
		if (blen < 0)
			memset(buf, 0, sizeof(buf));
		// printf("K=[%s], blen=%d\n", key, blen);
		lua_pushstring(lua, buf);
		rc = 1;
	}
	return (rc);
}

#ifdef WITH_MQTT

/*
 * Requires two string arguments: topic, payload
 * and two numeric args: qos and retain
 * Will be published via MQTT to the Recorder's
 * open connection.
 */

int otr_publish(lua_State *lua)
{
	const char *topic, *payload;
	int qos = 0, retain = 0;
	int rc = 0;

	if (lua_gettop(lua) >= 1) {
		topic =  lua_tostring(lua, 1);
		payload =  lua_tostring(lua, 2);
		qos =  lua_tonumber(lua, 3);
		retain =  lua_tonumber(lua, 4);

		if (MQTTconn == NULL) {
			olog(LOG_WARNING, "otr_publish(%s, %s, %d, %d): NULL MQTT connection\n",
				topic, payload, qos, retain);
		} else {
			rc = mosquitto_publish(MQTTconn, NULL, topic,
		                                strlen(payload), payload, qos, retain);
			olog(LOG_DEBUG, "otr_publish(%s, %s, %d, %d) == %d\n", topic, payload, qos, retain, rc);
		}
	}
	return (rc);
}
#endif

#endif /* WITH_LUA */
