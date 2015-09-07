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

#define __USE_XOPEN 1
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include "utstring.h"
#include "config.h"
#include "storage.h"
#include "geohash.h"
#include "util.h"
#include "udata.h"

char STORAGEDIR[BUFSIZ] = "./store";

#define LINESIZE	8192

static struct gcache *gc = NULL;

void storage_init(int revgeo)
{
	char path[BUFSIZ];

	setenv("TZ", "UTC", 1);

	if (revgeo) {
		snprintf(path, BUFSIZ, "%s/ghash", STORAGEDIR);
		gc = gcache_open(path, NULL, TRUE);
	}
}

void get_geo(JsonNode *o, char *ghash)
{
	JsonNode *geo;

	if ((geo = gcache_json_get(gc, ghash)) != NULL) {
		json_copy_to_object(o, geo, FALSE);
		json_delete(geo);
	}
}


/*
 * Populate a JSON object (`node') keyed by directory name; each element points
 * to a JSON array with a list of subdirectories on the first level.
 *
 * { "jpm": [ "5s", "nex4" ], "jjolie": [ "iphone5" ] }
 *
 * Returns 1 on failure.
 */

static int user_device_list(char *name, int level, JsonNode *obj)
{
	DIR *dirp;
	struct dirent *dp;
	char path[BUFSIZ];
	JsonNode *devlist;
	int rc = 0;
	struct stat sb;

	if ((dirp = opendir(name)) == NULL) {
		perror(name);
		return (1);
	}
	while ((dp = readdir(dirp)) != NULL) {
		if (*dp->d_name != '.') {
			sprintf(path, "%s/%s", name, dp->d_name);

			if (stat(path, &sb) != 0) {
				continue;
			}
			if (!S_ISDIR(sb.st_mode))
				continue;


			if (level == 0) {
				devlist = json_mkarray();
				json_append_member(obj, dp->d_name, devlist);
				rc = user_device_list(path, level + 1, devlist);
			} else if (level == 1) {
				json_append_element(obj, json_mkstring(dp->d_name));
			}
		}
	}
	closedir(dirp);
	return (rc);
}

void append_device_details(JsonNode *userlist, char *user, char *device)
{
	char path[BUFSIZ];
	JsonNode *node, *last, *card;

	snprintf(path, BUFSIZ, "%s/last/%s/%s/%s-%s.json",
		STORAGEDIR, user, device, user, device);

	last = json_mkobject();
	if (json_copy_from_file(last, path) == TRUE) {
		json_append_element(userlist, last);
	} else {
		json_delete(last);
	}

	snprintf(path, BUFSIZ, "%s/cards/%s/%s.json",
		STORAGEDIR, user, user);

	card = json_mkobject();
	if (json_copy_from_file(card, path) == TRUE) {
		json_copy_to_object(last, card, FALSE);
	} else {
		json_delete(card);
	}

	if ((node = json_find_member(last, "ghash")) != NULL) {
		if (node->tag == JSON_STRING) {
			get_geo(last, node->string_);
		}
	}
}

/*
 * Return an array of users gleaned from LAST with merged details
 */

JsonNode *last_users()
{
	JsonNode *obj = json_mkobject();
	JsonNode *un, *dn, *userlist = json_mkarray();
	char path[BUFSIZ], user[BUFSIZ], device[BUFSIZ];

	snprintf(path, BUFSIZ, "%s/last", STORAGEDIR);

	if (user_device_list(path, 0, obj) == 1)
		return (obj);

	/* Loop through users, devices */
	json_foreach(un, obj) {
		if (un->tag != JSON_ARRAY)
			continue;
		strcpy(user, un->key);
		json_foreach(dn, un) {
			if (dn->tag == JSON_STRING) {
				strcpy(device, dn->string_);
			} else if (dn->tag == JSON_NUMBER) {	/* all digits? */
				sprintf(device, "%.lf", dn->number_);
			}
			append_device_details(userlist, user, device);
		}
	}
	json_delete(obj);

	return (userlist);
}

/*
 * `s' has a time string in it. Try to convert into time_t
 * using a variety of formats from higher to lower precision.
 * Return 1 on success, 0 on failure.
 */

