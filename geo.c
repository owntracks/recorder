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
#include <time.h>
#include <curl/curl.h>
#include "utstring.h"
#include "config.h"
#include "geo.h"
#include "json.h"

static CURL *curl;

static size_t writemem(void *contents, size_t size, size_t nmemb, void *userp)
{
	UT_string *cbuf = (UT_string *)userp;
	size_t realsize = size * nmemb;

	utstring_bincpy(cbuf, contents, realsize);

	return (realsize);
}

static int goog_decode(UT_string *geodata, UT_string *addr, UT_string *cc)
{
	JsonNode *json, *results, *address, *ac, *zeroth, *j;

	/*
	* We are parsing this. I want the formatted_address in `addr' and
	* the country code short_name in `cc'
	*
	* {
	*    "results" : [
	*       {
	* 	 "address_components" : [
	* 	    {
	* 	       "long_name" : "New Zealand",
	* 	       "short_name" : "NZ",
	* 	       "types" : [ "country", "political" ]
	* 	    }, ...
	* 	 ],
	* 	 "formatted_address" : "59 Example Street, Christchurch 8081, New Zealand",
	*/

	if ((json = json_decode(utstring_body(geodata))) == NULL) {
		return (0);
	}

	/*
	 * Check for:
	 *
	 *  { "error_message" : "You have exceeded your daily request quota for this API. We recommend registering for a key at the Google Developers Console: https://console.developers.google.com/",
	 *     "results" : [],
	 *        "status" : "OVER_QUERY_LIMIT"
	 *  }
	 */

	// printf("%s\n", utstring_body(geodata));
	if ((j = json_find_member(json, "status")) != NULL) {
		// printf("}}}}}} %s\n", j->string_);
		if (strcmp(j->string_, "OK") != 0) {
			fprintf(stderr, "revgeo: %s (%s)\n", j->string_, utstring_body(geodata));
			return (0);
		}
	}

	if ((results = json_find_member(json, "results")) != NULL) {
		if ((zeroth = json_find_element(results, 0)) != NULL) {
			address = json_find_member(zeroth, "formatted_address");
			if ((address != NULL) && (address->tag == JSON_STRING)) {
				utstring_printf(addr, "%s", address->string_);
			}
		}

		/* Country */
		if ((ac = json_find_member(zeroth, "address_components")) != NULL) {
			JsonNode *comp, *j;
			int have_cc = 0;

			json_foreach(comp, ac) {
				JsonNode *a;

				if ((j = json_find_member(comp, "types")) != NULL) {
					json_foreach(a, j) {
						if ((a->tag == JSON_STRING) && (strcmp(a->string_, "country") == 0)) {
							JsonNode *c;

							if ((c = json_find_member(comp, "short_name")) != NULL) {
								utstring_printf(cc, "%s", c->string_);
								have_cc = 1;
								break;
							}
						}
					}
				}
				if (have_cc)
					break;
			}
		}
	}

	json_delete(json);
	return (1);
}

JsonNode *revgeo(double lat, double lon, UT_string *addr, UT_string *cc)
{
	static UT_string *url;
	static UT_string *cbuf;		/* Buffer for curl GET */
	CURLcode res;
	int rc;
	JsonNode *geo;
	time_t now;

	if ((geo = json_mkobject()) == NULL) {
		return (NULL);
	}

	if (lat == 0.0L && lon == 0.0L) {
		utstring_printf(addr, "Unknown (%lf,%lf)", lat, lon);
		utstring_printf(cc, "__");
		return (geo);
	}
	
	utstring_renew(url);
	utstring_renew(cbuf);
	utstring_printf(url, GURL, lat, lon);

	curl_easy_setopt(curl, CURLOPT_URL, utstring_body(url));
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "ot-recorder-agent/1.0");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writemem);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)cbuf);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		utstring_printf(addr, "revgeo failed for (%lf,%lf)", lat, lon);
		utstring_printf(cc, "__");
		fprintf(stderr, "curl_easy_perform() failed: %s\n",
		              curl_easy_strerror(res));
		json_delete(geo);
		return (NULL);
	}

	// printf("%s\n", utstring_body(url));

	if (!(rc = goog_decode(cbuf, addr, cc))) {
		json_delete(geo);
		return (NULL);
	}

	// fprintf(stderr, "revgeo returns %d: %s\n", rc, utstring_body(addr));

	time(&now);

	json_append_member(geo, "cc", json_mkstring(utstring_body(cc)));
	json_append_member(geo, "addr", json_mkstring(utstring_body(addr)));
	json_append_member(geo, "tst", json_mknumber((double)now));
	return (geo);
}

void revgeo_init()
{
	curl = curl_easy_init();
}

#if 0
int main()
{
	double lat = 52.034403, lon = 8.476544;
	double clat = 51.197500, clon = 6.699179;
	static UT_string *location;
	int rc;

	curl = curl_easy_init();

	utstring_renew(location);

	rc = revgeo(lat, lon, location);
	if (rc == 1) {
		puts(utstring_body(location));
	}

	utstring_renew(location);
	rc = revgeo(clat, clon, location);
	if (rc == 1) {
		puts(utstring_body(location));
	}



	curl_easy_cleanup(curl);

	return (0);

}
#endif
