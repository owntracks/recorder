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
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <time.h>
#include "json.h"
#include "storage.h"
#include "util.h"
#include "misc.h"
#include "version.h"

/*
 * Print the value in a single JSON node. If string, easy. If number account for
 * what we call 'integer' types which shouldn't be printed as floats.
 */

static void print_one(JsonNode *j, JsonNode *inttypes)
{
	/* Check if the value should be an "integer" (ie not float) */
	if (j->tag == JSON_NUMBER) {
		if (json_find_member(inttypes, j->key)) {
			printf("%.lf", j->number_);
		} else {
			printf("%lf", j->number_);
		}
	} else if (j->tag == JSON_STRING) {
		printf("%s", j->string_);
	} else if (j->tag == JSON_BOOL) {
		printf("%s", (j->bool_) ? "true" : "false");
	} else if (j->tag == JSON_NULL) {
		printf("null");
	}
}

/*
 * Output location data as CSV. If `fields' is not NULL, it's a JSON
 * array of JSON elment names which should be printed instead of the
 * default ALL.
 */

void csv_output(JsonNode *json, output_type otype, JsonNode *fields)
{
	JsonNode *node, *inttypes;
	JsonNode *arr, *one, *j;
	short virgin = 1;

	/* Prime the inttypes object with types we consider "integer" */
	inttypes = json_mkobject();
	json_append_member(inttypes, "batt", json_mkbool(1));
	json_append_member(inttypes, "vel", json_mkbool(1));
	json_append_member(inttypes, "cog", json_mkbool(1));
	json_append_member(inttypes, "tst", json_mkbool(1));
	json_append_member(inttypes, "alt", json_mkbool(1));
	json_append_member(inttypes, "dist", json_mkbool(1));
	json_append_member(inttypes, "trip", json_mkbool(1));

	arr = json_find_member(json, "locations");
	json_foreach(one, arr) {
		/* Headings */
		if (virgin) {
			virgin = !virgin;

			if (fields) {
				json_foreach(node, fields) {
					printf("%s%c", node->string_, (node->next) ? ',' : '\n');
				}
			} else {
				json_foreach(j, one) {
					if (j->key)
						printf("%s%c", j->key, (j->next) ? ',' : '\n');
				}
			}
		}

		/* Now the values */
		if (fields) {
			json_foreach(node, fields) {
				if ((j = json_find_member(one, node->string_)) != NULL) {
					print_one(j, inttypes);
					printf("%c", node->next ? ',' : '\n');
				} else {
					/* specified field not in JSON for this row */
					printf("%c", node->next ? ',' : '\n');
				}
			}
		} else {
			json_foreach(j, one) {
				print_one(j, inttypes);
				printf("%c", j->next ? ',' : '\n');
			}
		}
	}
	json_delete(inttypes);
}

void usage(char *prog)
{
	printf("Usage: %s [options..] [file ...]\n", prog);
	printf("  --help		-h	this message\n");
	printf("  --list		-l	list users (or a user's (-u) devices\n");
	printf("  --user username	-u	specify username ($OCAT_USERNAME)\n");
	printf("  --device devicename   -d	specify device name ($OCAT_DEVICE)\n");
	printf("  --from <time>         -F      from date/time; default -6H\n");
	printf("  --to   <time>         -T      to date/time; default now\n");
	printf("         specify <time> as     	YYYY-MM-DDTHH:MM:SS\n");
	printf("                               	YYYY-MM-DDTHH:MM\n");
	printf("                               	YYYY-MM-DDTHH\n");
	printf("                               	YYYY-MM-DD\n");
	printf("                               	YYYY-MM\n");
	printf("  --limit <number>	-N     	last <number> points\n");
	printf("  --format json    	-f     	output format (default: JSON)\n");
	printf("           csv                 	(overrides $OCAT_FORMAT\n");
	printf("           geojson		Geo-JSON points\n");
	printf("           linestring		Geo-JSON LineString\n");
	printf("           gpx\n");
	printf("           raw\n");
	printf("           payload		Like RAW but JSON payload only\n");
	printf("  --fields tst,lat,lon,...     	Choose fields for CSV. (dflt: ALL)\n");
	printf("  --last		-L     	JSON object with last users\n");
	printf("  --killdata                   	requires -u and -d\n");
	printf("  --storage		-S      storage dir (%s)\n", STORAGEDEFAULT);
	printf("  --norevgeo		-G      disable ghash to reverge-geo lookups\n");
	printf("  --precision		        ghash precision (dflt: %d)\n", GHASHPREC);
	printf("  --version			print version information\n");
	printf("\n");
	printf("Options override these environment variables:\n");
	printf("   $OCAT_USERNAME\n");
	printf("   $OCAT_DEVICE\n");
	printf("   $OCAT_FORMAT\n");
	printf("   $OCAT_STORAGEDIR\n");

	exit(1);
}

