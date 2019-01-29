(() => {
  const { LMap, LTileLayer, LMarker, LCircleMarker, LCircle, LPolyline } = Vue2Leaflet;
  const config = deepmerge(window.defaultConfig, window.config);
  new Vue({
    el: '#app',
    components: { vuejsDatepicker, LMap, LTileLayer, LMarker, LCircleMarker, LPolyline, LCircle },
    data: {
      users: [],
      devices: {},
      lastLocations: [],
      locationHistory: {},
      showLastLocations: true,
      showLocationHistoryPoints: false,
      showLocationHistoryLine: false,
      showLocationHeatmap: false,
      selectedUser: '',
      selectedDevice: '',
      startDate: config.startDate,
      endDate: config.endDate,
      showDownloadModal: false,
      showInformationModal: false,
      map: {
        center: config.map.center,
        zoom: config.map.zoom,
        maxNativeZoom: config.map.maxNativeZoom,
        maxZoom: config.map.maxZoom,
        url: config.map.url,
        attribution: config.map.attribution,
        polyline: {
          color: config.accentColor,
          fillColor: 'transparent',
        },
        circle: {
          color: config.accentColor,
          fillColor: config.accentColor,
          fillOpacity: 0.2,
        },
        circleMarker: {
          radius: 4,
          color: config.accentColor,
          fillColor: '#fff',
          fillOpacity: 1,
        },
        heatmap: {
          max: config.map.heatmap.max,
          radius: config.map.heatmap.radius,
          blur: config.map.heatmap.radius,
          gradient: config.map.heatmap.gradient,
        },
      },
      information: {
        version: '',
        documentationUrl: 'https://owntracks.org/booklet/',
        twitterUrl: 'https://twitter.com/OwnTracks',
        sourceCodeUrl: 'https://github.com/owntracks/recorder',
      }
    },
    watch: {
      selectedUser: async function () {
        this.selectedDevice = '';
        this.lastLocations = await this.getLastLocations();
        this.locationHistory = await this.getLocationHistory();
      },
      selectedDevice: async function () {
        this.lastLocations = await this.getLastLocations();
        this.locationHistory = await this.getLocationHistory();
      },
      startDate: async function () {
        this.locationHistory = await this.getLocationHistory();
      },
      endDate: async function () {
        this.locationHistory = await this.getLocationHistory();
      },
    },
    computed: {
      locationHistoryLatLngs() {
        const latLngs = [];
        Object.keys(this.locationHistory).forEach((user) => {
          Object.keys(this.locationHistory[user]).forEach((device) => {
            this.locationHistory[user][device].forEach((l) => {
              latLngs.push(L.latLng(l.lat, l.lon));
            });
          });
        });
        return latLngs;
      },
      startDateDisabledDates() {
        return {
          customPredictor: (date) => (date > this.endDate) || (date > new Date())
        };
      },
      endDateDisabledDates() {
        return {
          customPredictor: (date) => (date < this.startDate) || (date > new Date())
        };
      },
    },
    methods: {
      init: async function () {
        const root = document.documentElement;
        root.style.setProperty('--color-accent', config.accentColor);
        this.users = await this.getUsers();
        this.devices = await this.getDevices();
        this.lastLocations = await this.getLastLocations();
        this.locationHistory = await this.getLocationHistory();
        this.centerView();
        await this.connectWebsocket();
        this.information.version = await this.getVersion();
      },
      connectWebsocket: async function () {
        const wsUrl = `${document.location.protocol.replace('http', 'ws')}//${document.location.host}/ws/last`;
        const ws = new WebSocket(wsUrl);
        console.log(`[WS] Connecting to ${wsUrl}...`);
        ws.onopen = (e) => {
          console.log('[WS] Connected');
          ws.send('LAST');
        };
        ws.onclose = () => {
          console.log('[WS] Disconnected. Reconnecting in one second...')
          setTimeout(this.connectWebsocket, 1000);
        };
        ws.onmessage = async (msg) => {
          if (msg.data) {
            try {
              const data = JSON.parse(msg.data);
              if (data._type === 'location') {
                console.log('[WS] Location update received');
                this.lastLocations = await this.getLastLocations();
                this.locationHistory = await this.getLocationHistory();
              }
            } catch (err) {}
          } else {
            console.log('[WS] Ping');
          }
        };
      },
      getVersion: async function () {
        const response = await fetch('/api/0/version');
        const json = await response.json();
        const version = json.version;
        return version;
      },
      getUsers: async function () {
        const response = await fetch('/api/0/list');
        const json = await response.json();
        const users = json.results;
        return users;
      },
      getDevices: async function () {
        const devices = {};
        await Promise.all(this.users.map(async (user) => {
          const response = await fetch(`/api/0/list?user=${user}`);
          const json = await response.json();
          const userDevices = json.results;
          devices[user] = userDevices;
        }));
        return devices;
      },
      getLastLocations: async function () {
        let url = '/api/0/last';
        if (this.selectedUser !== '') {
          url += `?&user=${this.selectedUser}`;
          if (this.selectedDevice !== '') {
            url += `&device=${this.selectedDevice}`;
          }
        }
        const response = await fetch(url);
        const json = await response.json();
        return json;
      },
      getLocationHistory: async function () {
        let users;
        let devices;
        if (this.selectedUser === '') {
          users = this.users;
          devices = { ...this.devices };
        } else {
          users = [this.selectedUser];
          if (this.selectedDevice === '') {
            devices = { [this.selectedUser]: this.devices[this.selectedUser] };
          } else {
            devices = { [this.selectedUser]: [this.selectedDevice] };
          }
        }
        const locations = {};
        await Promise.all(users.map(async (user) => {
          locations[user] = {};
          await Promise.all(devices[user].map(async (device) => {
            const startDateString = `${this.startDate.toISOString().split('T')[0]}T00:00:00`;
            const endDateString = `${this.endDate.toISOString().split('T')[0]}T23:59:59`;
            const url = `/api/0/locations?from=${startDateString}&to=${endDateString}&format=json&user=${user}&device=${device}`;
            const response = await fetch(url);
            const json = await response.json();
            const userDeviceLocations = json.data;
            locations[user][device] = userDeviceLocations;
          }));
        }));
        return locations;
      },
      centerView() {
        if ((this.showLocationHistoryPoints || this.showLocationHistoryLine || this.showLocationHeatmap) && this.locationHistoryLatLngs.length > 0) {
          this.$refs.map.mapObject.fitBounds(new L.LatLngBounds(this.locationHistoryLatLngs));
        } else if (this.showLastLocations && this.lastLocations.length > 0) {
          const locations = this.lastLocations.map((l) => L.latLng(l.lat, l.lon));
          this.$refs.map.mapObject.fitBounds(new L.LatLngBounds(locations), {maxZoom: this.map.maxNativeZoom});
        }
      },
    },
    mounted() {
      this.init();
    },
  });
})();
