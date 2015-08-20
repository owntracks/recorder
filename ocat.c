#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include "json.h"
#include "storage.h"

static char *tstampyyyymm(time_t t) {
        static char buf[] = "YYYY-MM";

        strftime(buf, sizeof(buf), "%Y-%m", gmtime(&t));
        return(buf);
}

void usage(char *prog)
{
	printf("Usage: %s [options..] [file ...]\n", prog);
	printf("  --help		-h	this message\n");
	printf("  --list		-l	list users (or a user's (-u) devices\n");
	printf("  --user username	-u	specify username\n");
	printf("  --device devicename   -d	specify device name\n");
	printf("  --yyyymm YYYY-MM	-D	specify year-month (shell pat, eg: '2015-0[572]')\n");
	exit(1);
}

int main(int argc, char **argv)
{
	char *progname = *argv;
	int c;
	int list = 0;
	char *username = NULL, *device = NULL, *yyyymm = NULL;
	JsonNode *json, *obj, *locs;
	time_t now;

	time(&now);
	yyyymm = tstampyyyymm(now);

	while (1) {
		static struct option long_options[] = {
			{ "help",	no_argument,	0, 	'h'},
			{ "list",	no_argument,	0, 	'l'},
			{ "user",	required_argument, 0, 	'u'},
			{ "device",	required_argument, 0, 	'd'},
			{ "yyyymm",	required_argument, 0, 	'D'},
		  	{0, 0, 0, 0}
		  };
		int optindex = 0;

		c = getopt_long(argc, argv, "hlu:d:D:", long_options, &optindex);
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
			case 'D':
				yyyymm = strdup(optarg);
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

	if (!username && device) {
		fprintf(stderr, "%s: device name without username doesn't make sense\n", progname);
		return (-2);
	}

	if (list) {
		char *js;

		json = lister(username, device, yyyymm);
		if (json == NULL) {
			fprintf(stderr, "%s: cannot list\n", progname);
			exit(2);
		}
		js = json_stringify(json, " ");
		printf("%s\n", js);
		json_delete(json);
		free(js);
	}

	if (argc == 0 && !username && !device) {
		fprintf(stderr, "%s: nothing to do. Specify filename or --user and --device\n", progname);
		return (-1);
	} else if (username && device && (argc > 0)) {
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
			locations(argv[n], obj, locs);
		}
	} else {
		JsonNode *arr, *f;

		if ((json = lister(username, device, yyyymm)) != NULL) {
			if ((arr = json_find_member(json, "results")) != NULL) { // get array
				json_foreach(f, arr) {
					printf("%s\n", f->string_);
					locations(f->string_, obj, locs);
				}
			}
			json_delete(json);
		}
	}

	json_append_member(obj, "locations", locs);
	printf("%s\n", json_stringify(obj, " "));

	return (0);
}
