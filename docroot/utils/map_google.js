
/**
 * @file
 * Contains common functions used specifically with Google Maps.
 */

import { debug } from "./debug.js";

/*
 * The default style for a marker. 
 */
export const markerStyle = {
  path: 0, /* google.maps.SymbolPath.CIRCLE */
  fillColor: 'red',
  fillOpacity: 0.9,
  scale: 5.5,
  strokeColor: 'white',
  strokeWeight: 2,
};

/*
 * The default style for a track.
 */
export const strokeStyle = {
  strokeColor: 'red',
  strokeWeight: 4,
};

/*
 * Closes all the infowindows in the 'markers' object, and then opens the
 * infowindow in the specified marker.
 */
export function openSingularMarker(map, markers, markerToOpen) {
  for (const marker of Object.values(markers)) {
    marker.infowindow.close();
  }
  markerToOpen.infowindow.open(map, markerToOpen);
}

/*
 * Fits the viewport bounds to show all map markers.
 */
export function fitBounds(map, markers) {
  const bounds = new google.maps.LatLngBounds();
  for (const marker of Object.values(markers)) {
    bounds.extend(marker.getPosition());
  }
  map.setCenter(bounds.getCenter());	/* Center to geometric center of all markers */
  map.fitBounds(bounds);
}

/*
 * Logs a map update.
 */
export function clog(upd, id, s) {
  debug(`${ upd } ${ Math.round(new Date().getTime() / 1000) }: ${ id || 'unknown' }`, s);
}
