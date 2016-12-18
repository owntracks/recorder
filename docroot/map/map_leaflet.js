
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
  dataURL = "http://kantaki:8083"
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

  var geojsonLayer = new L.GeoJSON( empty_geojson , {
    pointToLayer: function (feature, latlng) {
      return L.circleMarker(latlng, geojsonMarkerOptions);
    },
    onEachFeature: function(feature, layer) {
      if (feature.geometry.type == "Point") {
        var data ={}
        data.lat = feature.geometry.coordinates[0].toFixed(4);
        data.lon = feature.geometry.coordinates[1].toFixed(4);
        data.addr = feature.properties.address;
        var tst = feature.properties.tst;
        var dt = moment.utc(tst * 1000).local();
        data.tst = tst;
        data.fulldate = dt.format("DD MMM YYYY HH:mm:ss")
        var t = "{{ addr }}<br/><span class='latlon'>({{ lat }},{{lon}})</span> {{ fulldate }}";
        if (typeof(tst) === 'undefined') {
            t = "Position: {{lat}}, {{lon}}";
        }

        layer.bindPopup(Mustache.render(t, data));
      }
    },
    style : function(feature) {
      if (feature.geometry.type == "Point") {
        return {}
      } else {
        return {
          color : "#ff0000",
          weight : 4,
        }
      }
    }
  });
  
  map.addLayer(geojsonLayer);

  $.getJSON( dataURL, function( data ) {
    geojsonLayer.addData(data)
    map.fitBounds(geojsonLayer.getBounds());
  });

}
