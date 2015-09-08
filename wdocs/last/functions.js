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
	var id = loc.tid.replace(/\//g, '-');
	var d;

	if (loc.addr && loc.tid) {
		d = loc.tid + " " + loc.addr;
	} else {
		d = 'unknown';
	}

	loc.description = d;

	console.log(JSON.stringify(loc));

	if (markers.hasOwnProperty(id)) {
		console.log("UPDATE " + id + " marker");
		var LatLng = new google.maps.LatLng(loc.lat, loc.lon);
		markers[id].setPosition(LatLng);
		markers[id].setTitle(loc.description);
	} else {
		var circle ={
			path: google.maps.SymbolPath.CIRCLE,
			fillColor: '#ff0000',
			fillOpacity: .9,
			scale: 5.5,
			strokeColor: 'white',
			strokeWeight: 2
		};

		console.log("NEW " + id + " marker");
		var LatLng = new google.maps.LatLng(loc.lat, loc.lon);
		var m = new google.maps.Marker({
			position: LatLng,
			map: map,
                        title: loc.description,
			// icon: "marker.php?tid=" + id,
			// icon: "red-marker.png"
			icon: circle
		});

		markers[id] = m;
		console.log("MARKER is " + id + "= " + id);
                info(map, m, loc);
	}
}

function info(map, marker, data) {
	var infowindow = new google.maps.InfoWindow();

	google.maps.event.addListener(marker, "click", function(e) {
		infowindow.setContent(data.description);
		infowindow.open(map, marker);
	});
}

google.maps.event.addDomListener(window, 'load', initialize);
