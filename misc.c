/*
 * OwnTracks Recorder
 * Copyright (C) 2015-2016 Jan-Piet Mens <jpmens@gmail.com>
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
	return UB(out);
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

	return ret;
}

/*
 * Fill in some defaults
 */

void get_defaults(char *filename, struct udata *ud)
{
	config_t cfg, *cf;
	const char *value;
#if LIBCONFIG_VER_MAJOR == 1
# if LIBCONFIG_VER_MINOR >= 4
	int ival;
# endif
# else
	long ival;
#endif

	if (access(filename, R_OK) == -1) {
		olog(LOG_ERR, "Cannot read defaults from %s: %s", filename, strerror(errno));
		return;
	}

	config_init(cf = &cfg);

	if (!config_read_file(cf, filename)) {
		olog(LOG_ERR, "Syntax error in %s:%d - %s",
			filename,
			config_error_line(cf),
			config_error_text(cf));
		config_destroy(cf);
		exit(2);
	}

	if (config_lookup_string(cf, "OTR_STORAGEDIR", &value) != CONFIG_FALSE)
		strcpy(STORAGEDIR, value);

	if (ud == NULL) {
		/* being invoked by ocat; return */
		return;
	}
#if WITH_MQTT
	if (config_lookup_string(cf, "OTR_HOST", &value) != CONFIG_FALSE) {
		if (ud->hostname) free(ud->hostname);
		ud->hostname = (value) ? strdup(value) : NULL;
	}
	if (config_lookup_int(cf, "OTR_PORT", &ival) != CONFIG_FALSE) {
		ud->port = ival;
	}
	if (config_lookup_string(cf, "OTR_USER", &value) != CONFIG_FALSE) {
		if (ud->username) free(ud->username);
		ud->username = (value) ? strdup(value) : NULL;
	}
	if (config_lookup_string(cf, "OTR_PASS", &value) != CONFIG_FALSE) {
		if (ud->password) free(ud->password);
		ud->password = (value) ? strdup(value) : NULL;
	}
	if (config_lookup_int(cf, "OTR_QOS", &ival) != CONFIG_FALSE) {
		ud->qos = ival;
	}
	if (config_lookup_string(cf, "OTR_CLIENTID", &value) != CONFIG_FALSE) {
		if (ud->clientid) free(ud->clientid);
		ud->clientid = (value) ? strdup(value) : NULL;
	}

	if (config_lookup_string(cf, "OTR_CAFILE", &value) != CONFIG_FALSE) {
		if (ud->cafile) free(ud->cafile);
		ud->cafile = (value) ? strdup(value) : NULL;
	}

	if (config_lookup_string(cf, "OTR_KEYFILE", &value) != CONFIG_FALSE) {
		if (ud->keyfile) free(ud->keyfile);
		ud->keyfile = (value) ? strdup(value) : NULL;
	}

	if (config_lookup_string(cf, "OTR_CERTFILE", &value) != CONFIG_FALSE) {
		if (ud->certfile) free(ud->certfile);
		ud->certfile = (value) ? strdup(value) : NULL;
	}

	/* Topics is a blank-separated string of words; split and add to JSON array */
	if (config_lookup_string(cf, "OTR_TOPICS", &value) != CONFIG_FALSE) {
		char *parts[40];
		int np, n;
		if (ud->topics) json_delete(ud->topics);

		if ((np = splitter((char *)value, " ", parts)) < 1) {
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

	if (config_lookup_string(cf, "OTR_GEOKEY", &value) != CONFIG_FALSE) {
		if (ud->geokey) free(ud->geokey);
		ud->geokey = (value) ? strdup(value) : NULL;
	}

	if (config_lookup_int(cf, "OTR_PRECISION", &ival) != CONFIG_FALSE) {
		geohash_setprec(ival);
	}

#if WITH_HTTP
	if (config_lookup_string(cf, "OTR_HTTPHOST", &value) != CONFIG_FALSE) {
		if (ud->http_host) free(ud->http_host);
		ud->http_host = (value) ? strdup(value) : NULL;
	}
	if (config_lookup_int(cf, "OTR_HTTPPORT", &ival) != CONFIG_FALSE) {
		ud->http_port = ival;
	}
	if (config_lookup_string(cf, "OTR_HTTPLOGDIR", &value) != CONFIG_FALSE) {
		if (ud->http_logdir) free(ud->http_logdir);
		ud->http_logdir = (value) ? strdup(value) : NULL;
	}
	if (config_lookup_string(cf, "OTR_BROWSERAPIKEY", &value) != CONFIG_FALSE) {
		if (ud->browser_apikey) free(ud->browser_apikey);
		ud->browser_apikey = (value) ? strdup(value) : NULL;
	}
#endif /* WITH_HTTP */

#if WITH_LUA
	if (config_lookup_string(cf, "OTR_LUASCRIPT", &value) != CONFIG_FALSE) {
		if (ud->luascript) free(ud->luascript);
		ud->luascript = (value) ? strdup(value) : NULL;
	}
#endif

	config_destroy(cf);
}
