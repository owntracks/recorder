
import { debug } from "../utils/debug.js";

const reconnectTimeout = 3000;

function setColor(element, color) {
  element.style.border = `2px solid ${color}`;
}

/*
function out(message) {
  const div = document.createElement('div');
  div.innerHTML = message;
  document.getElementById('output').appendChild(div);
};
*/

export function ws_connect(ws_url, callback) {

  debug("Connecting to websocket at:", ws_url);

  const ws = new WebSocket(ws_url);

  ws.onopen = function() {
    setColor(maplabel, 'green');
    const msg = 'LAST';
    // out('SENT: ' + msg);
    debug("SENT:", msg);
    ws.send(msg);
  };

  ws.onclose = function() {
    setColor(maplabel, 'red');
    setTimeout(ws_connect(ws_url, callback), reconnectTimeout, ws_url, callback);
  };

  ws.onmessage = function(event) {
    if (!event.data) {
      // out('<span style="color: blue;">PING... </span>');
      debug("PING... ");
    } else {
      // out('<span style="color: blue;">RESPONSE: ' + ev.data + ' </span>');
      try {
        const loc = JSON.parse(event.data);
        debug("RESPONSE:", loc);

        if (loc._label) {
          document.getElementById('maplabel').textContent = loc._label;
        }

        if (loc._type === 'location') {
          callback(loc);
        }
      } catch (error) {
        debug("Could not parse:", event.data, error);
      }
    }
  };

  ws.onerror = function(event) {
    // out('<span style="color: red; ">ERROR: </span> ' + ev.data);
    console.error("ERROR:", event.data);
  };
}
