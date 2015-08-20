#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <glob.h>
#include "utstring.h"
#include "config.h"
#include "storage.h"
#include "geohash.h"

#include "udata.h"

int ghash_readcache(struct udata *ud, char *ghash, UT_string *addr, UT_string *cc);

void get_geo(JsonNode *o, char *ghash)
{
	static UT_string *addr = NULL, *cc = NULL;
	static struct udata udata;

	udata.usefiles = 1;

	utstring_renew(addr);
	utstring_renew(cc);

	if (ghash_readcache(&udata, ghash, addr, cc) == 1) {
		json_append_member(o, "addr", json_mkstring(utstring_body(addr)));
		json_append_member(o, "cc", json_mkstring(utstring_body(cc)));
	}
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

	printf("opendir %s\n", path);
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
 * List the files (glob pattern) in the directory at `pathpat' and
 * put the names into a JSON array in obj.
 */

static void lsglob(char *pathpat, JsonNode *obj)
{
        glob_t paths;
        int flags = GLOB_NOCHECK;
        char **p;
	JsonNode *jarr = json_mkarray();

        if (glob(pathpat, flags, NULL, &paths) != 0) {
		json_append_member(obj, "error", json_mkstring("Cannot glob requested pattern"));
                return;
	}

	/* Ensure we add paths only if the glob actuall expanded */
	if (paths.gl_matchc > 0) {
		for (p = paths.gl_pathv; *p != NULL; p++) {
			// char *s = strdup(*p);
			// utarray_push_back(dirs, &s);
			json_append_element(jarr, json_mkstring(*p));
		}
        }
        globfree(&paths);

	json_append_member(obj, "results", jarr);
        return;
}

/*
 * If `user' and `device' are both NULL, return list of users.
 * If `user` is specified, and device is NULL, return user's devices
 * If both user and device are specified, return list of .rec files;
 * in that case, limit with `yyyymm' which is YYYY-MM
 */

JsonNode *lister(char *user, char *device, char *yyyymm)
{
	JsonNode *json = json_mkobject();
	UT_string *path = NULL;

	utstring_renew(path);

	/* FIXME: lowercase user/device */

	if (!user && !device) {
		utstring_printf(path, "%s/rec", STORAGEDIR);
		ls(utstring_body(path), json);
	} else if (!device) {
		utstring_printf(path, "%s/rec/%s", STORAGEDIR, user);
		ls(utstring_body(path), json);
	} else {
		utstring_printf(path, "%s/rec/%s/%s/%s.rec",
			STORAGEDIR, user, device,
			(yyyymm) ? yyyymm : "*");
		lsglob(utstring_body(path), json);
	}

	return (json);
}

/*
 * Read the file at `filename' (- is stdin) and store location
 * objects at the JSON array `arr`. `obj' is a JSON object which
 * contains `arr'.
 */

void locations(char *filename, JsonNode *obj, JsonNode *arr)
{
	JsonNode *o, *json, *j;
	FILE *fp;
	int doclose;
	char buf[BUFSIZ], **element;
	long counter = 0L;
	static char *numbers[] = { "lat", "lon", "batt", "vel", "cog", "tst", "alt", "dist", "trip", NULL };
	static char *strings[] = { "tid", "t", NULL };
	extern int errno;


	if (!strcmp(filename, "-")) {
		fp = stdin;
		doclose = 0;
	} else {
		if ((fp = fopen(filename, "r")) == NULL) {
			fprintf(stderr, "Cannot open %s: %s\n", filename, strerror(errno));
			return;
		}
		doclose = 1;
	}

	/* Initialize our counter to what the JSON obj currently has */

	if ((j = json_find_member(obj, "count")) != NULL) {
		counter = j->number_;
		json_delete(j);
	}


	while (fgets(buf, sizeof(buf)-1, fp) != NULL) {
		char *bp, *ghash;
		double lat, lon;

		if ((bp = strstr(buf, "Z\t* ")) != NULL) {
			if ((bp = strrchr(bp, '\t')) == NULL) {
				continue;
			}
			++counter;
			json = json_decode(bp + 1);
			if (json == NULL) {
				puts("Cannot decode JSON");
				continue;
			}

			o = json_mkobject();

			/* Start adding stuff to the object, then copy over the elements
			 * from the decoded original locations. */

			for (element = numbers; element && *element; element++) {
				if ((j = json_find_member(json, *element)) != NULL) {
					if (j->tag == JSON_NUMBER) {
						json_append_member(o, *element, json_mknumber(j->number_));
					} else {
						double d = atof(j->string_);
						json_append_member(o, *element, json_mknumber(d));
					}
				}
			}

			for (element = strings; element && *element; element++) {
				if ((j = json_find_member(json, *element)) != NULL) {
					json_append_member(o, *element, json_mkstring(j->string_));
				}
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

			get_geo(o, ghash);


			json_append_element(arr, o);
		}
	}

	/* Add the counter back into `obj' */

	json_append_member(obj, "count", json_mknumber(counter));


	if (doclose)
		fclose(fp);

}
