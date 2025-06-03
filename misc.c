/*
 * OwnTracks Recorder
 * Copyright (C) 2015-2025 Jan-Piet Mens <jpmens@gmail.com>
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
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <libconfig.h>
#include "utstring.h"
#include "ctype.h"
#include "udata.h"
#include "misc.h"
#include "util.h"
#include "json.h"
#include "version.h"

char *bindump(char *buf, long buflen)
{
	static UT_string *out = NULL;
	int i, ch;

	utstring_renew(out);

	for (i = 0; i < buflen; i++) {
		ch = buf[i];
		if (isprint(ch)) {
			utstring_printf(out, "%c", ch);
		} else {
			utstring_printf(out, " %02X ", ch & 0xFF);
		}
	}
	return (UB(out));
}

/*
 * At each received message, the recorder invokes this function with the
 * current epoch time and the topic being handled. Use this to update
 * a monitoring hoook. If we have Redis, use that exclusively.
 */

void monitorhook(struct udata *userdata, time_t now, char *topic)
{
	// struct udata *ud = (struct udata *)userdata;

	/* TODO: add monitor hook to a "monitor" key in LMDB ? */

	char mpath[BUFSIZ];
	static UT_string *us = NULL;

	utstring_renew(us);
	utstring_printf(us, "%ld %s\n", (long)now, topic);

	snprintf(mpath, sizeof(mpath), "%s/monitor", STORAGEDIR);
	safewrite(mpath, UB(us));
}

/*
 * Return a pointer to a static area containing the last monitor entry or NULL.
 */

char *monitor_get()
{
	char mpath[BUFSIZ];
	static char monitorline[BUFSIZ], *ret = NULL;
	FILE *fp;

	snprintf(mpath, sizeof(mpath), "%s/monitor", STORAGEDIR);

	if ((fp = fopen(mpath, "r")) != NULL) {
		if (fgets(monitorline, sizeof(monitorline), fp) != NULL) {
			char *bp = strchr(monitorline, '\n');

			if (bp)
				*bp = 0;
			ret = monitorline;
		}
		fclose(fp);
	}

	return (ret);
}

/*
 * Set the value of configuration `property' to the optional default
 * at `def', then try to find it in the open config file at `cf',
 * and finally the environment. Return a new copy.
 * `cf' is NULL if the config file couldn't be read, in which case
 * we check environment only.
 */

static char *c_str(config_t *cf, char *property, char *def)
{
	const char *value;
	char *val = def, *p;

	if (cf) {
		if (config_lookup_string(cf, property, &value) != CONFIG_FALSE) {
			val = (char *)value;
		}
	}

	if ((p = getenv(property)) != NULL) {
		val = p;
	}
	return val ? strdup(val) : val;
}

static int c_int(config_t *cf, char *property, int def)
{
#if LIBCONFIG_VER_MAJOR == 1
# if LIBCONFIG_VER_MINOR >= 4
	int ival;
# endif
# else
	long ival;
#endif

	int val = def;
	char *p;

	if (cf) {
		if (config_lookup_int(cf, property, &ival) != CONFIG_FALSE) {
			val = (int)ival;
		}
	}

	if ((p = getenv(property)) != NULL) {
		val = atol(p);
	}
	return val;
}

/*
 * Fill in some defaults
 */