static int str_time_to_secs(char *s, time_t *secs)
{
	static char **f, *formats[] = {
			"%Y-%m-%dT%H:%M:%S",
			"%Y-%m-%dT%H:%M",
			"%Y-%m-%dT%H",
			"%Y-%m-%d",
			"%Y-%m",
			NULL
		};
	struct tm tm;
	int success = 0;

	memset(&tm, 0, sizeof(struct tm));
	for (f = formats; f && *f; f++) {
		if (strptime(s, *f, &tm) != NULL) {
			success = 1;
			// fprintf(stderr, "str_time_to_secs succeeds with %s\n", *f);
			break;
		}
	}

	if (!success)
		return (0);

	tm.tm_mday = (tm.tm_mday < 1) ? 1 : tm.tm_mday;
	tm.tm_isdst = -1; 		/* A negative value for tm_isdst causes
					 * the mktime() function to attempt to
					 * divine whether summer time is in
					 * effect for the specified time. */

	*secs = mktime(&tm);
	// fprintf(stderr, "str_time_to_secs: %s becomes %04d-%02d-%02d %02d:%02d:%02d\n",
	// 	s,
	// 	tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	// 	tm.tm_hour, tm.tm_min, tm.tm_sec);

	return (1);
}

/*
 * For each of the strings, create an epoch time at the s_ pointers. If
 * a string is NULL, use a default time (from: HOURS previous, to: now)
 * Return 1 if the time conversion was successful and from <= to.
 */

int make_times(char *time_from, time_t *s_lo, char *time_to, time_t *s_hi)
{
	time_t now;


	time(&now);
	if (!time_from || !*time_from) {
		*s_lo = now - (60 * 60 * DEFAULT_HISTORY_HOURS);
	} else {
		if (str_time_to_secs(time_from, s_lo) == 0)
			return (0);
	}

	if (!time_to || !*time_to) {
		*s_hi = now;
	} else {
		if (str_time_to_secs(time_to, s_hi) == 0)
			return (0);
	}
	return (*s_lo > *s_hi ? 0 : 1);
}

/*
 * List the directories in the directory at `path' and put
 * the names into a JSON array which is in obj.
 */

static void ls(char *path, JsonNode *obj)
{
        DIR *dirp;
        struct dirent *dp;
	JsonNode *jarr = json_mkarray();

	if (obj == NULL || obj->tag != JSON_OBJECT)
		return;

        if ((dirp = opendir(path)) == NULL) {
		json_append_member(obj, "error", json_mkstring("Cannot open requested directory"));
                return;
        }

        while ((dp = readdir(dirp)) != NULL) {
                if ((*dp->d_name != '.') && (dp->d_type == DT_DIR)) {
                        // char *s = strdup(dp->d_name);
			json_append_element(jarr, json_mkstring(dp->d_name));
                }
        }

	json_append_member(obj, "results", jarr);
        closedir(dirp);
}

/*
 * List the files in the directory at `pathpat' and
 * put the names into a new JSON array in obj. Filenames (2015-08.rec)
 * are checked whether they fall (time-wise) into the times between
 * s_lo and s_hi.
 */

static time_t t_lo, t_hi;	/* must be global so that filter() can access them */

static int filter_filename(const struct dirent *d)
{
	struct tm tmfile, *tm;
	int lo_months, hi_months, file_months;

	/* if the filename doesn't look like YYYY-MM.rec we can safely ignore it.
	 * Needs modifying after the year 2999 ;-) */
	if (fnmatch("2[0-9][0-9][0-9]-[0-3][0-9].rec", d->d_name, 0) != 0)
		return (0);

	/* Convert filename (YYYY-MM) to a tm; see if months falls between
	 * from months and to months. */

	memset(&tmfile, 0, sizeof(struct tm));
	if (strptime(d->d_name, "%Y-%m", &tmfile) == NULL) {
		fprintf(stderr, "filter: convert err");
		return (0);
	}
	file_months = (tmfile.tm_year + 1900) * 12 + tmfile.tm_mon;

	tm = gmtime(&t_lo);
	lo_months = (tm->tm_year + 1900) * 12 + tm->tm_mon;

	tm = gmtime(&t_hi);
	hi_months = (tm->tm_year + 1900) * 12 + tm->tm_mon;

	/*
	printf("filter: file %s has %04d-%02d-%02d %02d:%02d:%02d\n",
		d->d_name,
		tmfile.tm_year + 1900, tmfile.tm_mon + 1, tmfile.tm_mday,
		tmfile.tm_hour, tmfile.tm_min, tmfile.tm_sec);
	*/

	if (file_months >= lo_months && file_months <= hi_months) {
		// fprintf(stderr, "filter: returns: %s\n", d->d_name);
		return (1);
	}
	return (0);
}

