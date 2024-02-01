/*
 * OwnTracks Recorder
 * Copyright (C) 2024 Jan-Piet Mens <jpmens@gmail.com>
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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "json.h"
#include "geohash.h"
#include "zonedetect.h"

int main(int argc, char **argv)
{
	char buf[BUFSIZ];
	char *s, *geohash, *js;
	JsonNode *json, *j;

	static ZoneDetect *zdb = NULL;

	zdb = ZDOpenDatabase(TZDATADB);
	if (zdb == NULL) {
		perror("Can't open zdb");
		exit(2);
	}

	while (fgets(s = buf, sizeof(buf), stdin) != NULL) {
		// find first space
		if ((geohash = strtok(buf, " ")) != NULL) {
			js = buf + strlen(geohash) + 1;
			// puts(js);
			if ((json = json_decode(js)) == NULL) {
				if (strncmp(buf, "friends", 7) &&
				    strncmp(buf, "topic2tid", 9) &&
				    strncmp(buf, "wp", 2)) {
					fprintf(stderr, "Can't decode %s\n", buf);
				}
				continue;
			}
			if ((j = json_find_member(json, "tzname")) == NULL) {
				char *tz;

				GeoCoord g = geohash_decode(geohash);

				if ((tz = ZDHelperSimpleLookupString(zdb, g.latitude, g.longitude)) != NULL) {
					json_append_member(json, "tzname", json_mkstring(tz));
					ZDHelperSimpleLookupStringFree(tz);
				}
			}
			js = json_stringify(json, NULL);
			printf("%s %s\n", geohash, js);
			free(js);
			json_delete(json);
		}
	}
	return 0;
}
