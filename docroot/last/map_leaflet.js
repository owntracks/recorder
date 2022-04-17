
import { ws_connect } from "./websock.js";

import { debug } from "../utils/debug.js";
import { generatePopupHTML } from "../utils/map.js";
import { markerStyle } from "../utils/map_leaflet.js";

export function initialize(ws_url) {

  let markerLayer;
  const markers = {};
  const map = L.map('map-canvas').setView([0.0, 0.0], 1);

  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
  }).addTo(map);

  function osm_map_marker(loc) {
    debug("Updating marker");
    const id = loc.topic.replaceAll("/", '-');

    if (!markers.hasOwnProperty(id)) {
      markers[id] = L.circleMarker([loc.lat, loc.lon], markerStyle);
      markerLayer.addLayer(markers[id]);
      if (loc.face) {
        markers[id]._face = loc.face;
      }
    } 

    markers[id].setLatLng({lat: loc.lat, lng: loc.lon});
    loc.face = markers[id]._face;
    const description = generatePopupHTML(loc);
    
    markers[id].bindPopup(description).openPopup();
    map.fitBounds(markerLayer.getBounds());

  }

 
  markerLayer = new L.FeatureGroup();
  map.addLayer(markerLayer);

  ws_connect(ws_url, osm_map_marker); // Connect to websocket and start listening

}
