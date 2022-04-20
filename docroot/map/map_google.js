// __author__    = 'Jan-Piet Mens <jpmens()gmail.com>'
// __copyright__ = 'Copyright 2015 Jan-Piet Mens'

// https://developers.google.com/maps/documentation/javascript/datalayer

import { debug } from "../utils/debug.js";

import { generatePopupHTML } from "../utils/map.js";
import { markerStyle, strokeStyle } from "../utils/map_google.js";

let infowindow;

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

export function initialize(dataUrl) {
  let map;
  const center = new google.maps.LatLng( 46.993665, 10.399188);

  infowindow = new google.maps.InfoWindow();

  const mapOptions = {
    center,
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

  debug("Loading data from URL:", dataUrl);
  map.data.loadGeoJson(dataUrl);

  const featureStyle = {
    ...strokeStyle,
    icon: markerStyle,
  };
  map.data.setStyle(featureStyle);

  // Zoom to show all the features
  const bounds = new google.maps.LatLngBounds();
  map.data.addListener('addfeature', function (e) {
    processPoints(e.feature.getGeometry(), bounds.extend, bounds);
    map.fitBounds(bounds);
  });

  // click dot on map to show info
  map.data.addListener('click', function(event) {
    const description = generatePopupHTML({
      lat:  event.latLng.lat().toFixed(4),
      lon:  event.latLng.lng().toFixed(4),
      addr: event.feature.getProperty("address"),
      tst:  event.feature.getProperty("tst"),
      vel:  event.feature.getProperty("vel"),
      acc:  event.feature.getProperty("acc"),
    });

    infowindow.setContent(description);
    infowindow.setPosition(event.latLng);
    infowindow.open(map);
  });
}
