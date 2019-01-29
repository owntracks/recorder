(() => {
  const capitalizeFirstLetter = (string) => {
    return string.charAt(0).toUpperCase() + string.slice(1);
  }

  const propsBinder = (vueElement, leafletElement, props) => {
    for (const key in props) {
      const setMethodName = 'set' + capitalizeFirstLetter(key);
      const deepValue = (props[key].type === Object) ||
        (props[key].type === Array) ||
        (Array.isArray(props[key].type));
      if (props[key].custom && vueElement[setMethodName]) {
        vueElement.$watch(key, (newVal, oldVal) => {
          vueElement[setMethodName](newVal, oldVal);
        }, {
            deep: deepValue
        });
      } else if (setMethodName === 'setOptions') {
        vueElement.$watch(key, (newVal, oldVal) => {
          L.setOptions(leafletElement, newVal);
        }, {
            deep: deepValue
        });
      } else if (leafletElement[setMethodName]) {
        vueElement.$watch(key, (newVal, oldVal) => {
          leafletElement[setMethodName](newVal);
        }, {
            deep: deepValue
        });
      }
    }
  };

  const { findRealParent, L } = Vue2Leaflet;
  const props = {
    latLng: {
      type: Array,
      custom: false,
      default: () => []
    },
    minOpacity: {
      type: Number,
      custom: true,
      default: 0.05
    },
    maxZoom: {
      type: Number,
      custom: true,
      default: 18
    },
    radius: {
      type: Number,
      custom: true,
      default: 25
    },
    blur: {
      type: Number,
      custom: true,
      default: 15
    },
    max: {
      type: Number,
      custom: true,
      default: 1.0
    },
    gradient: {
      type: Object,
      custom: true,
      default: () => ({
        0.4: 'blue',
        0.6: 'cyan',
        0.7: 'lime',
        0.8: 'yellow',
        1.0: 'red'
      })
    },
    visible: {
      type: Boolean,
      custom: true,
      default: true
    }
  };

  Vue.component('l-heatmap', {
    props,
    template: '<div></div>',
    mounted() {
      const options = {};
      if (this.minOpacity) {
        options.minOpacity = this.minOpacity;
      }
      if (this.maxZoom) {
        options.maxZoom = this.maxZoom;
      }
      if (this.radius) {
        options.radius = this.radius;
      }
      if (this.blur) {
        options.blur = this.blur;
      }
      if (this.max) {
        options.max = this.max;
      }
      if (this.gradient) {
        options.gradient = this.gradient;
      }
      this.mapObject = L.heatLayer(this.latLng, options);
      L.DomEvent.on(this.mapObject, this.$listeners);
      propsBinder(this, this.mapObject, props);

      this.$watch('latLng', (newVal, _) => {
        this.mapObject.setLatLngs(newVal);
      }, { deep: true });
      this.parentContainer = findRealParent(this.$parent);
      this.parentContainer.addLayer(this, !this.visible);
    },
    beforeDestroy() {
      this.parentContainer.removeLayer(this);
    },
    methods: {
      setMinOpacity(newVal) {
        this.mapObject.setOptions({ minOpacity: newVal });
      },
      setMaxZoom(newVal) {
        this.mapObject.setOptions({ maxZoom: newVal });
      },
      setRadius(newVal) {
        this.mapObject.setOptions({ radius: newVal });
      },
      setBlur(newVal) {
        this.mapObject.setOptions({ blur: newVal });
      },
      setMax(newVal) {
        this.mapObject.setOptions({ max: newVal });
      },
      setGradient(newVal) {
        this.mapObject.setOptions({ gradient: newVal });
      },
      setVisible(newVal, oldVal) {
        if (newVal === oldVal) return;
        if (newVal) {
          this.parentContainer.addLayer(this);
        } else {
          this.parentContainer.removeLayer(this);
        }
      },
      addLatLng(value) {
        this.mapObject.addLatLng(value);
      }
    }
  });
})();
