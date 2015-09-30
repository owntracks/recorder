var map;
var markers = {};
var livemarkers = true;

function initialize() {
	var lat = 50.098280;
	var lon = 10.187189;

	var center = new google.maps.LatLng(lat,lon);

	mapOptions = {
		center: center,
		zoom: 3, // 9,
                maxZoom: 18, // don't overzoom (should this be configurable?)
		mapTypeId: google.maps.MapTypeId.ROADMAP,
		scrollwheel: false,
		disableDefaultUI: false,
		panControl: false,
		scaleControl: false,
		streetViewControl: true,
		overviewMapControl: true,
	};

	map = new google.maps.Map(document.getElementById("map-canvas"), mapOptions);
}

function clog(upd, id, s) {
	var ident = id || 'unknown';

	console.log(upd + ": " + ident + " " + s);
}

/*
 * Close all the infowindows in the `markers' object, and then open the
 * infowindow in the specified marker.
 */

function refreshmarkers(m) {
	for (var k in markers) {
		markers[k]['infowindow'].close();
	}
	m['infowindow'].open(map, m);
}

function fitbounds() {
	var bounds = new google.maps.LatLngBounds();
	for (var k in markers) {
		bounds.extend(markers[k].getPosition());
	}
	map.setCenter(bounds.getCenter());	/* Center to geometric center of all markers */
	map.fitBounds(bounds);
}

/*
 * `loc' is a location object obtained via Websockets from the recorder
 */

function map_marker(loc)
{
	var id = loc.topic.replace(/\//g, '-');
	var htmldesc;
	var shortdesc;
	var s = loc.topic.split('/');
	var username = (s[0]) ? s[1] : s[2];	/* cater for leading / in topic */
	var device = (s[0]) ? s[2] : s[3];	/* cater for leading / in topic */
	var userdev = username + "/" + device;

	if (loc.addr) {
		htmldesc = "<b>" + userdev + "</b><br />" + loc.addr;
		shortdesc = userdev + " " + loc.addr;
	} else {
		htmldesc = "<b>" + userdev + "</b><br />" + loc.lat + ", " + loc.lon;
		shortdesc = userdev + " " + loc.lat + ", " + loc.lon;
	}

	loc.description = shortdesc;
	loc.htmldesc = htmldesc;

	if (markers.hasOwnProperty(id)) {
		clog("UPD", id, JSON.stringify(loc));
		m = markers[id];
		var LatLng = new google.maps.LatLng(loc.lat, loc.lon);
		m.setPosition(LatLng);
		m.setTitle(loc.description);

		/* Grab the InfoWindow of this marker and change content */
		m['infowindow'].setContent(loc.htmldesc);
		if (livemarkers) {
			refreshmarkers(m);
		}
	} else {
		clog("NEW", id, JSON.stringify(loc));
		var circle ={
			path: google.maps.SymbolPath.CIRCLE,
			fillColor: '#ff0000',
			fillOpacity: .9,
			scale: 5.5,
			strokeColor: 'white',
			strokeWeight: 2
		};

		var LatLng = new google.maps.LatLng(loc.lat, loc.lon);
		var m = new google.maps.Marker({
			position: LatLng,
			map: map,
                        title: loc.description,
			// icon: "marker.php?tid=" + id,
			// icon: "red-marker.png"
			icon: circle
		});

		/* Create a new InfoWindow for this marker, and add listener */
		m['infowindow'] = new google.maps.InfoWindow({
					content: loc.htmldesc
				  });
		google.maps.event.addListener(m, "click", function(e) {
			this['infowindow'].open(map, this);
		});
		if (livemarkers) {
			refreshmarkers(m);
		}

		markers[id] = m;
	}

	fitbounds();
}

google.maps.event.addDomListener(window, 'load', initialize);
