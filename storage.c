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
#include <glob.h>
#include <ctype.h>
#include <sys/stat.h>
#include "utstring.h"
#include "storage.h"
#include "geohash.h"
#include "util.h"
#include "udata.h"
#include "listsort.h"

char STORAGEDIR[BUFSIZ] = STORAGEDEFAULT;

#define LINESIZE	8192

static struct gcache *gc = NULL;

void storage_init(int revgeo)
{
	char path[BUFSIZ];

	setenv("TZ", "UTC", 1);

	if (revgeo) {
		snprintf(path, BUFSIZ, "%s/ghash", STORAGEDIR);
		gc = gcache_open(path, NULL, TRUE);
		if (gc == NULL) {
			olog(LOG_ERR, "storage_init(): gc is NULL");
		}
	}
}

void storage_gcache_dump(char *lmdbname)
{
	char path[BUFSIZ];
	snprintf(path, BUFSIZ, "%s/ghash", STORAGEDIR);

	gcache_dump(path, lmdbname);
}

void storage_gcache_load(char *lmdbname)
{
	char path[BUFSIZ];

	snprintf(path, BUFSIZ, "%s/ghash", STORAGEDIR);
	gcache_load(path, lmdbname);
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
 * Populate a JSON object (`obj') keyed by directory name; each element points
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
		JsonNode *tst;

		if ((tst = json_find_member(last, "tst")) != NULL) {
			json_append_member(last, "isotst", json_mkstring(isotime(tst->number_)));
		}

		json_append_element(userlist, last);
	} else {
		json_delete(last);
	}

	snprintf(path, BUFSIZ, "%s/cards/%s/%s.json",
		STORAGEDIR, user, user);

	card = json_mkobject();
	if (json_copy_from_file(card, path) == TRUE) {
		json_copy_to_object(last, card, FALSE);
	}
	json_delete(card);

	if ((node = json_find_member(last, "ghash")) != NULL) {
		if (node->tag == JSON_STRING) {
			get_geo(last, node->string_);
		}
	}
}

/*
 * Return an array of users gleaned from LAST with merged details.
 * If user and device are specified, limit to those; either may be
 * NULL.
 */

JsonNode *last_users(char *in_user, char *in_device)
{
	JsonNode *obj = json_mkobject();
	JsonNode *un, *dn, *userlist = json_mkarray();
	char path[BUFSIZ], user[BUFSIZ], device[BUFSIZ];

	snprintf(path, BUFSIZ, "%s/last", STORAGEDIR);

	// fprintf(stderr, "last_users(%s, %s)\n", (in_user) ? in_user : "<nil>",
	// 	(in_device) ? in_device : "<nil>");

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

			if (!in_user && !in_device) {
				append_device_details(userlist, user, device);
			} else if (in_user && !in_device) {
				if (strcmp(user, in_user) == 0) {
					append_device_details(userlist, user, device);
				}
			} else if (in_user && in_device) {
				if (strcmp(user, in_user) == 0 && strcmp(device, in_device) == 0) {
					append_device_details(userlist, user, device);
				}
			}
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
			"%Y-%m-%dt%H:%M:%S",
			"%Y-%m-%dt%H:%M",
			"%Y-%m-%dt%H",
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
	JsonNode *jarr;
	static UT_string *path = NULL;

	if (obj == NULL || obj->tag != JSON_OBJECT)
		return;

	/* If our obj contains the "results" array, use that
	 * and remove from obj; we'll add it back later.
	 */
	if ((jarr = json_find_member(obj, "results")) == NULL) {
		jarr = json_mkarray();
	} else {
		json_remove_from_parent(jarr);
	}

	utstring_renew(path);

	/* Set global t_ values */
	t_lo = s_lo;
	t_hi = s_hi;

	if ((n = scandir(pathpat, &namelist, filter_filename, cmp)) < 0) {
		json_append_member(obj, "error", json_mkstring("Cannot lsscan requested directory"));
                return;
	}

	if (reverse) {
		for (i = n - 1; i >= 0; i--) {
			utstring_clear(path);
			utstring_printf(path, "%s/%s", pathpat, namelist[i]->d_name);
			json_append_element(jarr, json_mkstring(UB(path)));
			free(namelist[i]);
		}
	} else {
		for (i = 0; i < n; i++) {
			utstring_clear(path);
			utstring_printf(path, "%s/%s", pathpat, namelist[i]->d_name);
			json_append_element(jarr, json_mkstring(UB(path)));
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
		ls(UB(path), json);
	} else if (!device) {
		utstring_printf(path, "%s/rec/%s", STORAGEDIR, user);
		ls(UB(path), json);
	} else {
		utstring_printf(path, "%s/rec/%s/%s",
			STORAGEDIR, user, device);
		lsscan(UB(path), s_lo, s_hi, json, reverse);
	}

	return (json);
}

/*
 * List multiple user/device combinations. udpairs is a JSON
 * array of strings, each of which is a user/device pair
 * separated by a slash.
 */

JsonNode *multilister(JsonNode *udpairs, time_t s_lo, time_t s_hi, int reverse)
{
	JsonNode *json = json_mkobject(), *ud;
	UT_string *path = NULL;
	char *pairs[2];
	int np, n;

	if (udpairs == NULL || udpairs->tag != JSON_ARRAY) {
		return (json);
	}

	json_foreach(ud, udpairs) {
		if ((np = splitter(ud->string_, "/", pairs)) != 2) {
			continue;
		}
		utstring_renew(path);
		utstring_printf(path, "%s/rec/%s/%s",
			STORAGEDIR,
			pairs[0],		/* user */
			pairs[1]);		/* device */
		lsscan(UB(path), s_lo, s_hi, json, reverse);

		for (n = 0; n < np; n++) {
			free(pairs[n]);
		}
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
	JsonNode *fields;	/* If non-NULL array of fields names to return */
	char *username;		/* If non-NULL, add username to location  */
	char *device;		/* If non-NULL, add device name to location */
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
		json_delete(json);
		return (NULL);
	}
	json_delete(json);	/* Done with this -- we've copied it. */

	lat = lon = 0.0;
	if ((j = json_find_member(o, "lat")) != NULL) {
		lat = j->number_;
	}
	if ((j = json_find_member(o, "lon")) != NULL) {
		lon = j->number_;
	}

	if ((ghash = geohash_encode(lat, lon, geohash_prec())) != NULL) {
		json_append_member(o, "ghash", json_mkstring(ghash));
		get_geo(o, ghash);
		free(ghash);
	}

	json_append_member(o, "isorcv", json_mkstring(tstamp));

	tst = 0L;
	if ((j = json_find_member(o, "tst")) != NULL) {
		tst = j->number_;
	}
	json_append_member(o, "isotst", json_mkstring(isotime(tst)));

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
	char *username	= jarg->username;
	char *device	= jarg->device;

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

		/*
		 * Username/device are added typically for multilister() only.
		 */

		if (username)
			json_append_member(o, "username", json_mkstring(username));
		if (device)
			json_append_member(o, "device", json_mkstring(device));

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
 *
 * If username & device are not NULL, populate the JSON locations
 * with them for multilister().
 */

void locations(char *filename, JsonNode *obj, JsonNode *arr, time_t s_lo, time_t s_hi, output_type otype, int limit, JsonNode *fields, char *username, char *device)
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
	jarg.username	= username;
	jarg.device	= device;

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
	JsonNode *top = json_mkobject(), *sorted;

	json_append_member(top, "type", json_mkstring("Feature"));

	JsonNode *c, *coords = json_mkarray();

	sorted = listsort(json_first_child(location_array), 0, 0);
	if (sorted && sorted->parent)
		sorted = sorted->parent;
	else
		sorted = location_array;

	json_foreach(c, sorted) {
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
	return (UB(xml));
}

#if WITH_KILL

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
	int i, n, rc;
	glob_t results;

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
	json_append_member(obj, "path", json_mkstring(UB(path)));

	if ((n = scandir(UB(path), &namelist, kill_datastore_filter, NULL)) < 0) {
		json_append_member(obj, "status", json_mkstring("ERROR"));
		json_append_member(obj, "error", json_mkstring(strerror(errno)));
		json_append_member(obj, "reason", json_mkstring("cannot scandir"));
                return (obj);
	}

	for (i = 0; i < n; i++) {
		char *p;

		utstring_clear(fname);
		utstring_printf(fname, "%s/%s", UB(path), namelist[i]->d_name);

		p = UB(fname);
		if (remove(p) == 0) {
			olog(LOG_NOTICE, "removed %s", p);
			json_append_element(killed, json_mkstring(namelist[i]->d_name));
		} else {
			perror(p);
		}

		free(namelist[i]);
	}
	free(namelist);

	json_append_member(obj, "status", json_mkstring("OK"));
	if (rmdir(UB(path)) != 0) {
		json_append_member(obj, "status", json_mkstring("ERROR"));
		json_append_member(obj, "error", json_mkstring( strerror(errno)));
	} else {
		olog(LOG_NOTICE, "removed %s", UB(path));

		/* Attempt to remove containing directory */
		utstring_renew(path);
		utstring_printf(path, "%s/rec/%s", STORAGEDIR, user);
		if (rmdir(UB(path)) == 0) {
			olog(LOG_NOTICE, "removed %s", UB(path));
		}
	}

	utstring_renew(path);
	utstring_printf(path, "%s/last/%s/%s/%s-%s.json", STORAGEDIR, user, device, user, device);
	if (remove(UB(path)) == 0) {
		olog(LOG_NOTICE, "removed %s", UB(path));
		json_append_member(obj, "last", json_mkstring(UB(path)));
	}

	/* Attempt to remove containing directory */
	utstring_renew(path);
	utstring_printf(path, "%s/last/%s/%s", STORAGEDIR, user, device);
	if (rmdir(UB(path)) == 0) {
		olog(LOG_NOTICE, "removed %s", UB(path));
	}

	/* Attempt to remove it's parent directory */
	utstring_renew(path);
	utstring_printf(path, "%s/last/%s", STORAGEDIR, user);
	if (rmdir(UB(path)) == 0) {
		olog(LOG_NOTICE, "removed %s", UB(path));
	}

	/* Attempt to remove CARD ... */
	utstring_renew(path);
	utstring_printf(path, "%s/cards/%s/%s.json", STORAGEDIR, user, user);
	if (remove(UB(path)) == 0) {
		olog(LOG_NOTICE, "removed %s", UB(path));
		json_append_member(obj, "card", json_mkstring(UB(path)));
	}

	/* ... and it's parent directory */
	utstring_renew(path);
	utstring_printf(path, "%s/cards/%s", STORAGEDIR, user);
	if (rmdir(UB(path)) == 0) {
		olog(LOG_NOTICE, "removed %s", UB(path));
	}

	/* Attempt to remove PHOTO ... */
	utstring_renew(path);
	utstring_printf(path, "%s/photos/%s.png", STORAGEDIR, user);
	if (remove(UB(path)) == 0) {
		olog(LOG_NOTICE, "removed %s", UB(path));
		json_append_member(obj, "photo", json_mkstring(UB(path)));
	}

	/* ... and parent directory */
	utstring_renew(path);
	utstring_printf(path, "%s/photos/%s", STORAGEDIR, user);
	if (rmdir(UB(path)) == 0) {
		olog(LOG_NOTICE, "removed %s", UB(path));
	}

	/* Remove config (.otrc) */
	utstring_renew(path);
	utstring_printf(path, "%s/config/%s-%s.otrc", STORAGEDIR, user, device);
	if (remove(UB(path)) == 0) {
		olog(LOG_NOTICE, "removed %s", UB(path));
		json_append_member(obj, "otrc", json_mkstring(UB(path)));
	}


	/* Remove waypoint files and containing directories */
	utstring_renew(path);
	utstring_printf(path, "%s/waypoints/%s/%s/*.json", STORAGEDIR, user, device);
	rc = glob(UB(path), 0, 0, &results);
	if (rc == 0) {
		JsonNode *list = json_mkarray();

		for (n = 0; n < results.gl_pathc; n++) {
			if (remove(results.gl_pathv[n]) == 0) {
				olog(LOG_NOTICE, "removed %s", results.gl_pathv[n]);
				json_append_element(list, json_mkstring(results.gl_pathv[n]));
			}
		}
		json_append_member(obj, "waypoint", list);
	}
	globfree(&results);

	utstring_renew(path);
	utstring_printf(path, "%s/waypoints/%s/%s/%s-%s.otrw", STORAGEDIR, user, device, user, device);
	if (remove(UB(path)) == 0) {
		olog(LOG_NOTICE, "removed %s", UB(path));
		json_append_member(obj, "otrw", json_mkstring(UB(path)));
	}

	utstring_renew(path);
	utstring_printf(path, "%s/waypoints/%s/%s", STORAGEDIR, user, device);
	if (rmdir(UB(path)) == 0) {
		olog(LOG_NOTICE, "removed %s", UB(path));
	}

	utstring_renew(path);
	utstring_printf(path, "%s/waypoints/%s", STORAGEDIR, user);
	if (rmdir(UB(path)) == 0) {
		olog(LOG_NOTICE, "removed %s", UB(path));
	}

	json_append_member(obj, "killed", killed);
	return (obj);
}
#endif /* WITH_KILL */

/*
 * Print the value in a single JSON node as XML by invoking func(). If string,
 * easy. If number account for what we call 'integer' types which shouldn't be
 * printed as floats.
 */

static void emit_one(JsonNode *j, JsonNode *inttypes, void (func)(char *line, void *param), void *param)
{
	static UT_string *line = NULL;

	if (!strcmp(j->key, "_type"))
		return;

	utstring_renew(line);
	utstring_printf(line, "  <%s>", j->key);
	/* Check if the value should be an "integer" (ie not float) */
	if (j->tag == JSON_NUMBER) {
		if (json_find_member(inttypes, j->key)) {
			utstring_printf(line, "%.lf", j->number_);
		} else {
			utstring_printf(line, "%lf", j->number_);
		}
	} else if (j->tag == JSON_STRING) {
		char *bp;

		for (bp = j->string_; bp && *bp; bp++) {
			switch (*bp) {
				case '&':	utstring_printf(line, "&amp;"); break;
				case '<':	utstring_printf(line, "&lt;"); break;
				case '>':	utstring_printf(line, "&gt;"); break;
				case '"':	utstring_printf(line, "&quot;"); break;
				case '\'':	utstring_printf(line, "&apos;"); break;
				default:	utstring_printf(line, "%c", *bp); break;
			}
		}
	} else if (j->tag == JSON_BOOL) {
		utstring_printf(line, "%s", (j->bool_) ? "true" : "false");
	} else if (j->tag == JSON_NULL) {
		utstring_printf(line, "null");
	}
	utstring_printf(line, "</%s>", j->key);
	func(UB(line), param);
}

void xml_output(JsonNode *json, output_type otype, JsonNode *fields, void (*func)(char *s, void *param), void *param)
{
	JsonNode *node, *inttypes;
	JsonNode *arr, *one, *j;

	/* Prime the inttypes object with types we consider "integer" */
	inttypes = json_mkobject();
	json_append_member(inttypes, "batt", json_mkbool(1));
	json_append_member(inttypes, "vel", json_mkbool(1));
	json_append_member(inttypes, "cog", json_mkbool(1));
	json_append_member(inttypes, "tst", json_mkbool(1));
	json_append_member(inttypes, "alt", json_mkbool(1));
	json_append_member(inttypes, "dist", json_mkbool(1));
	json_append_member(inttypes, "trip", json_mkbool(1));

	func("<?xml version='1.0' encoding='UTF-8'?>\n\
	<?xml-stylesheet type='text/xsl' href='owntracks.xsl'?>", param);
	func("<owntracks>", param);

	arr = json_find_member(json, "locations");
	json_foreach(one, arr) {
		func(" <point>", param);
		if (fields) {
			json_foreach(node, fields) {
				if ((j = json_find_member(one, node->string_)) != NULL) {
					emit_one(j, inttypes, func, param);
				} else {
					/* empty element */
					char label[128];

					snprintf(label, sizeof(label), "  <%s />", node->string_);
					func(label, param);
				}
			}
		} else {
			json_foreach(j, one) {
				emit_one(j, inttypes, func, param);
			}
		}
		func(" </point>\n", param);
	}
	func("</owntracks>", param);

	json_delete(inttypes);
}

char *storage_userphoto(char *username)
{
	static char path[BUFSIZ];

	if (!username || !*username)
		return (NULL);

	snprintf(path, sizeof(path), "%s/photos/%s/%s.png", STORAGEDIR, username, username);

	return (path);
}
