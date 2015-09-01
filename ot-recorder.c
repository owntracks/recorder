#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <mosquitto.h>
#include <getopt.h>
#include <time.h>
#include "json.h"
#include <sys/utsname.h>
#include <syslog.h>
#include "utstring.h"
#include "utarray.h"
#include "geo.h"
#include "config.h"
#include "geohash.h"
#include "file.h"
#include "safewrite.h"
#include "base64.h"
#include "misc.h"
#include "util.h"
#include "storage.h"
#ifdef HAVE_LMDB
# include "gcache.h"
#endif
#ifdef HAVE_HTTP
# include "http.h"
#endif


#define SSL_VERIFY_PEER (1)
#define SSL_VERIFY_NONE (0)

#define TOPIC_PARTS     (4)             /* owntracks/user/device/info */
#define TOPIC_SUFFIX    "info"
#define DEFAULT_QOS	(2)
#define CLEAN_SESSION	false

static int run = 1;
#ifdef HAVE_HTTP
static struct mg_server *mgserver;
#endif

double number(JsonNode *j, char *element)
{
	JsonNode *m;
	double d;

	if ((m = json_find_member(j, element)) != NULL) {
		if (m->tag == JSON_NUMBER) {
			return (m->number_);
		} else if (m->tag == JSON_STRING) {
			d = atof(m->string_);
			/* Normalize to number */
			json_remove_from_parent(m);
			json_append_member(j, element, json_mknumber(d));
			return (d);
		}
	}

	return (-7.0L);
}

JsonNode *extract(struct udata *ud, char *payload, char *tid, char *t, double *lat, double *lon, long *tst)

{
	JsonNode *json, *j;

	*tid = *t = 0;
	*lat = *lon = -1.0L;

	if ((json = json_decode(payload)) == NULL)
		return (NULL);

	if (ud->skipdemo && (json_find_member(json, "_demo") != NULL)) {
		json_delete(json);
		return (NULL);
	}

	if ((j = json_find_member(json, "_type")) == NULL) {
		json_delete(json);
		return (NULL);
	}
	if ((j->tag != JSON_STRING) || (strcmp(j->string_, "location") != 0)) {
		json_delete(json);
		return (NULL);
	}

	if ((j = json_find_member(json, "tid")) != NULL) {
		if (j->tag == JSON_STRING) {
			// printf("I got: [%s]\n", m->string_);
			strcpy(tid, j->string_);
		}
	}

	if ((j = json_find_member(json, "t")) != NULL) {
		if (j && j->tag == JSON_STRING) {
			strcpy(t, j->string_);
		}
	}

	/*
	 * Normalize tst, lat, lon to numbers, particularly for Greenwich
	 * which produces strings currently.
	 */

	*tst = time(NULL);
	if ((j = json_find_member(json, "tst")) != NULL) {
		if (j && j->tag == JSON_STRING) {
			*tst = strtoul(j->string_, NULL, 10);
			json_remove_from_parent(j);
			json_append_member(json, "tst", json_mknumber(*tst));
		} else {
			*tst = (unsigned long)j->number_;
		}
	}

	*lat = number(json, "lat");
	*lon = number(json, "lon");

	return (json);
}

static const char *ltime(time_t t) {
	static char buf[] = "HH:MM:SS";

	strftime(buf, sizeof(buf), "%T", localtime(&t));
	return(buf);
}

/*
 * Process info/ message containing a CARD. If the payload is a card, return TRUE.
 */

