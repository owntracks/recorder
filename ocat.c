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
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <time.h>
#if WITH_MQTT
# include <mosquitto.h>
#endif
#include "json.h"
#include "udata.h"
#include "fences.h"
#include "gcache.h"
#include "storage.h"
#include "util.h"
#include "misc.h"
#include "version.h"
#if WITH_ENCRYPT
# include <sodium.h>
#endif

static void print_xml_line(char *line, void *param)
{
	FILE *fp = (FILE *)param;

	fprintf(fp, "%s", line);
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
	printf("           xml\n");
	printf("           raw\n");
	printf("           payload		Like RAW but JSON payload only\n");
	printf("  --fields tst,lat,lon,...     	Choose fields for CSV. (dflt: ALL)\n");
	printf("  --last		-L     	JSON object with last users\n");
#if WITH_KILL
	printf("  --killdata                   	requires -u and -d\n");
#endif
	printf("  --storage		-S      storage dir (%s)\n", STORAGEDEFAULT);
	printf("  --norevgeo		-G      disable ghash to reverge-geo lookups\n");
	printf("  --precision		        ghash precision (dflt: %d)\n", GHASHPREC);
	printf("  --version		-v	print version information\n");
	printf("  --dump / --load [<db>]        dump/load content of db (default ghash)\n");
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
#ifdef WITH_MQTT
	printf("\tWITH_MQTT = yes\n");
#else
	printf("\tWITH_MQTT = no\n");
#endif
#ifdef WITH_HTTP
	printf("\tWITH_HTTP = yes\n");
#else
	printf("\tWITH_HTTP = no\n");
#endif
#ifdef WITH_LUA
	printf("\tWITH_LUA  = yes\n");
#endif
#ifdef WITH_ENCRYPT
	printf("\tWITH_ENCRYPT = yes\n");
#endif
#ifdef WITH_PING
	printf("\tWITH_PING = yes\n");
#endif
#ifdef WITH_KILL
	printf("\tWITH_KILL = yes\n");
#endif
	printf("\tCONFIGFILE = \"%s\"\n", CONFIGFILE);
	printf("\tSTORAGEDEFAULT = \"%s\"\n", STORAGEDEFAULT);
	printf("\tSTORAGEDIR = \"%s\"\n", STORAGEDIR);
	printf("\tDOCROOT = \"%s\"\n", DOCROOT);
	printf("\tGHASHPREC = %d\n", GHASHPREC);
	printf("\tDEFAULT_HISTORY_HOURS = %d\n", DEFAULT_HISTORY_HOURS);
	printf("\tJSON_INDENT = \"%s\"\n", (JSON_INDENT) ? JSON_INDENT : "NULL");
#if WITH_MQTT
	printf("\tLIBMOSQUITTO_VERSION = %d.%d.%d\n",
		LIBMOSQUITTO_MAJOR,
		LIBMOSQUITTO_MINOR,
		LIBMOSQUITTO_REVISION);
#endif
	printf("\tMDB VERSION = %s\n", MDB_VERSION_STRING);
#ifdef WITH_ENCRYPT
	printf("\tSODIUM VERSION = %s\n", SODIUM_VERSION_STRING);
#endif
	printf("\tGIT VERSION = %s\n", GIT_VERSION);

	exit(0);
}

int main(int argc, char **argv)
{
	char *progname = *argv, *p;
	int c;
	int list = 0, last = 0, limit = 0;
	char *lmdbname = NULL;
	int dumpghash = FALSE, loadghash = FALSE;
#if WITH_KILL
	int killdata = FALSE;
#endif
	int revgeo = TRUE;
	char *username = NULL, *device = NULL, *time_from = NULL, *time_to = NULL;
	JsonNode *json, *obj, *locs;
	time_t now, s_lo, s_hi;
	output_type otype = JSON;
	JsonNode *fields = NULL;
	FILE *xmlp = stdout;

	get_defaults(CONFIGFILE, NULL);

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
			{ "version",	no_argument,	0, 	'v'},
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
			{ "dump",	optional_argument, 0, 	3},
			{ "load",	optional_argument, 0, 	4},
#if WITH_KILL
			{ "killdata",	no_argument, 0, 	'K'},
#endif
			{ "norevgeo",	no_argument, 0, 	'G'},
		  	{0, 0, 0, 0}
		  };
		int optindex = 0;

		c = getopt_long(argc, argv, "hlu:d:F:T:f:KLS:GN:v", long_options, &optindex);
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
				dumpghash = TRUE;
				if (optarg)
					lmdbname = strdup(optarg);
				break;
			case 4:
				loadghash = TRUE;
				if (optarg)
					lmdbname = strdup(optarg);
				break;
			case 'v':
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
				else if (!strcmp(optarg, "xml"))
					otype = XML;
				else if (!strcmp(optarg, "gpx"))
					otype = GPX;
				else if (!strcmp(optarg, "csv"))
					otype = CSV;
				else {
					fprintf(stderr, "%s: unrecognized output format\n", progname);
					exit(2);
				}
				break;
