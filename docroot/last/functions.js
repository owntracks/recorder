var map;
var do_fit = false;
var markers = {};
var livemarkers = true;

function initialize() {
	var lat = 50.098280;
	var lon = 10.187189;
	var params = getSearchParameters();

	var center = new google.maps.LatLng(lat,lon);

	do_fit = (params.fit) ? true : false;

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

	/* Create the DIV to hold the control and call constructor */
	var buttonControlDiv = document.createElement('div');
	var buttonControl = new ButtonControl(buttonControlDiv, map);

	buttonControlDiv.index = 1;
	map.controls[google.maps.ControlPosition.BOTTOM_LEFT].push(buttonControlDiv);
}

function ButtonControl(controlDiv, map) {

	// Set CSS for the control border.
	var controlUI = document.createElement('div');
	controlUI.style.backgroundColor = '#fff';
	controlUI.style.border = '2px solid #fff';
	controlUI.style.borderRadius = '3px';
	controlUI.style.boxShadow = '0 2px 6px rgba(0,0,0,.3)';
	controlUI.style.cursor = 'pointer';
	controlUI.style.marginBottom = '22px';
	controlUI.style.textAlign = 'center';
	controlUI.title = 'Click to toggle Autozoom (zoom to fit)';
	controlDiv.appendChild(controlUI);

	// Set CSS for the control interior.
	var controlText = document.createElement('div');
	controlText.style.color = (do_fit) ? 'rgb(0,153,0)' : 'rgb(25,25,25)';
	controlText.style.fontFamily = 'Roboto,Arial,sans-serif';
	controlText.style.fontSize = '16px';
	controlText.style.lineHeight = '38px';
	controlText.style.paddingLeft = '4px';
	controlText.style.paddingRight = '4px';
	controlText.innerHTML = 'Autozoom';
	controlUI.appendChild(controlText);

	// Setup the click event listener.
	controlUI.addEventListener('click', function() {
		do_fit = !do_fit;
		controlText.style.color = (do_fit) ? 'rgb(0,153,0)' : 'rgb(25,25,25)';
		if (do_fit) {
			fitbounds();
		}
	});
}

function clog(upd, id, s) {
	var ident = id || 'unknown';

	console.log(upd + ": " + ident + " " + s);
}

/* http://stackoverflow.com/questions/5448545/ */
function getSearchParameters() {
      var prmstr = window.location.search.substr(1);
      return prmstr != null && prmstr != "" ? transformToAssocArray(prmstr) : {};
}

function transformToAssocArray( prmstr ) {
    var params = {};
    var prmarr = prmstr.split("&");
    for ( var i = 0; i < prmarr.length; i++) {
        var tmparr = prmarr[i].split("=");
        params[tmparr[0]] = tmparr[1];
    }
    return params;
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

	if (do_fit) {
		fitbounds();
	}
}

google.maps.event.addDomListener(window, 'load', initialize);
