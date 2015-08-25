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

typedef enum {
	TABULAR   = 0,
	GEOJSON,
	CSV,
	JSON,
	RAW,
} output_type;

#if 0
void csv_output(JsonNode *json, output_type otype)
{
	JsonNode *arr, *one, *j;
	short virgin = 1;

	arr = json_find_member(json, "locations");
	json_foreach(one, arr) {
		if (virgin) {
			virgin = !virgin;

			/* Print headings from key names */
			json_foreach(j, one) {
				if (j->key)
					printf("%s%c", j->key, (j->next) ? ',' : '\n');
			}
		}
		/* Now the values */
		json_foreach(j, one) {
			if (j->tag == JSON_STRING) {
				printf("%s%c", j->string_, (j->next) ? ',' : '\n');
			} else if (j->tag == JSON_NUMBER) {
				/* hmm; what do I do with ints ? */
				printf("%lf%c", j->number_, (j->next) ? ',' : '\n');
			}
		}
	}
}
#endif

#if 1
void csv_output(JsonNode *json, output_type otype)
{
	JsonNode *arr, *one, *j;
	time_t tst = 0L;
	double lat = 0.0, lon = 0.0;
	char *tid = "", *addr;

	if (otype == CSV) {
		printf("tst,tid,lat,lon,addr\n");
	}

	arr = json_find_member(json, "locations");
	json_foreach(one, arr) {
		tid = addr = "";
		lat = lon = 0.0;

		if ((j = json_find_member(one, "tid")) != NULL) {
			tid = j->string_;
		}

		if ((j = json_find_member(one, "addr")) != NULL) {
			addr = j->string_;
		}

		if ((j = json_find_member(one, "tst")) != NULL) {
			tst = j->number_;
		}

		if ((j = json_find_member(one, "lat")) != NULL) {
			lat = j->number_;
		}

		if ((j = json_find_member(one, "lon")) != NULL) {
			lon = j->number_;
		}

		if (otype == CSV) {
			printf("%s,%s,%lf,%lf,%s\n",
				isotime(tst),
				tid,
				lat,
				lon,
				addr);
		} else {
			printf("%s  %-2.2s %9.5lf %9.5lf %s\n",
				isotime(tst),
				tid,
				lat,
				lon,
				addr);
		}
	}
}
#endif

void usage(char *prog)
{
	printf("Usage: %s [options..] [file ...]\n", prog);
	printf("  --help		-h	this message\n");
	printf("  --list		-l	list users (or a user's (-u) devices\n");
	printf("  --user username	-u	specify username ($OCAT_USERNAME)\n");
	printf("  --device devicename   -d	specify device name ($OCAT_DEVICE)\n");
	printf("  --from <time>         -F      from date/time; default -6H\n");
	printf("  --to   <time>         -T      to date/time; default now\n");
	printf("         specify <time> as     YYYY-MM-DDTHH:MM:SS\n");
	printf("                               YYYY-MM-DDTHH:MM\n");
	printf("                               YYYY-MM-DDTHH\n");
	printf("                               YYYY-MM-DD\n");
	printf("                               YYYY-MM\n");
	printf("  --format json    	-f     output format (default: JSON)\n");
	printf("           csv                 (overrides $OCAT_FORMAT\n");
	printf("           geojson\n");
	printf("           raw\n");
	printf("           tabular\n");
	printf("  --last		-L     JSON object with last users\n");
	printf("  --killdata                   requires -u and -d\n");
	printf("  --storage		-S     storage dir (./store)\n");

	exit(1);
}

int main(int argc, char **argv)
{
	char *progname = *argv, *p;
	int c;
	int list = 0, killdata = 0, last = 0;
	char *username = NULL, *device = NULL, *time_from = NULL, *time_to = NULL;
	JsonNode *json, *obj, *locs;
	time_t now, s_lo, s_hi;
	output_type otype = JSON;

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
			case 't': otype = TABULAR; break;
		}
	}

	if ((p = getenv("OCAT_STORAGEDIR")) != NULL) {
		strcpy(STORAGEDIR, p);
	}

	time(&now);

	while (1) {
		static struct option long_options[] = {
			{ "help",	no_argument,	0, 	'h'},
			{ "list",	no_argument,	0, 	'l'},
			{ "user",	required_argument, 0, 	'u'},
			{ "device",	required_argument, 0, 	'd'},
			{ "from",	required_argument, 0, 	'F'},
			{ "to",		required_argument, 0, 	'T'},
			{ "format",	required_argument, 0, 	'f'},
			{ "storage",	required_argument, 0, 	'S'},
			{ "last",	no_argument, 0, 	'L'},
			{ "killdata",	no_argument, 0, 	'K'},
		  	{0, 0, 0, 0}
		  };
		int optindex = 0;

		c = getopt_long(argc, argv, "hlu:d:F:T:f:KL", long_options, &optindex);
		if (c == -1)
			break;

		switch (c) {
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
			case 'S':
				strcpy(STORAGEDIR, optarg);
				break;
			case 'T':
				time_to = strdup(optarg);
				break;
			case 'f':
				if (!strcmp(optarg, "json"))
					otype = JSON;
				else if (!strcmp(optarg, "tabular"))
					otype = TABULAR;
				else if (!strcmp(optarg, "geojson"))
					otype = GEOJSON;
				else if (!strcmp(optarg, "raw"))
					otype = RAW;
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

			js = json_stringify(obj, " ");
			puts(js);
			free(js);
			json_delete(obj);
		}
		return (0);
	}

	if (last) {
		JsonNode *obj;
		char *js;

		obj = last_users();
		js = json_stringify(obj, " ");
		printf("%s\n", js);

		free(js);
		json_delete(obj);
		return (0);
	}

	if (!username && device) {
		fprintf(stderr, "%s: device name without username doesn't make sense\n", progname);
		return (-2);
	}

	if (make_times(time_from, &s_lo, time_to, &s_hi) != 1) {
		fprintf(stderr, "%s: bad time(s) specified\n", progname);
		return (-2);
	}

	if (list) {
		char *js;

		json = lister(username, device, 0, s_hi);
		if (json == NULL) {
			fprintf(stderr, "%s: cannot list\n", progname);
			exit(2);
		}
		if (otype == JSON) {
			js = json_stringify(json, " ");
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
			locations(argv[n], obj, locs, s_lo, s_hi, (otype == RAW) ? 1 : 0);
		}
	} else {
		JsonNode *arr, *f;

		if ((json = lister(username, device, s_lo, s_hi)) != NULL) {
			if ((arr = json_find_member(json, "results")) != NULL) { // get array
				json_foreach(f, arr) {
					// fprintf(stderr, "%s\n", f->string_);
					locations(f->string_, obj, locs, s_lo, s_hi, (otype == RAW) ? 1 : 0);
				}
			}
			json_delete(json);
		}
	}

	json_append_member(obj, "locations", locs);


	if (otype == JSON) {
		char *js = json_stringify(obj, " ");

		if (js != NULL) {
			printf("%s\n", js);
			free(js);
		}

	} else if (otype == TABULAR) {
		csv_output(obj, TABULAR);
	} else if (otype == CSV) {
		csv_output(obj, CSV);
	} else if (otype == RAW) {
		/* We've already done what we need to do in locations() */
	} else if (otype == GEOJSON) {
		JsonNode *geojson = geo_json(locs);
		char *js;

		if (geojson != NULL) {
			js = json_stringify(geojson, " ");
			if (js != NULL) {
				printf("%s\n", js);
				free(js);
			}
		}
	}

	return (0);
}