#if WITH_KILL
			case 'K':
				killdata = 1;
				break;
#endif
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

	// printf("lmdbname = %s\n", (lmdbname) ? lmdbname : "NULL");

	if (loadghash) {
		storage_gcache_load(lmdbname);
		exit(0);
	}

	if (dumpghash) {
		storage_gcache_dump(lmdbname);
		exit(0);
	}

	storage_init(revgeo);


#if WITH_KILL
	if (killdata) {
		JsonNode *obj; //, *killed, *f;

		if (!username || !device) {
			fprintf(stderr, "%s: killdata requires username and device\n", progname);
			return -2;
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
		return 0;
	}
#endif

	if (last) {
		JsonNode *user_array;

		if ((user_array = last_users(username, device, fields)) != NULL) {

			if (otype == JSON) {
				char *js;
				if ((js = json_stringify(user_array, JSON_INDENT)) != NULL) {
					printf("%s\n", js);
					free(js);
				}
				json_delete(user_array);
			} else if (otype == CSV) {
				csv_output(user_array, CSV, fields, print_xml_line, xmlp);
			} else if (otype == XML) {
				xml_output(user_array, XML, fields, print_xml_line, xmlp);
			} else {
				fprintf(stderr, "%s: unsupported output type for LAST\n", progname);
			}
		}
		return 0;
	}

	lowercase(username);
	lowercase(device);

	if (!username && device) {
		fprintf(stderr, "%s: device name without username doesn't make sense\n", progname);
		return -2;
	}

	/* If no from time specified but limit, set from to this month */
	if (limit) {
		if (time_from == NULL) {
			time_from = strdup(yyyymm(now));
		}
	}

	if (make_times(time_from, &s_lo, time_to, &s_hi, 0) != 1) {
		fprintf(stderr, "%s: bad time(s) specified\n", progname);
		return -2;
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

		return 0;
	}

	if (argc == 0 && !username && !device) {
		fprintf(stderr, "%s: nothing to do. Specify filename or --user and --device\n", progname);
		return -1;
	} else if (argc == 0 && (!username || !device)) {
		fprintf(stderr, "%s: must specify username and device\n", progname);
		return -1;
	} else if ((username || device) && (argc > 0)) {
		fprintf(stderr, "%s: filename with --user and --device is not supported\n", progname);
		return -1;
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
			locations(argv[n], obj, locs, s_lo, s_hi, otype, 0, fields, NULL, NULL);
		}
	} else {
		JsonNode *arr, *f;

		/*
		 * Obtain a list of .rec files from lister(), possibly limited by s_lo/s_hi times,
		 * process each and build the JSON `obj' with an array of locations.
		 */

		if ((json = lister(username, device, s_lo, s_hi, (limit > 0) ? TRUE : FALSE)) != NULL) {
			int i_have = 0;

			if ((arr = json_find_member(json, "results")) != NULL) { // get array
				json_foreach(f, arr) {
					// locations(f->string_, obj, locs, s_lo, s_hi, otype, limit, fields, username, device);
					locations(f->string_, obj, locs, s_lo, s_hi, otype, limit, fields, NULL, NULL);
					if (limit) {
						i_have += limit;
						if (i_have >= limit)
							break;
					}
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
		csv_output(locs, CSV, fields, print_xml_line, xmlp);
	} else if (otype == XML) {
		xml_output(locs, XML, fields, print_xml_line, xmlp);
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

	return 0;
}