static int cmp( const struct dirent **a, const struct dirent **b)
{
	return strcmp((*a)->d_name, (*b)->d_name);
}

static void lsscan(char *pathpat, time_t s_lo, time_t s_hi, JsonNode *obj, int reverse)
{
	struct dirent **namelist;
	int i, n;
	JsonNode *jarr = json_mkarray();
	static UT_string *path = NULL;

	if (obj == NULL || obj->tag != JSON_OBJECT)
		return;

	utstring_renew(path);

	/* Set global t_ values */
	t_lo = s_lo; //month_part(s_lo);
	t_hi = s_hi; //month_part(s_hi);

	if ((n = scandir(pathpat, &namelist, filter_filename, cmp)) < 0) {
		json_append_member(obj, "error", json_mkstring("Cannot lsscan requested directory"));
                return;
	}

	if (reverse) {
		for (i = n - 1; i >= 0; i--) {
			utstring_clear(path);
			utstring_printf(path, "%s/%s", pathpat, namelist[i]->d_name);
			json_append_element(jarr, json_mkstring(utstring_body(path)));
			free(namelist[i]);
		}
	} else {
		for (i = 0; i < n; i++) {
			utstring_clear(path);
			utstring_printf(path, "%s/%s", pathpat, namelist[i]->d_name);
			json_append_element(jarr, json_mkstring(utstring_body(path)));
			free(namelist[i]);
		}
	}
	free(namelist);

	json_append_member(obj, "results", jarr);
}

/*
 * If `user' and `device' are both NULL, return list of users.
 * If `user` is specified, and device is NULL, return user's devices
 * If both user and device are specified, return list of .rec files;
 * in that case, limit with `s_lo` and `s_hi`. `reverse' is TRUE if
 * list should be sorted in descending order.
 */

JsonNode *lister(char *user, char *device, time_t s_lo, time_t s_hi, int reverse)
{
	JsonNode *json = json_mkobject();
	UT_string *path = NULL;
	char *bp;

	utstring_renew(path);

	for (bp = user; bp && *bp; bp++) {
		if (isupper(*bp))
			*bp = tolower(*bp);
	}
	for (bp = device; bp && *bp; bp++) {
		if (isupper(*bp))
			*bp = tolower(*bp);
	}

	if (!user && !device) {
		utstring_printf(path, "%s/rec", STORAGEDIR);
		ls(utstring_body(path), json);
	} else if (!device) {
		utstring_printf(path, "%s/rec/%s", STORAGEDIR, user);
		ls(utstring_body(path), json);
	} else {
		utstring_printf(path, "%s/rec/%s/%s",
			STORAGEDIR, user, device);
		lsscan(utstring_body(path), s_lo, s_hi, json, reverse);
	}

	return (json);
}

struct jparam {
	JsonNode *obj;
	JsonNode *locs;
	time_t s_lo;
	time_t s_hi;
	output_type otype;
	int limit;		/* if non-zero, we're searching backwards */
	JsonNode *fields;
};

/*
 * line is a single valid '*' line from a .rec file. Turn it into a JSON object.
 * objectorize it. Is that a word? :)
 */

