/*
 * OwnTracks Recorder
 * Copyright (C) 2015-2024 Jan-Piet Mens <jpmens@gmail.com>
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
#include <ctype.h>
#include <curl/curl.h>
#include "utstring.h"
#include "geo.h"
#include "json.h"
#include "util.h"

typedef enum {
	GOOGLE,
	OPENCAGE,
	REVGEOD
} geocoder;

#define GOOGLE_URL "https://maps.googleapis.com/maps/api/geocode/json?latlng=%lf,%lf&sensor=false&language=EN&key=%s"

#define OPENCAGE_URL "https://api.opencagedata.com/geocode/v1/json?q=%lf+%lf&key=%s&abbrv=1&no_record=1&limit=1&format=json"

#define REVGEOD_URL "http://%s/rev?lat=%lf&lon=%lf&app=recorder"	/* "host:port", lat, lon */

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

static int revgeod_decode(UT_string *geodata, UT_string *addr, UT_string *cc, UT_string *locality)
{
	JsonNode *json, *village, *j, *a;

	/*
	* We are parsing this data returned from revgeod(1):
	*
	* {"address":{"village":"La Terre Noire, 77510 Sablonnières, France","locality":"Sablonnières","cc":"FR","s":"lmdb"}}
	*
	*/

	if ((json = json_decode(UB(geodata))) == NULL) {
		return (0);
	}

	if ((a = json_find_member(json, "address")) != NULL) {
		if ((village = json_find_member(a, "village")) != NULL) {
			if (village->tag == JSON_STRING) {
				utstring_printf(addr, "%s", village->string_);
			}
		}
		if ((j = json_find_member(a, "locality")) != NULL) {
			if (j->tag == JSON_STRING) {
				utstring_printf(locality, "%s", j->string_);
			}
		}
		if ((j = json_find_member(a, "cc")) != NULL) {
			if (j->tag == JSON_STRING) {
				utstring_printf(cc, "%s", j->string_);
			}
		}
	}

	json_delete(json);
	return (1);
}

