(() => {
  Vue.component('modal', {
    template: `
      <div class="modal" v-show="visible" @click.self="$emit('close')">
        <div class="modal-container">
          <button class="modal-close-button" title="Close" @click="$emit('close')">
            &times;
          </button>
          <slot></slot>
        </div>
      </div>
    `,
    props: {
      visible: {
        type: Boolean,
        default: false,
      },
    },
  });
})();