static JsonNode *line_to_location(char *line)
{
	JsonNode *json, *o, *j;
	char *ghash;
	char tstamp[64], *bp;
	double lat, lon;
	long tst;

	snprintf(tstamp, 21, "%s", line);

	if ((bp = strchr(line, '{')) == NULL)
		return (NULL);

	if ((json = json_decode(bp)) == NULL) {
		return (NULL);
	}

	if ((j = json_find_member(json, "_type")) == NULL) {
		json_delete(json);
		return (NULL);
	}
	if (j->tag != JSON_STRING || strcmp(j->string_, "location") != 0) {
		json_delete(json);
		return (NULL);
	}

	o = json_mkobject();

	if (json_copy_to_object(o, json, FALSE) == FALSE) {
		json_delete(o);
		return (NULL);
	}

	lat = lon = 0.0;
	if ((j = json_find_member(o, "lat")) != NULL) {
		lat = j->number_;
	}
	if ((j = json_find_member(o, "lon")) != NULL) {
		lon = j->number_;
	}

	ghash = geohash_encode(lat, lon, GEOHASH_PREC);
	json_append_member(o, "ghash", json_mkstring(ghash));
	json_append_member(o, "isorcv", json_mkstring(tstamp));

	tst = 0L;
	if ((j = json_find_member(o, "tst")) != NULL) {
		tst = j->number_;
	}
	json_append_member(o, "isotst", json_mkstring(isotime(tst)));

	get_geo(o, ghash);

	return (o);
}

/*
 * Invoked via tac() and cat(). Verify that line is indeed a location
 * line from a .rec file. Then objectorize it and add to the locations
 * JSON array and update the counter in our JSON object.
 * Return 0 to tell caller to "ignore" the line, 1 to use it.
 */

static int candidate_line(char *line, void *param)
{
	long counter = 0L;
	JsonNode *j, *o;
	struct jparam *jarg = (struct jparam*)param;
	char *bp;
	JsonNode *obj	= jarg->obj;
	JsonNode *locs	= jarg->locs;
	int limit	= jarg->limit;
	time_t s_lo	= jarg->s_lo;
	time_t s_hi	= jarg->s_hi;
	JsonNode *fields = jarg->fields;
	output_type otype = jarg->otype;

	if (obj == NULL || obj->tag != JSON_OBJECT)
		return (-1);
	if (locs == NULL || locs->tag != JSON_ARRAY)
		return (-1);

	if (limit == 0) {
		/* Reading forwards; account for time */

		char *p;
		struct tm tmline;
		time_t secs;

		if ((p = strptime(line, "%Y-%m-%dT%H:%M:%SZ", &tmline)) == NULL) {
			fprintf(stderr, "no strptime on %s", line);
			return (0);
		}
		secs = mktime(&tmline);

		if (secs <= s_lo || secs >= s_hi) {
			return (0);
		}

		if (otype == RAW) {
			printf("%s\n", line);
			return (0);
		} else if (otype == RAWPAYLOAD) {
			char *bp;

			if ((bp = strchr(line, '{')) != NULL) {
				printf("%s\n", bp);
			}
		}

	}
	if (limit > 0 && otype == RAW) {
		printf("%s\n", line);
		return (1); /* make it 'count' or tac() will not decrement line counter and continue until EOF */
	}

	/* Do we have location line? */
	if ((bp = strstr(line, "Z\t* ")) == NULL) {	/* Not a location line */
		return (0);
	}

	if ((bp = strrchr(bp, '\t')) == NULL) {
		return (0);
	}

	/* Initialize our counter to what the JSON obj currently has */
	if ((j = json_find_member(obj, "count")) != NULL) {
		counter = j->number_;
		json_delete(j);
	}

	// fprintf(stderr, "-->[%s]\n", line);

	if ((o = line_to_location(line)) != NULL) {
		if (fields) {
			/* Create a new object, copying members we're interested in into it */
			JsonNode *f, *node;
			JsonNode *newo = json_mkobject();

			json_foreach(f, fields) {
				char *key = f->string_;

				if ((node = json_find_member(o, key)) != NULL) {
					json_copy_element_to_object(newo, key, node);
				}
			}
			json_delete(o);
			o = newo;
		}
		json_append_element(locs, o);
		++counter;
	}


	/* Add the (possibly) incremented counter back into `obj' */
	json_append_member(obj, "count", json_mknumber(counter));
	return (1);
}

