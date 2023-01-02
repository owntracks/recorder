#include <stdio.h>
#include <stdlib.h>
#include "json.h"
#include "udata.h"
#include "fences.h"
#include "gcache.h"
#include "util.h"
#ifdef WITH_LUA
# include "hooks.h"
#endif

/*
 * OwnTracks Recorder
 * Copyright (C) 2015-2023 Jan-Piet Mens <jpmens@gmail.com>
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

/*
 * lat/lon are the position that has been reported by the device as it is at
 * now, and wp contains the point read from the list of waypoints. Calculate
 * distance betwee the two, and if that's less than waypoint radius, user is
 * now IN the geofence, else OUT of the geofence. Indicate transition by
 * changing IO in wp and telling caller to store.
 */

static int check_a_waypoint(char *key, wpoint *wp, double lat, double lon)
{
	double dist;
	bool rewrite = false;

	dist = haversine_dist(wp->lat, wp->lon, lat, lon);
	// printf("WP= rad=%ld, io=%d, dist=%lf %s\n", wp->rad, wp->io, dist, wp->desc);
	
	if (dist < wp->rad) {
		// printf("YEAH: key(%s)\n", key);
		
		if (wp->io == false) {
			wp->event = ENTER;
			wp->io = true;
			rewrite = true;
		}
	} else if (wp->io == true) {
		wp->event = LEAVE;
		wp->io = false;
		rewrite = true;
	}

	if (rewrite) {
		// printf("%s - %s: EVENT == %s\n", wp->user, wp->device, wp->event == 0 ? "ENTER" : "LEAVE");
#ifdef WITH_LUA
		if (wp->ud->luadata) {
			hooks_transition(wp->ud, wp->user, wp->device, wp->event, wp->desc, wp->lat, wp->lon, lat, lon, wp->topic, wp->json, (long)dist);
		}
#endif /* WITH_LUA */
	}

	return (rewrite);
}

/*
 * Every time a position is obtained, calculate the distance to the center
 * of each geofence and check whether that distance is less than the radius
 * of the geofence. If the distance is <= to the radius, the obtained position
 * is considered to be inside the geofence. This position is calculated using
 * the Haversine formula.
 */

void check_fences(struct udata *ud, char *username, char *device, double lat, double lon, JsonNode *json, char *topic)
{
	static UT_string *userdev;

	utstring_renew(userdev);
	utstring_printf(userdev, "%s-%s", username, device);

	/*
	 * For each of this user's geofences (username-device-*) obtain lat/lon/rad
	 * and do as described above.
	 */

	gcache_enum(username, device, ud->wpdb, UB(userdev), check_a_waypoint, lat, lon, ud, topic, json);
}
