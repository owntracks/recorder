
// __author__    = 'Jan-Piet Mens <jpmens()gmail.com>'
// __copyright__ = 'Copyright 2015 Jan-Piet Mens'
// __license__   = """Eclipse Public License - v 1.0 (http://www.eclipse.org/legal/epl-v10.html)"""

// https://developers.google.com/maps/documentation/javascript/datalayer

var infowindow = new google.maps.InfoWindow();

function processPoints(geometry, callback, thisArg) {
	if (geometry instanceof google.maps.LatLng) {
		callback.call(thisArg, geometry);
	} else if (geometry instanceof google.maps.Data.Point) {
		callback.call(thisArg, geometry.get());
	} else {
		geometry.getArray().forEach(function(g) {
			processPoints(g, callback, thisArg);
		});
	}
}

function initialize() {

	var map;
	var center = new google.maps.LatLng( 46.993665, 10.399188);

	mapOptions = {
		center: center,
		zoom: 5,
		mapTypeId: google.maps.MapTypeId.ROADMAP,
		scrollwheel: false,
		disableDefaultUI: false,
		panControl: false,
		scaleControl: false,
		streetViewControl: true,
		overviewMapControl: true,
	};

	map = new google.maps.Map(document.getElementById("map-canvas"), mapOptions);

	// alert(JSON.stringify(location));

	var dataURI = location.protocol + "//" + location.host;

	var parts = location.pathname.split('/');
	for (var i = 1; i < parts.length - 2; i++) {
		dataURI = dataURI + "/" + parts[i];
	}
	dataURI = dataURI + "/api/0/locations" + location.search;

	console.log("dataURI = " + dataURI);
	map.data.loadGeoJson(dataURI);

        // Set the stroke width, and fill color for each polygon
        var featureStyle = {
                fillColor: 'green',
                strokeColor: 'red',
                strokeWeight: 4,
                title: "OwnTracks",
        };
        map.data.setStyle(featureStyle);


	// Zoom to show all the features
	var bounds = new google.maps.LatLngBounds();
	map.data.addListener('addfeature', function (e) {
		processPoints(e.feature.getGeometry(), bounds.extend, bounds);
		map.fitBounds(bounds);
	});

   /*
	// Zoom to the clicked feature
	map.data.addListener('click', function (e) {
		var bounds = new google.maps.LatLngBounds();
	        processPoints(e.feature.getGeometry(), bounds.extend, bounds);
		map.fitBounds(bounds);
	});
    */

	google.maps.event.addListener(map, 'click', function() {
		infowindow.close();
	});
	map.data.addListener('click', function(e) {
		var content = "<h1>" + e.feature.getProperty('name') + "</h1>" + e.feature.getProperty('address');

		infowindow.setContent(content);
		infowindow.setPosition(e.latLng);
		infowindow.setOptions({
			pixelOffset: new google.maps.Size(0, -34),
			});
		infowindow.open(map);
	});

	
	map.data.setStyle(function(feature) {
		var circle ={
			path: google.maps.SymbolPath.CIRCLE,
			fillColor: '#ff0000',
			fillOpacity: .9,
			scale: 5.5,
			strokeColor: 'white',
			strokeWeight: 2
		};

		return ({
			icon: circle
			});
	});

}


google.maps.event.addDomListener(window, 'load', initialize);