void print_versioninfo()
{
	printf("This is OwnTracks Recorder, version %s\n", VERSION);
	printf("built with:\n");
#ifdef HAVE_LMDB
	printf("\tHAVE_LMDB = yes\n");
#endif
#ifdef HAVE_HTTP
	printf("\tHAVE_HTTP = yes\n");
#endif
#ifdef HAVE_PING
	printf("\tHAVE_PING = yes\n");
#endif
	printf("\tSTORAGEDEFAULT = \"%s\"\n", STORAGEDEFAULT);
	printf("\tGHASHPREC = %d\n", GHASHPREC);
	printf("\tDEFAULT_HISTORY_HOURS = %d\n", DEFAULT_HISTORY_HOURS);
	printf("\tJSON_INDENT = \"%s\"\n", (JSON_INDENT) ? JSON_INDENT : "NULL");

	exit(0);
}

int main(int argc, char **argv)
{
	char *progname = *argv, *p;
	int c;
	int list = 0, killdata = 0, last = 0, limit = 0;
	int revgeo = TRUE;
	char *username = NULL, *device = NULL, *time_from = NULL, *time_to = NULL;
	JsonNode *json, *obj, *locs;
	time_t now, s_lo, s_hi;
	output_type otype = JSON;
	JsonNode *fields = NULL;

	if ((p = getenv("OCAT_USERNAME")) != NULL) {
		username = strdup(p);
	}
	if ((p = getenv("OCAT_DEVICE")) != NULL) {
		device = strdup(p);
	}

	if ((p = getenv("OCAT_FORMAT")) != NULL) {
		switch (tolower(*p)) {
			case 'j': otype = JSON; break;
			case 'c': otype = CSV; break;
			case 'g': otype = GEOJSON; break;
			case 'r': otype = RAW; break;
			case 'p': otype = RAWPAYLOAD; break;
			case 'l': otype = LINESTRING; break;
		}
	}

	if ((p = getenv("OCAT_STORAGEDIR")) != NULL) {
		strcpy(STORAGEDIR, p);
	}

	time(&now);

	while (1) {
		static struct option long_options[] = {
			{ "help",	no_argument,	0, 	'h'},
			{ "version",	no_argument,	0, 	3},
			{ "list",	no_argument,	0, 	'l'},
			{ "user",	required_argument, 0, 	'u'},
			{ "device",	required_argument, 0, 	'd'},
			{ "from",	required_argument, 0, 	'F'},
			{ "to",		required_argument, 0, 	'T'},
			{ "limit",	required_argument, 0, 	'N'},
			{ "format",	required_argument, 0, 	'f'},
			{ "storage",	required_argument, 0, 	'S'},
			{ "last",	no_argument, 0, 	'L'},
			{ "fields",	required_argument, 0, 	1},
			{ "precision",	required_argument, 0, 	2},
			{ "killdata",	no_argument, 0, 	'K'},
			{ "norevgeo",	no_argument, 0, 	'G'},
		  	{0, 0, 0, 0}
		  };
		int optindex = 0;

		c = getopt_long(argc, argv, "hlu:d:F:T:f:KLS:GN:", long_options, &optindex);
		if (c == -1)
			break;

		switch (c) {
			case 1:	/* No short option */
				if (strcmp(optarg, "ALL") != 0)
					fields = json_splitter(optarg, ",");
				break;
			case 2:
				geohash_setprec(atoi(optarg));
				break;
			case 3:
				print_versioninfo();
				break;
			case 'l':
				list = 1;
				break;
			case 'u':
				username = strdup(optarg);
				break;
			case 'd':
				device = strdup(optarg);
				break;
			case 'F':
				time_from = strdup(optarg);
				break;
			case 'N':
				limit = atoi(optarg);
				break;
			case 'G':
				revgeo = FALSE;
				break;
			case 'S':
				strcpy(STORAGEDIR, optarg);
				break;
			case 'T':
				time_to = strdup(optarg);
				break;
			case 'f':
				if (!strcmp(optarg, "json"))
					otype = JSON;
				else if (!strcmp(optarg, "geojson"))
					otype = GEOJSON;
				else if (!strcmp(optarg, "linestring"))
					otype = LINESTRING;
				else if (!strcmp(optarg, "raw"))
					otype = RAW;
				else if (!strcmp(optarg, "payload"))
					otype = RAWPAYLOAD;
				else if (!strcmp(optarg, "gpx"))
					otype = GPX;
				else if (!strcmp(optarg, "csv"))
					otype = CSV;
				else {
					fprintf(stderr, "%s: unrecognized output format\n", progname);
					exit(2);
				}
				break;
			case 'K':
				killdata = 1;
				break;
			case 'L':
				last = 1;
				break;
			case 'h':
			case '?':
				/* getopt_long already printed message */
				usage(progname);
				break;
			default:
				abort();
		}

	}

	argc -= (optind);
	argv += (optind);

	storage_init(revgeo);

	if (killdata) {
		JsonNode *obj; //, *killed, *f;

		if (!username || !device) {
			fprintf(stderr, "%s: killdata requires username and device\n", progname);
			return (-2);
		}

		fprintf(stderr, "Storage deleted these files:\n");
		if ((obj = kill_datastore(username, device)) != NULL) {
			char *js;
			// if ((killed = json_find_member(obj, "results")) != NULL) { // get array
			// 	json_foreach(f, killed) {
			// 		fprintf(stderr, "  %s\n", f->string_);
			// 	}
			// }

			js = json_stringify(obj, JSON_INDENT);
			printf("%s\n", js);
			free(js);
			json_delete(obj);
		}
		return (0);
	}

	if (last) {
		JsonNode *user_array;

		if ((user_array = last_users(username, device)) != NULL) {
			char *js;

			if ((js = json_stringify(user_array, JSON_INDENT)) != NULL) {
				printf("%s\n", js);
				free(js);
			}
			json_delete(user_array);
		}
		return (0);
	}

	if (!username && device) {
		fprintf(stderr, "%s: device name without username doesn't make sense\n", progname);
		return (-2);
	}

	/* If no from time specified but limit, set from to this month */
	if (limit) {
		if (time_from == NULL) {
			time_from = strdup(yyyymm(now));
		}
	}

	if (make_times(time_from, &s_lo, time_to, &s_hi) != 1) {
		fprintf(stderr, "%s: bad time(s) specified\n", progname);
		return (-2);
	}

	if (list) {
		char *js;

		json = lister(username, device, 0, s_hi, FALSE);
		if (json == NULL) {
			fprintf(stderr, "%s: cannot list\n", progname);
			exit(2);
		}
		if (otype == JSON) {
			js = json_stringify(json, JSON_INDENT);
			printf("%s\n", js);
			free(js);
		} else {
			JsonNode *f, *arr;

			if ((arr = json_find_member(json, "results")) != NULL) { // get array
				json_foreach(f, arr) {
					printf("%s\n", f->string_);
				}
			}
		}

		json_delete(json);

		return (0);
	}

	if (argc == 0 && !username && !device) {
		fprintf(stderr, "%s: nothing to do. Specify filename or --user and --device\n", progname);
		return (-1);
	} else if (argc == 0 && (!username || !device)) {
		fprintf(stderr, "%s: must specify username and device\n", progname);
		return (-1);
	} else if ((username || device) && (argc > 0)) {
		fprintf(stderr, "%s: filename with --user and --device is not supported\n", progname);
		return (-1);
	}


	/*
	 * If any files were passed on the command line, we assume these are *.rec
	 * and process those. Otherwise, we expect a `user' and `device' and process
	 * "today"
	 */

	obj = json_mkobject();
	locs = json_mkarray();


	if (argc) {
		int n;

		for (n = 0; n < argc; n++) {
			locations(argv[n], obj, locs, s_lo, s_hi, otype, 0, fields);
		}
	} else {
		JsonNode *arr, *f;

		/*
		 * Obtain a list of .rec files from lister(), possibly limited by s_lo/s_hi times,
		 * process each and build the JSON `obj' with an array of locations.
		 */

		if ((json = lister(username, device, s_lo, s_hi, (limit > 0) ? TRUE : FALSE)) != NULL) {
			if ((arr = json_find_member(json, "results")) != NULL) { // get array
				json_foreach(f, arr) {
					locations(f->string_, obj, locs, s_lo, s_hi, otype, limit, fields);
					// printf("%s\n", f->string_);
				}
			}
			json_delete(json);
		}
	}

	json_append_member(obj, "locations", locs);


	if (otype == JSON) {
		char *js = json_stringify(obj, JSON_INDENT);

		if (js != NULL) {
			printf("%s\n", js);
			free(js);
		}

	} else if (otype == CSV) {
		csv_output(obj, CSV, fields);
	} else if (otype == RAW || otype == RAWPAYLOAD) {
		/* We've already done what we need to do in locations() */
	} else if (otype == LINESTRING) {
		JsonNode *geolinestring = geo_linestring(locs);
		char *js;

		if (geolinestring != NULL) {
			js = json_stringify(geolinestring, JSON_INDENT);
			if (js != NULL) {
				printf("%s\n", js);
				free(js);
			}
			json_delete(geolinestring);
		}

	} else if (otype == GEOJSON) {
		JsonNode *geojson = geo_json(locs);
		char *js;

		if (geojson != NULL) {
			js = json_stringify(geojson, JSON_INDENT);
			if (js != NULL) {
				printf("%s\n", js);
				free(js);
			}
			json_delete(geojson);
		}
	} else if (otype == GPX) {
		char *xml = gpx_string(locs);

		if (xml)
			printf("%s\n", xml);
	}

	json_delete(obj);
	if (fields)
		json_delete(fields);

	return (0);
}
