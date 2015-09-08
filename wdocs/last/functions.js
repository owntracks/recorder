var map;
var markers = {};

function initialize() {
	var lat = 50.098280;
	var lon = 10.187189;

	var center = new google.maps.LatLng(lat,lon);

	mapOptions = {
		center: center,
		zoom: 3, // 9,
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

/*
 * `loc' is a location object obtained via Websockets from the recorder
 */

function map_marker(loc)
{
	var id = loc.topic.replace(/\//g, '-');
	var d;

	if (loc.addr && loc.topic) {
		d = loc.topic + " " + loc.addr;
	} else {
		d = 'unknown';
	}

	loc.description = d;

	console.log(JSON.stringify(loc));

	if (markers.hasOwnProperty(id)) {
		console.log("UPDATE " + id + " marker");
		m = markers[id];
		var LatLng = new google.maps.LatLng(loc.lat, loc.lon);
		m.setPosition(LatLng);
		m.setTitle(loc.description);

		/* Grab the InfoWindow of this marker and change content */
		m['infowindow'].setContent(loc.description);
	} else {
		console.log("NEW " + id + " marker");
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
					content: loc.description
				  });
		google.maps.event.addListener(m, "click", function(e) {
			this['infowindow'].open(map, this);
		});

		markers[id] = m;
	}
}

google.maps.event.addDomListener(window, 'load', initialize);
