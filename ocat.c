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
	GEOJSON = 0,
	CSV,
	JSON,
	RAW,
} output_type;

static void print_one(JsonNode *j)
{
	static char *ints[] = { "batt", "vel", "cog", "tst", "alt", NULL };
	char **p;

	/* Check if the value should be an "integer" (ie not float) */
	if (j->tag == JSON_NUMBER) {
		int isint = FALSE;
		for (p = ints; p && *p; p++) {
			if (!strcmp(j->key, *p)) {
				isint = TRUE;
				printf("%.lf", j->number_);
				break;
			}
		}
		if (!isint) {
			printf("%lf", j->number_);
		}
	} else if (j->tag == JSON_STRING) {
		printf("%s", j->string_);

	}
}

void csv_output(JsonNode *json, output_type otype, JsonNode *fields)
{
	JsonNode *node;
	JsonNode *arr, *one, *j;
	short virgin = 1;

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
					print_one(j);
					printf("%c", node->next ? ',' : '\n');
				}
			}
		} else {
			json_foreach(j, one) {
				print_one(j);
				printf("%c", j->next ? ',' : '\n');
			}
		}
	}
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
	printf("         specify <time> as     YYYY-MM-DDTHH:MM:SS\n");
	printf("                               YYYY-MM-DDTHH:MM\n");
	printf("                               YYYY-MM-DDTHH\n");
	printf("                               YYYY-MM-DD\n");
	printf("                               YYYY-MM\n");
	printf("  --format json    	-f     output format (default: JSON)\n");
	printf("           csv                 (overrides $OCAT_FORMAT\n");
	printf("           geojson\n");
	printf("           raw\n");
	printf("  --fields tst,lat,lon,...     Choose fields for CSV\n");
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
			{ "fields",	required_argument, 0, 	1},
			{ "killdata",	no_argument, 0, 	'K'},
		  	{0, 0, 0, 0}
		  };
		int optindex = 0;

		c = getopt_long(argc, argv, "hlu:d:F:T:f:KLS:", long_options, &optindex);
		if (c == -1)
			break;

		switch (c) {
			case 1:	/* No short option */
				fields = json_splitter(optarg, ",");
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

	} else if (otype == CSV) {
		csv_output(obj, CSV, fields);
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
