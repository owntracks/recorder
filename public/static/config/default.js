(() => {
  const endDate = new Date();
  endDate.setUTCHours(0);
  endDate.setUTCMinutes(0);
  endDate.setUTCSeconds(0);
  const startDate = new Date(endDate);
  startDate.setUTCMonth(startDate.getMonth()-1);
  window.defaultConfig = {
    accentColor: '#3388ff',
    startDate,
    endDate,
    map: {
      center: L.latLng(0, 0),
      zoom: 19,
      maxNativeZoom: 19,
      maxZoom: 21,
      url: 'https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',
      attribution: '&copy; <a href="https://osm.org/copyright">OpenStreetMap</a> contributors',
      heatmap: {
        max: 20,
        radius: 25,
        blur: 15,
        gradient: null,  // https://github.com/mourner/simpleheat/blob/gh-pages/simpleheat.js#L22
      },
    },
  };
})();