int do_info(void *userdata, UT_string *username, UT_string *device, char *payload)
{
	struct udata *ud = (struct udata *)userdata;
	JsonNode *json, *j;
	static UT_string *name = NULL, *face = NULL;
	FILE *fp;
	char *img;
	int rc = FALSE;

	utstring_renew(name);
	utstring_renew(face);

        if ((json = json_decode(payload)) == NULL) {
		fprintf(stderr, "Can't decode INFO payload for username=%s\n", utstring_body(username));
		return (FALSE);
	}

	if (ud->skipdemo && (json_find_member(json, "_demo") != NULL)) {
		goto cleanup;
	}

	if ((j = json_find_member(json, "_type")) == NULL) {
		goto cleanup;
	}
	if ((j->tag != JSON_STRING) || (strcmp(j->string_, "card") != 0)) {
		goto cleanup;
	}

	/* I know the payload is valid JSON: write card */

	if ((fp = pathn("wb", "cards", username, NULL, "json")) != NULL) {
		fprintf(fp, "%s\n", payload);
		fclose(fp);
	}
	rc = TRUE;

	if ((j = json_find_member(json, "name")) != NULL) {
		if (j->tag == JSON_STRING) {
			// printf("I got: [%s]\n", j->string_);
			utstring_printf(name, "%s", j->string_);
		}
	}

	if ((j = json_find_member(json, "face")) != NULL) {
		if (j->tag == JSON_STRING) {
			// printf("I got: [%s]\n", j->string_);
			utstring_printf(face, "%s", j->string_);

		}
	}

	fprintf(stderr, "* CARD: %s-%s %s\n", utstring_body(username), utstring_body(device), utstring_body(name));


#ifdef HAVE_REDIS /* TODO: LMDB? */
	if (ud->useredis) {
		redis_ping(&ud->redis);
		r = redisCommand(ud->redis, "HMSET card:%s name %s face %s", utstring_body(username), utstring_body(name), utstring_body(face));
	}
#endif

	/* We have a base64-encoded "face". Decode it and store binary image */
	if ((img = malloc(utstring_len(face))) != NULL) {
		int imglen;

		if ((imglen = base64_decode(utstring_body(face), img)) > 0) {
			if ((fp = pathn("wb", "photos", username, NULL, "png")) != NULL) {
				fwrite(img, sizeof(char), imglen, fp);
				fclose(fp);
			}

#ifdef HAVE_REDIS /* TODO: LMDB ? */
			if (ud->useredis) {
				/* Add photo (binary) to Redis as photo:username */
				redis_ping(&ud->redis);
				r = redisCommand(ud->redis, "SET photo:%s %b", utstring_body(username), img, imglen);
			}
#endif
		}
		free(img);
	}


    cleanup:
	json_delete(json);
	return (rc);
}

void do_msg(void *userdata, UT_string *username, UT_string *device, char *payload)
{
	struct udata *ud = (struct udata *)userdata;
	JsonNode *json, *j;
	FILE *fp;

        if ((json = json_decode(payload)) == NULL) {
		fprintf(stderr, "Can't decode MSG payload for username=%s, device=%s\n",
			utstring_body(username), utstring_body(device));
		return;
	}

	if (ud->skipdemo && (json_find_member(json, "_demo") != NULL)) {
		goto cleanup;
	}

	if ((j = json_find_member(json, "_type")) == NULL) {
		goto cleanup;
	}
	if ((j->tag != JSON_STRING) || (strcmp(j->string_, "msg") != 0)) {
		goto cleanup;
	}

	/* I know the payload is valid JSON: write message */

	if ((fp = pathn("ab", "msg", username, NULL, "json")) != NULL) {
		fprintf(fp, "%s\n", payload);
		fclose(fp);
	}

	fprintf(stderr, "* MSG: %s-%s\n", utstring_body(username), utstring_body(device));

    cleanup:
	json_delete(json);
}

void republish(struct mosquitto *mosq, struct udata *userdata, char *username, char *topic, double lat, double lon, char *cc, char *addr, long tst, char *t)
{
	struct udata *ud = (struct udata *)userdata;
        JsonNode *json;
	static UT_string *newtopic = NULL;
        char *payload;

	if (ud->pubprefix == NULL)
		return;

        if ((json = json_mkobject()) == NULL) {
                return;
        }

	utstring_renew(newtopic);

	utstring_printf(newtopic, "%s/%s", ud->pubprefix, topic);

        json_append_member(json, "username",	json_mkstring(username));
        json_append_member(json, "topic",	json_mkstring(topic));
        json_append_member(json, "cc",		json_mkstring(cc));
        json_append_member(json, "addr",	json_mkstring(addr));
        json_append_member(json, "t",		json_mkstring(t));
        json_append_member(json, "tst",		json_mknumber(tst));
        json_append_member(json, "lat",		json_mknumber(lat));
        json_append_member(json, "lon",		json_mknumber(lon));


        if ((payload = json_stringify(json, NULL)) != NULL) {
                mosquitto_publish(mosq, NULL, utstring_body(newtopic),
                                strlen(payload), payload, 1, true);
		fprintf(stderr, "%s %s\n", utstring_body(newtopic), payload);
                free(payload);
        }

        json_delete(json);

}