/*
 * Read the file at `filename' (- is stdin) and store location
 * objects at the JSON array `arr`. `obj' is a JSON object which
 * contains `arr'.
 * If limit is zero, we're going forward, else backwards.
 * Fields, if not NULL, is a JSON array of desired element names.
 */

void locations(char *filename, JsonNode *obj, JsonNode *arr, time_t s_lo, time_t s_hi, output_type otype, int limit, JsonNode *fields)
{
	struct jparam jarg;

	if (obj == NULL || obj->tag != JSON_OBJECT)
		return;

	jarg.obj	= obj;
	jarg.locs	= arr;
	jarg.s_lo	= s_lo;
	jarg.s_hi	= s_hi;
	jarg.otype	= otype;
	jarg.limit	= limit;
	jarg.fields	= fields;

	if (limit == 0) {
		cat(filename, candidate_line, &jarg);
	} else {
		tac(filename, limit, candidate_line, &jarg);
	}
}


/*
 * We're being passed an array of location objects created in
 * locations(). Produce a Geo JSON tree.

	{
	  "type": "FeatureCollection",
	  "features": [
	    {
	      "type": "Feature",
	      "geometry": {
		"type": "Point",
		"coordinates": [-80.83775386582222,35.24980190252168]
	      },
	      "properties": {
		"name": "DOUBLE OAKS CENTER",
		"address": "1326 WOODWARD AV"
	      }
	    },
	    {
	      "type": "Feature",
	      "geometry": {
		"type": "Point",
		"coordinates": [-80.83827000459532,35.25674709224663]
	      },
	      "properties": {
		"name": "DOUBLE OAKS NEIGHBORHOOD PARK",
		"address": "2605  DOUBLE OAKS RD"
	      }
	    }
	  ]
	}
 *
 */

static void append_to_feature_array(JsonNode *features, double lat, double lon, char *tid, char *addr)
{
	JsonNode *geom, *props, *f = json_mkobject();

	json_append_member(f, "type", json_mkstring("Feature"));

	geom = json_mkobject();
                   json_append_member(geom, "type", json_mkstring("Point"));
                   JsonNode *coords = json_mkarray();
                   json_append_element(coords, json_mknumber(lon));         /* first LON! */
                   json_append_element(coords, json_mknumber(lat));
                   json_append_member(geom, "coordinates", coords);

	props = json_mkobject();
                  json_append_member(props, "name", json_mkstring(tid));
                  json_append_member(props, "address", json_mkstring(addr));

        json_append_member(f, "geometry", geom);
        json_append_member(f, "properties", props);

        json_append_element(features, f);
}

JsonNode *geo_json(JsonNode *location_array)
{
	JsonNode *one, *j;
        JsonNode *feature_array, *fcollection;

	if ((fcollection = json_mkobject()) == NULL)
		return (NULL);

	json_append_member(fcollection, "type", json_mkstring("FeatureCollection"));

	feature_array = json_mkarray();

	json_foreach(one, location_array) {
		double lat = 0.0, lon = 0.0;
		char *addr = "", *tid = "";

                if ((j = json_find_member(one, "lat")) != NULL) {
                        lat = j->number_;
                }

                if ((j = json_find_member(one, "lon")) != NULL) {
                        lon = j->number_;
                }
                if ((j = json_find_member(one, "tid")) != NULL) {
                        tid = j->string_;
                }
                if ((j = json_find_member(one, "addr")) != NULL) {
                        addr = j->string_;
                }

		append_to_feature_array(feature_array, lat, lon, tid, addr);
	}

	json_append_member(fcollection, "features", feature_array);

	return (fcollection);
}

/*
 * Create JSON suitable to represent this LineString:
 *
 *	{
 *	    "geometry": {
 *	        "coordinates": [
 *	            [
 *	                17.814833,
 *	                59.604585
 *	            ],
 *	            [
 *	                17.814824,
 *	                59.60459
 *	            ]
 *	        ],
 *	        "type": "LineString"
 *	    },
 *	    "type": "Feature"
 *	}
 *
 */
