
function initialize_leaflet() {
  var markerLayer;
  var markers={};
  var map = L.map('map-canvas').setView([0.0, 0.0], 1);

  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '&copy; <a href="http://osm.org/copyright">OpenStreetMap</a> contributors'
  }).addTo(map);

  function osm_map_marker(loc)
  {
    console.log("Updating marker");
    var id = loc.topic.replace(/\//g, '-');

    if (!markers.hasOwnProperty(id)) {
      markers[id] = L.marker([loc.lat, loc.lon]);
      markerLayer.addLayer(markers[id]);
      if (loc.face) {
        markers[id]._face=loc.face;
      }
    } 

    markers[id].setLatLng({lat: loc.lat, lng: loc.lon});
    loc.face=markers[id]._face;
    var description = formatPopup(loc);
    
    markers[id].bindPopup(description.htmldesc).openPopup()
    map.fitBounds(markerLayer.getBounds());

  }

  
 
  markerLayer = new L.FeatureGroup();
  map.addLayer(markerLayer);

  ws_go(osm_map_marker); 	// Connect to websocket and start listening

}

function formatPopup(loc) {
  var htmldesc="";
  var shortdesc="";
  var s = loc.topic.split('/');
  var username = (s[0]) ? s[1] : s[2];	/* cater for leading / in topic */
  var device = (s[0]) ? s[2] : s[3];	/* cater for leading / in topic */

  var dt = moment.utc(loc.tst * 1000).local();

  var userdev = username + "/" + device;
  userdev = renames[userdev] ? renames[userdev] : userdev;
  userdev = loc.name ? loc.name : userdev;

  var data = {
    userdev:  userdev,
    ghash:    loc.ghash ? loc.ghash : 'unknown',
    addr:     loc.addr,
    lat:      loc.lat,
    lon:      loc.lon,
    fulldate: dt.format("DD MMM YYYY HH:mm:ss"),
    facedata: loc.face,
    vel:      loc.vel,
    cog:      loc.cog,
  };

  
  if (loc.face) {
    htmldesc = htmldesc + "<div style='float:right;' id='avatar'><img class='img-circle' src='data:image/png;base64,{{ facedata }}' height='35' width='35' /></div>";
  }

  if (loc.addr) {
    htmldesc = htmldesc + "<b>{{userdev}}</b><br/>{{addr}}<br/><span class='extrainfo'>{{ghash}} <span class='latlon'>({{lat}},{{lon}}) v={{vel}}, c={{cog}}</span> {{fulldate}}</span>";
    shortdesc = "{{{userdev}}} {{addr}}";
  } else {
    htmldesc = htmldesc + "<b>{{userdev}}</b><br/>{{lat}}, {{lon}}<br/><span class='extrainfo'>{{ghash}} <span class='latlon'>({{lat}},{{lon}}) v={{vel}}, c={{cog}}</span> {{fulldate}}</span>";
    shortdesc = shortdesc + "{{{userdev}}} {{lat}},{{lon}}";
  }


  var result = {}
  result.short = Mustache.render(shortdesc, data);
  result.htmldesc = Mustache.render(htmldesc, data);
  return result;
}