/*
 * Decode OwnTracks CSV and return a new JsonNode to a JSON object.
 */

#define MILL 1000000.0

JsonNode *csv(char *payload, char *tid, char *t, double *lat, double *lon, long *tst)
{
	JsonNode *json = NULL;
        double dist = 0;
	char tmptst[40];
	double vel, trip, alt, cog;

        if (sscanf(payload, "%[^,],%[^,],%[^,],%lf,%lf,%lf,%lf,%lf,%lf,%lf", tid, tmptst, t, lat, lon, &cog, &vel, &alt, &dist, &trip) != 10) {
		// fprintf(stderr, "**** payload not CSV: %s\n", payload);
                return (NULL);
        }

        *lat /= MILL;
        *lon /= MILL;
        cog *= 10;
        alt *= 10;
        trip *= 1000;

	*tst = strtoul(tmptst, NULL, 16);

	json = json_mkobject();
	json_append_member(json, "_type", json_mkstring("location"));
	json_append_member(json, "t",	  json_mkstring(t));
	json_append_member(json, "tid",	  json_mkstring(tid));
	json_append_member(json, "tst",	  json_mknumber(*tst));
	json_append_member(json, "lat",	  json_mknumber(*lat));
	json_append_member(json, "lon",	  json_mknumber(*lon));
	json_append_member(json, "cog",	  json_mknumber(cog));
	json_append_member(json, "vel",	  json_mknumber(vel));
	json_append_member(json, "alt",	  json_mknumber(alt));
	json_append_member(json, "dist",  json_mknumber(dist));
	json_append_member(json, "trip",  json_mknumber(trip));
	json_append_member(json, "csv",   json_mkbool(1));

	return (json);
}
#define RECFORMAT "%s\t%-18s\t%s\n"