JsonNode *geo_linestring(JsonNode *location_array)
{
	JsonNode *top = json_mkobject();

	json_append_member(top, "type", json_mkstring("Feature"));

	JsonNode *c, *coords = json_mkarray();
	json_foreach(c, location_array) {
		JsonNode *lat, *lon;

		if (((lat = json_find_member(c, "lat")) != NULL) &&
		    ((lon = json_find_member(c, "lon")) != NULL)) {

		    JsonNode *latlon = json_mkarray();

		    json_append_element(latlon, json_mknumber(lon->number_));
		    json_append_element(latlon, json_mknumber(lat->number_));

		    json_append_element(coords, latlon);
		}
	}

	JsonNode *geometry = json_mkobject();

	json_append_member(geometry, "coordinates", coords);
	json_append_member(geometry, "type", json_mkstring("LineString"));
	json_append_member(top, "geometry", geometry);

	return (top);
}

/*
 * Turn our JSON location array into a GPX XML string.
 */

char *gpx_string(JsonNode *location_array)
{
	JsonNode *one;
        static UT_string *xml = NULL;

	if (location_array->tag != JSON_ARRAY)
		return (NULL);

	utstring_renew(xml);

	utstring_printf(xml, "%s", "<?xml version='1.0' encoding='UTF-8' standalone='no' ?>\n\
<gpx version='1.1' creator='OwnTracks-Recorder'>\n\
 <trk>\n\
  <trkseg>\n");

      // <trkpt lat="xx.xxx" lon="yy.yyy"> <!-- Attribute des Trackpunkts --> </trkpt>

	json_foreach(one, location_array) {
		double lat = 0.0, lon = 0.0;
		JsonNode *j;

                if ((j = json_find_member(one, "lat")) != NULL) {
                        lat = j->number_;
                }

                if ((j = json_find_member(one, "lon")) != NULL) {
                        lon = j->number_;
                }

		utstring_printf(xml, "    <trkpt lat='%lf' lon='%lf' />\n", lat, lon);
	}

	utstring_printf(xml, "%s", "  </trkseg>\n</trk>\n</gpx>\n");
	return (utstring_body(xml));
}

/*
 * Remove all data for a user's device. Return a JSON object with a status
 * and an array of deleted files.
 */

static int kill_datastore_filter(const struct dirent *d)
{
	return (*d->d_name == '.') ? 0 : 1;
}

JsonNode *kill_datastore(char *user, char *device)
{
	static UT_string *path = NULL, *fname = NULL;
	JsonNode *obj = json_mkobject(), *killed = json_mkarray();
	char *bp;
	struct dirent **namelist;
	int i, n;

	utstring_renew(path);
	utstring_renew(fname);

	if (!user || !*user || !device || !*device)
		return (obj);

	for (bp = user; bp && *bp; bp++) {
		if (isupper(*bp))
			*bp = tolower(*bp);
	}
	for (bp = device; bp && *bp; bp++) {
		if (isupper(*bp))
			*bp = tolower(*bp);
	}

	utstring_printf(path, "%s/rec/%s/%s", STORAGEDIR, user, device);
	json_append_member(obj, "path", json_mkstring(utstring_body(path)));

	if ((n = scandir(utstring_body(path), &namelist, kill_datastore_filter, NULL)) < 0) {
		json_append_member(obj, "status", json_mkstring("ERROR"));
		json_append_member(obj, "error", json_mkstring(strerror(errno)));
		json_append_member(obj, "reason", json_mkstring("cannot scandir"));
                return (obj);
	}

	for (i = 0; i < n; i++) {
		char *p;

		utstring_clear(fname);
		utstring_printf(fname, "%s/%s", utstring_body(path), namelist[i]->d_name);

		p = utstring_body(fname);
		if (remove(p) == 0) {
			json_append_element(killed, json_mkstring(namelist[i]->d_name));
		} else {
			perror(p);
		}

		free(namelist[i]);
	}
	free(namelist);

	json_append_member(obj, "status", json_mkstring("OK"));
	if (rmdir(utstring_body(path)) != 0) {
		json_append_member(obj, "status", json_mkstring("ERROR"));
		json_append_member(obj, "error", json_mkstring( strerror(errno)));
	}

	json_append_member(obj, "killed", killed);
	return (obj);
}
