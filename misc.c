/*
 * OwnTracks Recorder
 * Copyright (C) 2015-2020 Jan-Piet Mens <jpmens@gmail.com>
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
#include <libconfig.h>
#include "utstring.h"
#include "ctype.h"
#include "udata.h"
#include "misc.h"
#include "util.h"

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

	if (access(filename, R_OK) == -1) {
		olog(LOG_ERR, "Skipping open defaults file %s: %s", filename, strerror(errno));
		cf = NULL;
	} else {

		config_init(cf = &cfg);

		if (!config_read_file(cf, filename)) {
			olog(LOG_ERR, "Syntax error in %s:%d - %s",
				filename,
				config_error_line(cf),
				config_error_text(cf));
			config_destroy(cf);
			exit(2);
		}

		v = c_str(cf, "OTR_STORAGEDIR", STORAGEDEFAULT);
		strcpy(STORAGEDIR, v);
	}

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
	if (cf) {
		if ((v = c_str(cf, "OTR_TOPICS", NULL)) != NULL) {
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

	if (cf) {
		config_destroy(cf);
	}
}

static void d_int(char *property, int ival)
{
	printf("%-25s %d\n", property, ival);
}

static void d_str(char *property, char *val)
{
	printf("%-25s %s\n", property, val ? val : "<null>");
}

static void d_bool(char *property, bool tf)
{
	printf("%-25s %s\n", property, tf ? "true" : "false");
}

void display_variables(struct udata *ud)
{
	d_str("OTR_STORAGEDIR", STORAGEDIR);
#if WITH_MQTT
	// char *pubprefix;		/* If not NULL (default), republish modified payload to <pubprefix>/topic */

	d_str("OTR_HOST",	ud->hostname);
	d_int("OTR_PORT", 	ud->port);
	d_str("OTR_USER",	ud->username);
	d_str("OTR_PASS",	ud->password);
	d_int("OTR_QOS",	ud->qos);
	d_str("OTR_CLIENTID",	ud->clientid);
	d_str("OTR_CAFILE",	ud->cafile);
	d_str("OTR_CAPATH",	ud->capath);
	d_str("OTR_CERTFILE",	ud->certfile);
	d_str("OTR_KEYFILE",	ud->keyfile);
	d_str("OTR_IDENTITY",	ud->identity);
	d_str("OTR_PSK",	ud->psk);
#endif
	d_bool("skip _demo",	ud->skipdemo);
	d_bool("perform reverse geo",	ud->revgeo);
	d_str("OTR_GEOKEY",		ud->geokey);
	d_bool("do not write .rec",	ud->norec);

#ifdef WITH_HTTP
	d_str("OTR_HTTPHOST",		ud->http_host);
	d_int("OTR_HTTPPORT",		ud->http_port);
	d_str("OTR_HTTPLOGDIR",		ud->http_logdir);
	d_str("OTR_BROWSERAPIKEY",	ud->browser_apikey);
	d_str("OTR_VIEWSDIR",		ud->viewsdir);
# ifdef WITH_TOURS
	d_str("OTR_HTTPPREFIX",		ud->http_prefix);
# endif /* WITH_TOURS */
#endif
#ifdef WITH_LUA
	d_str("OTR_LUASCRIPT",		ud->luascript);
#endif
	d_str("OTR_SERVERLABEL",	ud->label);
}