void get_defaults(char *filename, struct udata *ud)
{
	config_t cfg, *cf = NULL;
	char *v;
	int iv;
	struct stat sb;

	if (stat(filename, &sb) != -1) {
		config_init(cf = &cfg);

		if (config_read_file(cf, filename) == CONFIG_FALSE) {
			fprintf(stderr, "Syntax error in %s:%d - %s\n",
				filename,
				config_error_line(cf),
				config_error_text(cf));
			olog(LOG_ERR, "Syntax error in %s:%d - %s",
				filename,
				config_error_line(cf),
				config_error_text(cf));
			config_destroy(cf);
			exit(2);
		}
	}

	v = c_str(cf, "OTR_STORAGEDIR", STORAGEDEFAULT);
	strcpy(STORAGEDIR, v);

	if (ud == NULL) {
		/* being invoked by ocat; return */
		return;
	}

#if WITH_MQTT

	ud->hostname		= c_str(cf, "OTR_HOST", "localhost");
	ud->username		= c_str(cf, "OTR_USER", NULL);
	ud->password		= c_str(cf, "OTR_PASS", NULL);
	ud->clientid		= c_str(cf, "OTR_CLIENTID", ud->clientid);
	ud->capath		= c_str(cf, "OTR_CAPATH", NULL);
	ud->cafile		= c_str(cf, "OTR_CAFILE", NULL);
	ud->certfile		= c_str(cf, "OTR_CERTFILE", NULL);
	ud->keyfile		= c_str(cf, "OTR_KEYFILE", NULL);
	ud->identity		= c_str(cf, "OTR_IDENTITY", ud->identity);
	ud->psk			= c_str(cf, "OTR_PSK", ud->psk);

	ud->port 		= c_int(cf, "OTR_PORT", 1883);
	ud->qos 		= c_int(cf, "OTR_QOS", ud->qos);


	/* Topics is a blank-separated string of words; split and add to JSON array */
	v 			= c_str(cf, "OTR_TOPICS", NULL);
	if (v && *v) {
		// if ((v = c_str(cf, "OTR_TOPICS", NULL)) != NULL) {
			char *parts[40];
			int np, n;
			if (ud->topics) json_delete(ud->topics);

			if ((np = splitter((char *)v, " ", parts)) < 1) {
				olog(LOG_ERR, "Illegal value in OTR_TOPICS");
				exit(2);
			}
			ud->topics = json_mkarray();

			for (n = 0; n < np; n++) {
				json_append_element(ud->topics, json_mkstring(parts[n]));
			}
			splitterfree(parts);
	}
#endif /* WITH_MQTT */

	ud->geokey		= c_str(cf, "OTR_GEOKEY", NULL);
	iv			= c_int(cf, "OTR_PRECISION", GHASHPREC);
	geohash_setprec(iv);

#if WITH_HTTP
	ud->http_host		= c_str(cf, "OTR_HTTPHOST", NULL);
	ud->http_logdir		= c_str(cf, "OTR_HTTPLOGDIR", NULL);
	ud->browser_apikey	= c_str(cf, "OTR_BROWSERAPIKEY", NULL);
	ud->viewsdir		= c_str(cf, "OTR_VIEWSDIR", ud->viewsdir);

	ud->http_port		= c_int(cf, "OTR_HTTPPORT", ud->http_port);

#ifdef WITH_TOURS
	ud->http_prefix		= c_str(cf, "OTR_HTTPPREFIX", NULL);
# endif /* WITH_TOURS */
#endif /* WITH_HTTP */

#if WITH_LUA
	ud->luascript		= c_str(cf, "OTR_LUASCRIPT", NULL);
#endif
	ud->label		= c_str(cf, "OTR_SERVERLABEL", ud->label);
	ud->clean_age		= c_int(cf, "OTR_CLEAN_AGE", ud->clean_age);

	if (cf) {
		config_destroy(cf);
	}
}

static void j_int(JsonNode *j, char *property, int ival)
{
	json_append_member(j, property, json_mknumber(ival));
}

static void j_str(JsonNode *j, char *property, char *val)
{
	if (val && *val) {
		json_append_member(j, property, json_mkstring(val));
	} else {
		json_append_member(j, property, json_mknull());
	}
}

static void j_bool(JsonNode *j, char *property, bool tf)
{
	json_append_member(j, property, json_mkbool(tf));
}

