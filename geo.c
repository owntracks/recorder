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
#include <time.h>
#include <curl/curl.h>
#include "utstring.h"
#include "geo.h"
#include "json.h"
#include "util.h"

#define GURL "%s://maps.googleapis.com/maps/api/geocode/json?latlng=%lf,%lf&sensor=false&language=EN"

static CURL *curl;

static size_t writemem(void *contents, size_t size, size_t nmemb, void *userp)
{
	UT_string *cbuf = (UT_string *)userp;
	size_t realsize = size * nmemb;

	utstring_bincpy(cbuf, contents, realsize);

	return (realsize);
}

static int goog_decode(UT_string *geodata, UT_string *addr, UT_string *cc, UT_string *locality)
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

	if ((json = json_decode(UB(geodata))) == NULL) {
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

	// printf("%s\n", UB(geodata));
	if ((j = json_find_member(json, "status")) != NULL) {
		// printf("}}}}}} %s\n", j->string_);
		if (strcmp(j->string_, "OK") != 0) {
			fprintf(stderr, "revgeo: %s (%s)\n", j->string_, UB(geodata));
			json_delete(json);
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
			int have_cc = 0, have_locality = 0;

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
						} else if ((a->tag == JSON_STRING) && (strcmp(a->string_, "locality") == 0)) {
							JsonNode *l;

							if ((l = json_find_member(comp, "long_name")) != NULL) {
								utstring_printf(locality, "%s", l->string_);
								have_locality = 1;
								break;
							}
						}
					}
				}
				if (have_cc && have_locality)
					break;
			}
		}
	}

	json_delete(json);
	return (1);
}

JsonNode *revgeo(struct udata *ud, double lat, double lon, UT_string *addr, UT_string *cc)
{
	static UT_string *url;
	static UT_string *cbuf;		/* Buffer for curl GET */
	static UT_string *locality = NULL;
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
	utstring_renew(locality);

	if (ud && ud->geokey) {
		utstring_printf(url, GURL, "https", lat, lon);
		utstring_printf(url, "&key=%s", ud->geokey);
	} else {
		utstring_printf(url, GURL, "http", lat, lon);
	}

	curl_easy_setopt(curl, CURLOPT_URL, UB(url));
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "OwnTracks-Recorder/1.0");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);

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

	// printf("%s\n", UB(url));

	if (!(rc = goog_decode(cbuf, addr, cc, locality))) {
		json_delete(geo);
		return (NULL);
	}

	// fprintf(stderr, "revgeo returns %d: %s\n", rc, UB(addr));

	time(&now);

	json_append_member(geo, "cc", json_mkstring(UB(cc)));
	json_append_member(geo, "addr", json_mkstring(UB(addr)));
	json_append_member(geo, "tst", json_mknumber((double)now));
	json_append_member(geo, "locality", (utstring_len(locality) > 0) ?
		json_mkstring(UB(locality)) : json_mknull());
	return (geo);
}

void revgeo_init()
{
	curl = curl_easy_init();
}

void revgeo_free()
{
	curl_easy_cleanup(curl);
	curl = NULL;
}

#if 0
int main()
{
	double lat = 52.25458, lon = 5.1494;
	double clat = 51.197500, clon = 6.699179;
	UT_string *location = NULL, *cc = NULL;
	JsonNode *json;
	char *js;

	curl = curl_easy_init();

	utstring_renew(location);
	utstring_renew(cc);

	if ((json = revgeo(NULL, lat, lon, location, cc)) != NULL) {
		js = json_stringify(json, " ");
		printf("%s\n", js);
		free(js);
	} else {
		printf("Cannot get revgeo\n");
	}

	if ((json = revgeo(NULL, clat, clon, location, cc)) != NULL) {
		js = json_stringify(json, " ");
		printf("%s\n", js);
		free(js);
	} else {
		printf("Cannot get revgeo\n");
	}

	curl_easy_cleanup(curl);

	return (0);

}
#endif
