
function initialize_leaflet() {
  var map = L.map('map-canvas').setView([0.0, 0.0], 1);

  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '&copy; <a href="http://osm.org/copyright">OpenStreetMap</a> contributors'
  }).addTo(map);


  var dataURL = location.protocol + "//" + location.host;

  var parts = location.pathname.split('/');
  for (var i = 1; i < parts.length - 2; i++) {
    dataURL = dataURL + "/" + parts[i];
  }
  dataURL = dataURL + "/api/0/locations" + location.search;

  console.log("dataURL = " + dataURL);

  var geojsonMarkerOptions = {
    radius: 5,
    fillColor: "#ff0000",
    color: "#ffffff",
    weight: 2,
    opacity: 1,
    fillOpacity: 0.9
  };

  var empty_geojson = {
    type: "FeatureCollection",
    features: []
  };
  var lastLatLng = L.latLng(0.0, 0.0);

  var geojsonLayer = new L.GeoJSON(empty_geojson, {
    pointToLayer: function (feature, latlng) {
      return L.circleMarker(latlng, geojsonMarkerOptions);
    },
    onEachFeature: function(feature, layer) {
      if (feature.geometry.type == 'Point') {
        var data ={};
        data.address = feature.properties.address;
        data.lat = feature.geometry.coordinates[1].toFixed(5);
        data.lon = feature.geometry.coordinates[0].toFixed(5);
        data.vel = feature.properties.vel;
        data.tst = feature.properties.tst;
        var localtime = moment.utc(data.tst * 1000).local();
        data.timestring = localtime.format('YYYY-MM-DD, ddd, HH:mm:ss Z');

        var text = [];
        if(data.timestring) {text.push('{{ timestring }}')}
        if(data.lat && data.lon) {text.push('<span class="latlon">{{ lat }},{{ lon }}</span>')}
        if(data.address) {text.push('{{ address }}')}
        if(data.vel !== undefined) {text.push('{{ vel }} km/h')}
        layer.bindPopup(Mustache.render(text.join('<br/>'), data));
      }
    },
    style : function(feature) {
      if (feature.geometry.type == 'Point') {
        return {}
      } else {
        return {
          color : "#ff0000",
          weight : 4,
        }
      }
    },
    coordsToLatLng: function(coords) {
       var lat = coords[1];
       var lon = coords[0];
       var dist0 = Math.abs(lon - lastLatLng.lng);
       var dist1 = Math.abs(lon + 360.0 - lastLatLng.lng);
       var dist2 = Math.abs(lon - 360.0 - lastLatLng.lng);
       if (dist0 > dist1 || dist0 > dist2) {
	       if (dist0 > dist1) {
		       lon = lon + 360.0;
	       } else {
		       lon = lon - 360.0;
	       }
       }
       var latLng = L.GeoJSON.coordsToLatLng([lon, lat]);
       lastLatLng = latLng;
       return latLng
    }
  });

  map.addLayer(geojsonLayer);

  $.getJSON( dataURL, function( data ) {
    geojsonLayer.addData(data)
    map.fitBounds(geojsonLayer.getBounds());
  });

}
