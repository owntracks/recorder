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
	utstring_printf(us, "%ld %s\n", now, topic);

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

	if (access(filename, R_OK) == -1)
		return;

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
#if WITH_MQTT
	if (config_lookup_string(cf, "OTR_HOST", &value) != CONFIG_FALSE) {
		if (ud->hostname) free(ud->hostname);
		ud->hostname = strdup(value);
	}
	if (config_lookup_int(cf, "OTR_PORT", &ival) != CONFIG_FALSE) {
		ud->port = ival;
	}
	if (config_lookup_string(cf, "OTR_USER", &value) != CONFIG_FALSE) {
		if (ud->username) free(ud->username);
		ud->username = strdup(value);
	}
	if (config_lookup_string(cf, "OTR_PASS", &value) != CONFIG_FALSE) {
		if (ud->password) free(ud->password);
		ud->password = strdup(value);
	}
	if (config_lookup_int(cf, "OTR_QOS", &ival) != CONFIG_FALSE) {
		ud->qos = ival;
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
		ud->geokey = strdup(value);
	}

	if (config_lookup_int(cf, "OTR_PRECISION", &ival) != CONFIG_FALSE) {
		geohash_setprec(ival);
		printf("PREC=%d\n", geohash_prec());
	}

#if WITH_HTTP
	if (config_lookup_string(cf, "OTR_HTTPHOST", &value) != CONFIG_FALSE) {
		if (ud->http_host) free(ud->http_host);
		ud->http_host = strdup(value);
	}
	if (config_lookup_int(cf, "OTR_HTTPPORT", &ival) != CONFIG_FALSE) {
		ud->http_port = ival;
	}
#endif /* WITH_HTTP */

#if WITH_LUA
	if (config_lookup_string(cf, "OTR_LUASCRIPT", &value) != CONFIG_FALSE) {
		if (ud->luascript) free(ud->luascript);
		ud->luascript = strdup(value);
	}
#endif

	config_destroy(cf);
}
