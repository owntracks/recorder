
import { ws_connect } from "./websock.js";

import { debug } from "../utils/debug.js";
import { generatePopupHTML } from "../utils/map.js";
import { openSingularMarker, fitBounds, markerStyle, clog } from "../utils/map_google.js";
import { getCosmeticLocation, getCosmeticName } from "../utils/misc.js";

let map;
let do_fit = false;
const markers = {};
const livemarkers = true;

export function initialize(ws_url) {

  const params = new URL(window.location).searchParams;
  do_fit = Boolean(params.get("fit"));

  const mapOptions = {
    center: new google.maps.LatLng(50.098280, 10.187189),
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
  const buttonControlDiv = document.createElement('div');
  const buttonControl = new ButtonControl(buttonControlDiv);

  buttonControlDiv.index = 1;
  map.controls[google.maps.ControlPosition.BOTTOM_LEFT].push(buttonControlDiv);

  ws_connect(ws_url, map_marker); 	// Connect to websocket and start listening
}

function ButtonControl(controlDiv) {

  // Set CSS for the control border.
  const controlUI = document.createElement('div');
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
  const controlText = document.createElement('div');
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
      fitBounds(map, markers);
    }
  });
}

/*
 * `loc' is a location object obtained via Websockets from the recorder
 */
function map_marker(loc) {
  debug("Format marker:", loc);

  const id = loc.topic.replaceAll("/", '-');

  const title = `${ getCosmeticName(loc) } ${ getCosmeticLocation(loc) }`;
  const htmldesc = generatePopupHTML(loc);

  const LatLng = new google.maps.LatLng(loc.lat, loc.lon);
  if (markers.hasOwnProperty(id)) {
    clog("UPD", id, loc);
    const m = markers[id];
    
    m.setPosition(LatLng);
    m.setTitle(title);

    /* Grab the InfoWindow of this marker and change content */
    m.infowindow.setContent(htmldesc);
    if (livemarkers) {
      openSingularMarker(map, markers, m);
    }
  } else {
    clog("NEW", id, loc);

    const m = new google.maps.Marker({
      position: LatLng,
      map,
      title: title,
      // icon: "marker.php?tid=" + id,
      // icon: "red-marker.png"
      icon: markerStyle,
    });

    /* Create a new InfoWindow for this marker, and add listener */
    m.infowindow = new google.maps.InfoWindow({
      content: htmldesc,
    });
    google.maps.event.addListener(m, "click", function() {
      this.infowindow.open(map, this);
    });
    if (livemarkers) {
      openSingularMarker(map, markers, m);
    }

    markers[id] = m;
  }

  if (do_fit) {
    fitBounds(map, markers);
  }
}

// google.maps.event.addDomListener(window, 'load', initialize);
