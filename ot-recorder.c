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
#include "utstring.h"
#include "utarray.h"
#include "geo.h"
#include "ghash.h"
#include "config.h"
#include "file.h"
#include "safewrite.h"
#include "base64.h"
#include "misc.h"


#define SSL_VERIFY_PEER (1)
#define SSL_VERIFY_NONE (0)

#define TOPIC_PARTS     (4)             /* owntracks/user/device/info */
#define TOPIC_SUFFIX    "info"

static int run = 1;

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

static const char *isotime(time_t t) {
	static char buf[] = "YYYY-MM-DDTHH:MM:SSZ";

	strftime(buf, sizeof(buf), "%FT%TZ", gmtime(&t));
	return(buf);
}

static const char *ltime(time_t t) {
	static char buf[] = "HH:MM:SS";

	strftime(buf, sizeof(buf), "%T", localtime(&t));
	return(buf);
}

void do_info(void *userdata, UT_string *username, UT_string *device, char *payload)
{
	struct udata *ud = (struct udata *)userdata;
	JsonNode *json, *j;
	static UT_string *name = NULL, *face = NULL;
#ifdef HAVE_REDIS
	redisReply *r;
#endif
	FILE *fp;
	char *img;

	utstring_renew(name);
	utstring_renew(face);

        if ((json = json_decode(payload)) == NULL) {
		fprintf(stderr, "Can't decode INFO payload for username=%s\n", utstring_body(username));
		return;
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

	if (ud->usefiles) {
		/* I know the payload is valid JSON: write card */

		if ((fp = pathn("wb", "cards", username, NULL, "json")) != NULL) {
			fprintf(fp, "%s\n", payload);
			fclose(fp);
		}
	}

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


#ifdef HAVE_REDIS
	if (ud->useredis) {
		redis_ping(&ud->redis);
		r = redisCommand(ud->redis, "HMSET card:%s name %s face %s", utstring_body(username), utstring_body(name), utstring_body(face));
		if (r)  { ; } /* FIXME */
	}
#endif

	/* We have a base64-encoded "face". Decode it and store binary image */
	if ((img = malloc(utstring_len(face))) != NULL) {
		int imglen;

		if ((imglen = base64_decode(utstring_body(face), img)) > 0) {
			if (ud->usefiles) {
				if ((fp = pathn("wb", "photos", username, NULL, "png")) != NULL) {
					fwrite(img, sizeof(char), imglen, fp);
					fclose(fp);
				}
			}

#ifdef HAVE_REDIS
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

	if (ud->usefiles) {
		/* I know the payload is valid JSON: write message */

		if ((fp = pathn("ab", "msg", username, NULL, "json")) != NULL) {
			fprintf(fp, "%s\n", payload);
			fclose(fp);
		}
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

	if (m->payloadlen == 0) {
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

	time(&now);

	monitorhook(ud, now, m->topic);

	/* FIXME: handle null leading topic `/` */
	utstring_printf(basetopic, "%s/%s/%s", topics[0], topics[1], topics[2]);
	utstring_printf(username, "%s", topics[1]);
	utstring_printf(device, "%s", topics[2]);

	if ((count == TOPIC_PARTS) && (strcmp(topics[count-1], TOPIC_SUFFIX) == 0)) {
		do_info(ud, username, device, m->payload);
	}

	/* owntracks/user/device/msg */
	if ((count == TOPIC_PARTS) && (strcmp(topics[count-1], "msg") == 0)) {
		if (m->retain == FALSE || ud->ignoreretained == FALSE) {
			do_msg(ud, username, device, m->payload);
		}
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


		if (ud->usefiles) {
			if ((fp = pathn("a", "rec", username, device, "rec")) != NULL) {

				fprintf(fp, RECFORMAT, isotime(now), utstring_body(reltopic), bindump(m->payload, m->payloadlen));
				fclose(fp);
			}
		}

		mosquitto_sub_topic_tokens_free(&topics, count);
		return;
        }

	if (utstring_len(reltopic) == 0)
		utstring_printf(reltopic, "-");

	mosquitto_sub_topic_tokens_free(&topics, count);

	if (m->retain == TRUE && ud->ignoreretained) {
		return;
	}

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

	/* FIXME: */

	cached = ghash_readcache(ud, utstring_body(ghash), addr, cc);
	if (!cached) {
		JsonNode *geo;

		if ((geo = revgeo(lat, lon, addr, cc)) != NULL) {
			// fprintf(stderr, "REVGEO: %s\n", utstring_body(addr));
			ghash_storecache(ud, geo, utstring_body(ghash), utstring_body(addr), utstring_body(cc));
			json_delete(geo);
		}
	}

	/*
	 * We have exactly three topic parts (owntracks/user/device), and valid JSON.
	 */

	/*
	 * Add a few bits to the JSON, and record it on a per-user/device basis.
        json_append_member(json, "ghash",    json_mkstring(utstring_body(ghash)));
	 */
	
	if ((jsonstring = json_stringify(json, NULL)) != NULL) {

#ifdef HAVE_REDIS
		if (ud->useredis) {
			/* add last to Redis as "lastpos:username-device" */
			last_storeredis(&ud->redis, utstring_body(username), utstring_body(device), jsonstring);
		}
#endif

		if (ud->usefiles) {
			if ((fp = pathn("a", "rec", username, device, "rec")) != NULL) {

				// fprintf(fp, "%s\n", jsonstring);
				fprintf(fp, RECFORMAT, isotime(now), "*", jsonstring);

				/* Now safewrite the last location */
				utstring_printf(ts, "%s/last/%s/%s",
					JSONDIR, utstring_body(username), utstring_body(device));
				if (mkpath(utstring_body(ts)) < 0) {
					perror(utstring_body(ts));
				}
				utstring_printf(ts, "/%s-%s.json",
					utstring_body(username), utstring_body(device));

				safewrite(utstring_body(ts), jsonstring);
				fclose(fp);
			}
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
		fprintf(stderr, "Subscribing to %s\n", *m);
		mosquitto_subscribe(mosq, &mid, *m, 0);
	}
}

void on_disconnect(struct mosquitto *mosq, void *userdata, int reason)
{
#ifdef HAVE_REDIS
	struct udata *ud = (struct udata *)userdata;
#endif

	fprintf(stderr, "Disconnected. Reason: %d [%s]\n", reason, mosquitto_strerror(reason));

	if (reason == 0) { 	// client wish
	#ifdef HAVE_REDIS
		redisFree(ud->redis);
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
	fprintf(stderr, "Usage: %s [-D] [-F] [-N] [-P prefix] [-R] topic [topic...]\n", prog);
	exit(2);
}

int main(int argc, char **argv)
{
	struct mosquitto *mosq = NULL;
	char err[1024], *p, *username, *password, *cafile;
	char *hostname = "localhost";
	int port = 1883;
	int rc, i, ch;
	static struct udata udata, *ud = &udata;
	struct utsname uts;
	UT_string *clientid;
#ifdef HAVE_REDIS
	struct timeval timeout = { 1, 500000 }; // 1.5 seconds
#endif
	char *progname = *argv;

	udata.usefiles		= TRUE;
	udata.ignoreretained	= TRUE;
	udata.pubprefix		= NULL;
	udata.skipdemo		= TRUE;
	udata.useredis		= TRUE;

	while ((ch = getopt(argc, argv, "DFRNP:")) != EOF) {
		switch (ch) {
			case 'D':
				ud->skipdemo = FALSE;
				break;
			case 'F':
				ud->usefiles = FALSE;
				break;
			case 'N':
				ud->useredis = FALSE;
				break;
			case 'P':
				udata.pubprefix = strdup(optarg);
				break;
			case 'R':
				ud->ignoreretained = FALSE;
				break;
			default:
				usage(*argv);
				break;
		}
	}

	argc -= (optind - 1);
	argv += (optind - 1);

	if (argc < 2) {
		usage(progname);
		return (-1);
	}

	if ((p = getenv("OTR_HOST")) != NULL) {
		hostname = strdup(p);
	}

	if ((p = getenv("OTR_PORT")) != NULL) {
		port = atoi(p);
	}

	revgeo_init();

	utstring_new(clientid);

	mosquitto_lib_init();

	utstring_printf(clientid, "ot-recorder");
	if (uname(&uts) == 0) {
		utstring_printf(clientid, "-%s", uts.nodename);
	}
	utstring_printf(clientid, "-%d", getpid());

	signal(SIGINT, catcher);

#ifdef HAVE_REDIS
	ud->redis = redisConnectWithTimeout("localhost", 6379, timeout);
#endif

	mosq = mosquitto_new(utstring_body(clientid), true, (void *)&udata);
	if (!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		mosquitto_lib_cleanup();
		return 1;
	}

	/*
	 * Pushing list of topics into the array so that we can (re)subscribe on_connect()
	 */

	utarray_new(ud->topics, &ut_str_icd);
	for (i = 1; i < argc; i++) {
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

	while (run) {
		rc = mosquitto_loop(mosq, 0, 1);
		if (run && rc) {
			fprintf(stderr, "loop sleep: rc=%d [%s]\n", rc, mosquitto_strerror(rc));
			sleep(10);
			mosquitto_reconnect(mosq);
		}
	}

	mosquitto_disconnect(mosq);

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

	return (0);
}