void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *m)
{
	JsonNode *json;
	char tid[BUFSIZ], t[BUFSIZ], *p;
	double lat, lon;
	long tst;
	struct udata *ud = (struct udata *)userdata;
	FILE *fp;
        char **topics;
        int count = 0, cached;
	static UT_string *basetopic = NULL, *username = NULL, *device = NULL, *addr = NULL, *cc = NULL, *ghash = NULL, *ts = NULL;
	static UT_string *reltopic = NULL;
	char *jsonstring;
	time_t now;

	/*
	 * mosquitto_message->
	 * 	 int mid;
	 * 	 char *topic;
	 * 	 void *payload;
	 * 	 int payloadlen;
	 * 	 int qos;
	 * 	 bool retain;
	 */

	time(&now);
	monitorhook(ud, now, m->topic);

	if (m->payloadlen == 0) {
		return;
	}

	if (m->retain == TRUE && ud->ignoreretained) {
		return;
	}

	// printf("%s %s\n", m->topic, bindump(m->payload, m->payloadlen)); fflush(stdout);


	utstring_renew(ts);
	utstring_renew(basetopic);
	utstring_renew(username);
	utstring_renew(device);

        if (mosquitto_sub_topic_tokenise(m->topic, &topics, &count) != MOSQ_ERR_SUCCESS) {
		return;
	}


	/* FIXME: handle null leading topic `/` */
	utstring_printf(basetopic, "%s/%s/%s", topics[0], topics[1], topics[2]);
	utstring_printf(username, "%s", topics[1]);
	utstring_printf(device, "%s", topics[2]);

	if ((count == TOPIC_PARTS) && (strcmp(topics[count-1], TOPIC_SUFFIX) == 0)) {
		if (do_info(ud, username, device, m->payload) == TRUE) {  /* this was a card */
			return;
		}
	}

	/* owntracks/user/device/msg */
	if ((count == TOPIC_PARTS) && (strcmp(topics[count-1], "msg") == 0)) {
		do_msg(ud, username, device, m->payload);
	}

	/*
	 * Determine "relative topic", relative to base, i.e. whatever comes behind
	 * ownntracks/user/device/
	 */

	utstring_renew(reltopic);
	if (count != 3) {
		/*
		 * Not a normal location publish. Build up a string consisting of the remaining
		 * topic parts, i.e. whatever is after base topic, and record with whatever
		 * (hopefully non-binary) payload we got.
		 */

		int j;

		for (j = 3; j < count; j++) {
			utstring_printf(reltopic, "%s%c", topics[j], (j < count - 1) ? '/' : ' ');
		}


		if ((fp = pathn("a", "rec", username, device, "rec")) != NULL) {

			fprintf(fp, RECFORMAT, isotime(now), utstring_body(reltopic), bindump(m->payload, m->payloadlen));
			fclose(fp);
		}

		mosquitto_sub_topic_tokens_free(&topics, count);
		return;
        }

	if (utstring_len(reltopic) == 0)
		utstring_printf(reltopic, "-");

	mosquitto_sub_topic_tokens_free(&topics, count);


	/*
	 * Try to decode JSON payload to find _type: location. If that doesn't work,
	 * see if it's OwnTracks' Greenwich CSV
	 */

	json = extract(ud, m->payload, tid, t, &lat, &lon, &tst);
	if (json == NULL) {
		/* Is it OwnTracks Greenwich CSV? */

		if ((json = csv(m->payload, tid, t, &lat, &lon, &tst)) == NULL) {
			/* It's not JSON or it's not a location CSV; store it */
			/* It may be an lwt */
			if ((fp = pathn("a", "rec", username, device, "rec")) != NULL) {
				fprintf(fp, RECFORMAT, isotime(now),
					utstring_body(reltopic),
					 bindump(m->payload, m->payloadlen));
				fclose(fp);
			}
			return;
		}
		// fprintf(stderr, "+++++ %s\n", json_stringify(json, NULL));
	}

#if 0
	if (*t && (!strcmp(t, "p") || !strcmp(t, "b"))) {
		// fprintf(stderr, "Ignore `t:%s' for %s\n", t, m->topic);
		json_delete(json);
		return;
	}
#endif

	/*
	 * We are now processing a _type location.
	 */

	utstring_renew(ghash);
        p = geohash_encode(lat, lon, GEOHASH_PREC);
	if (p != NULL) {
		utstring_printf(ghash, "%s", p);
		free(p);
	}

	utstring_renew(addr);
	utstring_renew(cc);

	cached = FALSE;
	if (ud->revgeo == TRUE) {
		JsonNode *geo, *j;

		if ((geo = gcache_json_get(ud->gc, utstring_body(ghash))) != NULL) {
			/* Habemus cached data */
			
			cached = TRUE;

			if ((j = json_find_member(geo, "cc")) != NULL) {
				utstring_printf(cc, "%s", j->string_);
			}
			if ((j = json_find_member(geo, "addr")) != NULL) {
				utstring_printf(addr, "%s", j->string_);
			}
			
		} else {
			if ((geo = revgeo(lat, lon, addr, cc)) != NULL) {
				gcache_json_put(ud->gc, utstring_body(ghash), geo);
			} else {
				/* We didn't obtain reverse Geo, maybe because of over
				 * quota; make a note of the missing geohash */

				char gfile[BUFSIZ];
				FILE *fp;

				snprintf(gfile, BUFSIZ, "%s/ghash/missing", STORAGEDIR);
				if ((fp = fopen(gfile, "a")) != NULL) {
					fprintf(fp, "%s %lf %lf\n", utstring_body(ghash), lat, lon);
					fclose(fp);
				}
			}
		}
	} else {
		utstring_printf(cc, "??");
		utstring_printf(addr, "n.a.");
	}

	/*
	 * We have exactly three topic parts (owntracks/user/device), and valid JSON.
	 */

	
	if ((jsonstring = json_stringify(json, NULL)) != NULL) {
		char *js;

#ifdef HAVE_REDIS /* TODO: shall we store last positions in LMDB? */
		if (ud->useredis) {
			/* add last to Redis as "lastpos:username-device" */
			last_storeredis(&ud->redis, utstring_body(username), utstring_body(device), jsonstring);
		}
#endif

		if ((fp = pathn("a", "rec", username, device, "rec")) != NULL) {

			fprintf(fp, RECFORMAT, isotime(now), "*", jsonstring);
			fclose(fp);
		}


		/* Keep track of original username & device name in LAST. */
		json_append_member(json, "username",    json_mkstring(utstring_body(username)));
		json_append_member(json, "device",    json_mkstring(utstring_body(device)));
		json_append_member(json, "topic",    json_mkstring(m->topic));
		json_append_member(json, "ghash",    json_mkstring(utstring_body(ghash)));

		if ((js = json_stringify(json, NULL)) != NULL) {
			/* Now safewrite the last location */
			utstring_printf(ts, "%s/last/%s/%s",
				STORAGEDIR, utstring_body(username), utstring_body(device));
			if (mkpath(utstring_body(ts)) < 0) {
				perror(utstring_body(ts));
			}
			utstring_printf(ts, "/%s-%s.json",
				utstring_body(username), utstring_body(device));

			safewrite(utstring_body(ts), js);
			free(js);
		}
		free(jsonstring);
	}

	/* publish  */
	// republish(mosq, ud, utstring_body(username), m->topic, lat, lon, utstring_body(cc), utstring_body(addr), tst, t);

	if (*t == 0) {
		strcpy(t, " ");
	}

	fprintf(stderr, "%c %s %-35s t=%-1.1s tid=%-2.2s loc=%.5f,%.5f [%s] %s (%s)\n",
		(cached) ? '*' : '-',
		ltime(tst),
		m->topic,
		t,
		tid,
		lat, lon,
		utstring_body(cc),
		utstring_body(addr),
		utstring_body(ghash)
		);
	

	json_delete(json);
}