static int opencage_decode(UT_string *geodata, UT_string *addr, UT_string *cc, UT_string *locality, UT_string *tzname)
{
	JsonNode *json, *results, *address, *ac, *zeroth;
	JsonNode *annotations, *timezone;

	/*
	* We are parsing this. I want the formatted in `addr' and
	* the country code short_name in `cc'
	*
	* {
	*   "documentation": "https://geocoder.opencagedata.com/api",
	*   "licenses": [
	*     {
	*       "name": "CC-BY-SA",
	*       "url": "http://creativecommons.org/licenses/by-sa/3.0/"
	*     },
	*     {
	*       "name": "ODbL",
	*       "url": "http://opendatacommons.org/licenses/odbl/summary/"
	*     }
	*   ],
	*   "rate": {
	*     "limit": 2500,
	*     "remaining": 2495,
	*     "reset": 1525392000
	*   },
	*   "results": [
	*     {
	*       "annotations" : {
	*          "timezone" : {
	*             "name" : "America/Cancun",
	*             "now_in_dst" : 0,
	*             "offset_sec" : -18000,
	*             "offset_string" : "-0500",
	*             "short_name" : "EST"
	*          },
	*       ...
	*       "components": {
	*         "city": "Sablonnières",
	*         "country": "France",
	*         "country_code": "fr",
	*         "place": "La Terre Noire",
	*       },
	*       "formatted": "La Terre Noire, 77510 Sablonnières, France",
	*/

	if ((json = json_decode(UB(geodata))) == NULL) {
		return (0);
	}

	if ((results = json_find_member(json, "results")) != NULL) {
		if ((zeroth = json_find_element(results, 0)) != NULL) {
			address = json_find_member(zeroth, "formatted");
			if ((address != NULL) && (address->tag == JSON_STRING)) {
				utstring_printf(addr, "%s", address->string_);
			}
		}

		if ((annotations = json_find_member(zeroth, "annotations")) != NULL) {
			if ((timezone = json_find_member(annotations, "timezone")) != NULL) {
				JsonNode *tz;

				// puts(json_stringify(timezone, NULL));

				if ((tz = json_find_member(timezone, "name")) != NULL)
				{
					utstring_printf(tzname, "%s", tz->string_);
				}
			}
		}
		if ((ac = json_find_member(zeroth, "components")) != NULL) {

			/*
			 * {
			 *   "ISO_3166-1_alpha-2": "FR",
			 *   "_type": "place",
			 *   "city": "Sablonnières",
			 *   "country": "France",
			 *   "country_code": "fr",
			 *   "county": "Seine-et-Marne",
			 *   "place": "La Terre Noire",
			 *   "political_union": "European Union",
			 *   "postcode": "77510",
			 *   "state": "Île-de-France"
			 * }
			 */

			JsonNode *j;

			if ((j = json_find_member(ac, "country_code")) != NULL) {
				if (j->tag == JSON_STRING) {
					char *bp = j->string_;
					int ch;

					while (*bp) {
						ch = (islower(*bp)) ? toupper(*bp) : *bp;
						utstring_printf(cc, "%c", ch);
						++bp;
					}
				}
			}

			if ((j = json_find_member(ac, "city")) != NULL) {
				if (j->tag == JSON_STRING) {
					utstring_printf(locality, "%s", j->string_);
				}
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
	static UT_string *tzname = NULL;
	long http_code;
	CURLcode res;
	int rc;
	JsonNode *geo;
	time_t now;
	geocoder geocoder;

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
	utstring_renew(tzname);

	if (!ud->geokey || !*ud->geokey) {
		utstring_printf(addr, "Unknown (%lf,%lf)", lat, lon);
		utstring_printf(cc, "__");
		return (geo);
	}

	if (strncmp(ud->geokey, "opencage:", strlen("opencage:")) == 0) {
		utstring_printf(url, OPENCAGE_URL, lat, lon, ud->geokey + strlen("opencage:"));
		geocoder = OPENCAGE;
	} else if (strncmp(ud->geokey, "revgeod:", strlen("revgeod:")) == 0) {
		/* revgeod:localhost:8865 */
		utstring_printf(url, REVGEOD_URL, ud->geokey + strlen("revgeod:"), lat, lon);
		geocoder = REVGEOD;
	} else {
		utstring_printf(url, GOOGLE_URL, lat, lon, ud->geokey);
		geocoder = GOOGLE;
	}

	// fprintf(stderr, "--------------- %s\n", UB(url));

	curl_easy_setopt(curl, CURLOPT_URL, UB(url));
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "OwnTracks-Recorder/1.0");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, GEOCODE_TIMEOUT);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writemem);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)cbuf);

	res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	if (res != CURLE_OK || http_code != 200) {
		utstring_printf(addr, "revgeo failed for (%lf,%lf): HTTP status_code==%ld", lat, lon, http_code);
		utstring_printf(cc, "__");
		fprintf(stderr, "curl_easy_perform() failed: %s\n",
		              curl_easy_strerror(res));
		json_delete(geo);
		return (NULL);
	}

	switch (geocoder) {
		case GOOGLE:
			rc = goog_decode(cbuf, addr, cc, locality);
			break;
		case OPENCAGE:
			rc = opencage_decode(cbuf, addr, cc, locality, tzname);
			break;
		case REVGEOD:
			rc = revgeod_decode(cbuf, addr, cc, locality);
			break;
	}

	if (!rc) {
		json_delete(geo);
		return (NULL);
	}

	// fprintf(stderr, "revgeo returns %d: %s\n", rc, UB(addr));

	time(&now);

	json_append_member(geo, "cc", json_mkstring(UB(cc)));
	json_append_member(geo, "addr", json_mkstring(UB(addr)));
	json_append_member(geo, "tst", json_mknumber((double)now));
	if (utstring_len(locality) > 0) {
		json_append_member(geo, "locality", json_mkstring(UB(locality)));
	}
	if (utstring_len(tzname) > 0) {
		json_append_member(geo, "tzname", json_mkstring(UB(tzname)));
	}
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
