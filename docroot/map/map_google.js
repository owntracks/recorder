// __author__    = 'Jan-Piet Mens <jpmens()gmail.com>'
// __copyright__ = 'Copyright 2015 Jan-Piet Mens'

// https://developers.google.com/maps/documentation/javascript/datalayer

var infowindow;

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

function initialize_googlemaps() {

	var map;
	var center = new google.maps.LatLng( 46.993665, 10.399188);

	infowindow = new google.maps.InfoWindow();

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

	var dataURL = location.protocol + "//" + location.host;

	var parts = location.pathname.split('/');
	for (var i = 1; i < parts.length - 2; i++) {
		dataURL = dataURL + "/" + parts[i];
	}
	
	dataURL = "http://kantaki:8083"
	
	dataURL = dataURL + "/api/0/locations" + location.search;

	console.log("dataURL = " + dataURL);

	map.data.loadGeoJson(dataURL);

        var circle ={
                  path: google.maps.SymbolPath.CIRCLE,
                  fillColor: '#ff0000',
                  fillOpacity: .9,
                  scale: 5.5,
                  strokeColor: 'white',
                  strokeWeight: 2
        };
        var featureStyle = {
                strokeColor: 'red',
                strokeWeight: 4,
		icon: circle
        };
        map.data.setStyle(featureStyle);

	// Zoom to show all the features
	var bounds = new google.maps.LatLngBounds();
	map.data.addListener('addfeature', function (e) {
		processPoints(e.feature.getGeometry(), bounds.extend, bounds);
		map.fitBounds(bounds);
	});

	// click dot on map to show info
	map.data.addListener('click', function(event) {
		var tst = event.feature.getProperty('tst');
		var dt = moment.utc(tst * 1000).local();
		var data = {
			lat:   event.latLng.lat().toFixed(4),
			lon:   event.latLng.lng().toFixed(4),
			tst:   tst,
			addr:  event.feature.getProperty('address'),
			fulldate: dt.format("DD MMM YYYY HH:mm:ss"),
		};

		var t = "{{ addr }}<br/><span class='latlon'>({{ lat }},{{lon}})</span> {{ fulldate }}";
		if (typeof(tst) === 'undefined') {
			t = "Position: {{lat}}, {{lon}}";
		}

		infowindow.setContent(Mustache.render(t, data));
		infowindow.setPosition(event.latLng);
		infowindow.open(map);
	});
}