void display_json_variables(struct udata *ud, bool plain)
{
	JsonNode *json = json_mkobject();
	char *js;

	j_str(json, "VERSION", VERSION);
	j_str(json, "CONFIGFILE", CONFIGFILE);
	j_str(json, "OTR_STORAGEDIR", STORAGEDIR);
#if WITH_MQTT
	// char *pubprefix;		/* If not NULL (default), republish modified payload to <pubprefix>/topic */

	j_str(json, "OTR_HOST",	ud->hostname);
	j_int(json, "OTR_PORT", 	ud->port);
	j_str(json, "OTR_USER",	ud->username);
	j_str(json, "OTR_PASS",	ud->password);
	j_int(json, "OTR_QOS",	ud->qos);
	j_str(json, "OTR_CLIENTID",	ud->clientid);
	j_str(json, "OTR_CAFILE",	ud->cafile);
	j_str(json, "OTR_CAPATH",	ud->capath);
	j_str(json, "OTR_CERTFILE",	ud->certfile);
	j_str(json, "OTR_KEYFILE",	ud->keyfile);
	j_str(json, "OTR_IDENTITY",	ud->identity);
	j_str(json, "OTR_PSK",	ud->psk);
#endif
	j_bool(json, "skip_demo",	ud->skipdemo);
	j_bool(json, "perform_reverse_geo",	ud->revgeo);
	j_str(json, "OTR_GEOKEY",		ud->geokey);
	j_int(json, "OTR_PRECISION", 		geohash_prec());
	j_bool(json, "do_not_write_.rec",	ud->norec);

#ifdef WITH_HTTP
	j_str(json, "OTR_HTTPHOST",		ud->http_host);
	j_int(json, "OTR_HTTPPORT",		ud->http_port);
	j_str(json, "OTR_HTTPLOGDIR",		ud->http_logdir);
	j_str(json, "OTR_BROWSERAPIKEY",	ud->browser_apikey);
	j_str(json, "OTR_VIEWSDIR",		ud->viewsdir);
# ifdef WITH_TOURS
	j_str(json, "OTR_HTTPPREFIX",		ud->http_prefix);
# endif /* WITH_TOURS */
#endif
#ifdef WITH_LUA
	j_str(json, "OTR_LUASCRIPT",		ud->luascript);
#endif
	j_str(json, "OTR_SERVERLABEL",	ud->label);
	j_int(json, "OTR_CLEAN_AGE",		ud->clean_age);
#ifdef WITH_TZ
	j_str(json, "TZDATADB",		TZDATADB);
#endif

	/* WITH_ flags */
#ifdef WITH_ENCRYPT
	j_bool(json, "WITH_ENCRYPT", true);
#else
	j_bool(json, "WITH_ENCRYPT", false);
#endif
#ifdef WITH_HTTP
	j_bool(json, "WITH_HTTP", true);
#else
	j_bool(json, "WITH_HTTP", false);
#endif
#ifdef WITH_KILL
	j_bool(json, "WITH_KILL", true);
#else
	j_bool(json, "WITH_KILL", false);
#endif
#ifdef WITH_LUA
	j_bool(json, "WITH_LUA", true);
#else
	j_bool(json, "WITH_LUA", false);
#endif
#ifdef WITH_MQTT
	j_bool(json, "WITH_MQTT", true);
#else
	j_bool(json, "WITH_MQTT", false);
#endif
#ifdef WITH_TOURS
	j_bool(json, "WITH_TOURS", true);
#else
	j_bool(json, "WITH_TOURS", false);
#endif
#ifdef WITH_PING
	j_bool(json, "WITH_PING", true);
#else
	j_bool(json, "WITH_PING", false);
#endif
#ifdef WITH_TZ
	j_bool(json, "WITH_TZ", true);
#else
	j_bool(json, "WITH_TZ", false);
#endif

	if (plain) {
		JsonNode *j;

		json_foreach(j, json) {
			printf("%-25s", j->key);
			switch (j->tag) {
				case JSON_NULL:
					printf("<null>\n");
					break;
				case JSON_BOOL:
					printf("%s\n", j->bool_ ? "true" : "false");
					break;
				case JSON_STRING:
					printf("%s\n", j->string_);
					break;
				case JSON_NUMBER:
					printf("%.0lf\n", j->number_);
					break;
				case JSON_ARRAY:
				case JSON_OBJECT:
					break;
			}
		}
	} else {
		if ((js = json_stringify(json, "  ")) != NULL) {
			printf("%s\n", js);
			free(js);
		}
	}
	json_delete(json);

}
