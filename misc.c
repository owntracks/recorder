/*
 * Copyright (C) 2015 Jan-Piet Mens <jpmens@gmail.com> and OwnTracks
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * JAN-PIET MENS OR OWNTRACKS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
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
	return (utstring_body(out));
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
	safewrite(mpath, utstring_body(us));
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