void on_connect(struct mosquitto *mosq, void *userdata, int rc)
{
	struct udata *ud = (struct udata *)userdata;
	int mid;
	char **m = NULL;

	while ((m = (char **)utarray_next(ud->topics, m))) {
		syslog(LOG_DEBUG, "Subscribing to %s (qos=%d)", *m, ud->qos);
		mosquitto_subscribe(mosq, &mid, *m, ud->qos);
	}
}

void on_disconnect(struct mosquitto *mosq, void *userdata, int reason)
{
#ifdef HAVE_LMDB
	struct udata *ud = (struct udata *)userdata;
#endif

	syslog(LOG_INFO, "Disconnected. Reason: %d [%s]", reason, mosquitto_strerror(reason));

	if (reason == 0) { 	// client wish
#ifdef HAVE_LMDB
		gcache_close(ud->gc);
#endif
	}
}

static void catcher(int sig)
{
        fprintf(stderr, "Going down on signal %d\n", sig);

        exit(1);
}

void usage(char *prog)
{
	printf("Usage: %s [options..] topic [topic ...]\n", prog);
	printf("  --help		-h	this message\n");
	printf("  --storage		-S     storage dir (./store)\n");
	printf("  --norevgeo		-G     disable ghash to reverge-geo lookups\n");
	printf("  --skipdemo 		-D     do not handle objects with _demo\n");
	printf("  --useretained		-R     process retained messages (default: no)\n");
	printf("  --clientid		-i     MQTT client-ID\n");
	printf("  --qos			-q     MQTT QoS (dflt: 2)\n");
	printf("  --pubprefix		-P     republish prefix (dflt: no republish)\n");
	printf("  --host		-H     MQTT host (localhost)\n");
	printf("  --port		-p     MQTT port (1883)\n");
	printf("  --logfacility		       syslog facility (local0)\n");
#ifdef HAVE_HTTP
	printf("  --http-host <host>	       HTTP addr to bind to (localhost)\n");
	printf("  --http-port <port>	-A     HTTP port (8083)\n");
	printf("  --doc-root <directory>       document root (./wdocs)\n");
#endif

	exit(1);
}


int main(int argc, char **argv)
{
	struct mosquitto *mosq = NULL;
	char err[1024], *p, *username, *password, *cafile;
	char *hostname = "localhost", *logfacility = "local0";
	int port = 1883;
	int rc, i, ch;
	static struct udata udata, *ud = &udata;
	struct utsname uts;
	UT_string *clientid;
#ifdef HAVE_HTTP
	int http_port = 8083;
	char *doc_root = "./wdocs";
	char *http_host = "localhost";
#endif
	char *progname = *argv;

	udata.qos		= DEFAULT_QOS;
	udata.ignoreretained	= TRUE;
	udata.pubprefix		= NULL;
	udata.skipdemo		= TRUE;
	udata.revgeo		= TRUE;
#ifdef HAVE_LMDB
	udata.gc		= NULL;
#endif
#ifdef HAVE_HTTP
	mgserver = udata.server = mg_create_server(NULL, ev_handler);
#endif

	if ((p = getenv("OTR_HOST")) != NULL) {
		hostname = strdup(p);
	}

	if ((p = getenv("OTR_PORT")) != NULL) {
		port = atoi(p);
	}

	if ((p = getenv("OTR_STORAGEDIR")) != NULL) {
		strcpy(STORAGEDIR, p);
	}

	utstring_new(clientid);
	utstring_printf(clientid, "ot-recorder");
	if (uname(&uts) == 0) {
		utstring_printf(clientid, "-%s", uts.nodename);
	}
	utstring_printf(clientid, "-%d", getpid());

	while (1) {
		static struct option long_options[] = {
			{ "help",	no_argument,		0, 	'h'},
			{ "skipdemo",	no_argument,		0, 	'D'},
			{ "norevgeo",	no_argument,		0, 	'G'},
			{ "useretained",	no_argument,		0, 	'R'},
			{ "clientid",	required_argument,	0, 	'i'},
			{ "pubprefix",	required_argument,	0, 	'P'},
			{ "qos",	required_argument,	0, 	'q'},
			{ "host",	required_argument,	0, 	'H'},
			{ "port",	required_argument,	0, 	'p'},
			{ "storage",	required_argument,	0, 	'S'},
			{ "logfacility",	required_argument,	0, 	4},
#ifdef HAVE_HTTP
			{ "http-host",	required_argument,	0, 	3},
			{ "http-port",	required_argument,	0, 	'A'},
			{ "doc-root",	required_argument,	0, 	2},
#endif
			{0, 0, 0, 0}
		  };
		int optindex = 0;

		ch = getopt_long(argc, argv, "hDGRi:P:q:S:H:p:A:", long_options, &optindex);
		if (ch == -1)
			break;

		switch (ch) {
			case 4:
				logfacility = strdup(optarg);
				break;
#ifdef HAVE_HTTP
			case 'A':	/* API */
				http_port = atoi(optarg);
				break;
			case 2:		/* no short char */
				doc_root = strdup(optarg);
				break;
			case 3:		/* no short char */
				http_host = strdup(optarg);
				break;
#endif
			case 'D':
				ud->skipdemo = FALSE;
				break;
			case 'G':
				ud->revgeo = FALSE;
				break;
			case 'i':
				utstring_clear(clientid);
				utstring_printf(clientid, "%s", optarg);
				break;
			case 'P':
				udata.pubprefix = strdup(optarg);	/* TODO: do we want this? */
				break;
			case 'q':
				ud->qos = atoi(optarg);
				if (ud->qos < 0 || ud->qos > 2) {
					fprintf(stderr, "%s: illegal qos\n", progname);
					exit(2);
				}
				break;
			case 'R':
				ud->ignoreretained = FALSE;
				break;
			case 'H':
				hostname = strdup(optarg);
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'S':
				strcpy(STORAGEDIR, optarg);
				break;
			case 'h':
				usage(progname);
				exit(0);
			default:
				abort();
		}

	}

	argc -= (optind);
	argv += (optind);

	if (argc < 1) {
		usage(progname);
		return (-1);
	}

	openlog("ot-recorder", LOG_PID | LOG_PERROR, syslog_facility_code(logfacility));

#ifdef HAVE_HTTP
	if (!is_directory(doc_root)) {
		syslog(LOG_ERR, "%s is not a directory", doc_root);
		exit(1);
	}
#endif
	syslog(LOG_DEBUG, "starting");

	if (ud->revgeo == TRUE) {
#ifdef HAVE_LMDB
		char db_filename[BUFSIZ], *pa;

		snprintf(db_filename, BUFSIZ, "%s/ghash", STORAGEDIR);
		pa = strdup(db_filename);
		mkpath(pa);
		free(pa);
		udata.gc = gcache_open(db_filename, FALSE);
		if (udata.gc == NULL) {
			syslog(LOG_WARNING, "Can't initialize gcache in %s", db_filename);
			exit(1);
		}
#endif
		revgeo_init();
	}


	mosquitto_lib_init();


	signal(SIGINT, catcher);

	mosq = mosquitto_new(utstring_body(clientid), CLEAN_SESSION, (void *)&udata);
	if (!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		mosquitto_lib_cleanup();
		return 1;
	}

	/*
	 * Pushing list of topics into the array so that we can (re)subscribe on_connect()
	 */

	utarray_new(ud->topics, &ut_str_icd);
	for (i = 0; i < argc; i++) {
		utarray_push_back(ud->topics, &argv[i]);
	}

	mosquitto_reconnect_delay_set(mosq,
			2, 	/* delay */
			20,	/* delay_max */
			0);	/* exponential backoff */

	mosquitto_message_callback_set(mosq, on_message);
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_disconnect_callback_set(mosq, on_disconnect);

	if ((username = getenv("OTR_USER")) != NULL) {
		if ((password = getenv("OTR_PASS")) != NULL) {
			mosquitto_username_pw_set(mosq, username, password);
		}
	}

	cafile = getenv("OTR_CAFILE");

	if (cafile && *cafile) {

                rc = mosquitto_tls_set(mosq,
                        cafile,                 /* cafile */
                        NULL,                   /* capath */
                        NULL,                   /* certfile */
                        NULL,                   /* keyfile */
                        NULL                    /* pw_callback() */
                        );
                if (rc != MOSQ_ERR_SUCCESS) {
                        fprintf(stderr, "Cannot set TLS CA: %s (check path names)\n",
                                mosquitto_strerror(rc));
                        exit(3);
                }

                mosquitto_tls_opts_set(mosq,
                        SSL_VERIFY_PEER,
                        NULL,                   /* tls_version: "tlsv1.2", "tlsv1" */
                        NULL                    /* ciphers */
                        );

	}


	rc = mosquitto_connect(mosq, hostname, port, 60);
	if (rc) {
		if (rc == MOSQ_ERR_ERRNO) {
			strerror_r(errno, err, 1024);
			fprintf(stderr, "Error: %s\n", err);
		} else {
			fprintf(stderr, "Unable to connect (%d) [%s].\n", rc, mosquitto_strerror(rc));
		}
		mosquitto_lib_cleanup();
		return rc;
	}

#ifdef HAVE_HTTP
	if (http_port) {
		char address[BUFSIZ];

		sprintf(address, "%s:%d", http_host, http_port);

		mg_set_option(udata.server, "listening_port", address);
		// mg_set_option(udata.server, "listening_port", "8090,ssl://8091:cert.pem");

		// mg_set_option(udata.server, "ssl_certificate", "cert.pem");
		// mg_set_option(udata.server, "listening_port", "8091");

		mg_set_option(udata.server, "document_root", doc_root);
		mg_set_option(udata.server, "enable_directory_listing", "yes");
		// mg_set_option(udata.server, "access_log_file", "access.log");
		// mg_set_option(udata.server, "cgi_pattern", "**.cgi");

		syslog(LOG_INFO, "HTTP listener started on %s", mg_get_option(udata.server, "listening_port"));
	}
#endif

	while (run) {
		rc = mosquitto_loop(mosq, /* timeout */ 200, /* max-packets */ 1);
		if (run && rc) {
			syslog(LOG_INFO, "MQTT connection: rc=%d [%s]. Sleeping...", rc, mosquitto_strerror(rc));
			sleep(10);
			mosquitto_reconnect(mosq);
		}
#ifdef HAVE_HTTP
		if (http_port) {
			mg_poll_server(udata.server, 10);
		}
#endif
	}

#ifdef HAVE_HTTP
	mg_destroy_server(&udata.server);
#endif
	mosquitto_disconnect(mosq);

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

	return (0);
}
