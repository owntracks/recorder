var reconnectTimeout = 3000;
var ws_url;
var ws;

function setColor(element, color) {
	element.style.backgroundColor = color;
}

var out = function(message) {
	var div = document.createElement('div');
	div.innerHTML = message;
	document.getElementById('output').appendChild(div);
};

function ws_connect() {

	console.log("Connecting to websocket at " + ws_url);

	ws = new WebSocket(ws_url);

	ws.onopen = function(ev) {
		setColor(mapover, 'green');
		document.getElementById('mapover').textContent = 'Connected';
		var msg = 'LAST';
		// out('SENT: ' + msg);
		ws.send(msg);
	};

	ws.onclose = function(ev) {
		setColor(mapover, 'red');
		document.getElementById('mapover').textContent = 'Disconnected';
		setTimeout(ws_connect, reconnectTimeout);
	};

	ws.onmessage = function(ev) {
		if (!ev.data) {
			// out('<span style="color: blue;">PING... </span>');
		} else {
			// out('<span style="color: blue;">RESPONSE: ' + ev.data + ' </span>');
			try {
				var loc = JSON.parse(ev.data);

				if (loc['_label']) {
					document.getElementById('maplabel').textContent = loc['_label'];
				}

				if (loc['_type'] == 'location') {
					map_marker(loc);
				}
			} catch (x) {
				;
			}
		}
	};

	ws.onerror = function(ev) {
		out('<span style="color: red; ">ERROR: </span> ' + ev.data);
	};
}

window.onload = function() {
	// var url = 'ws://' + location.host + '/ws/last';
	// console.log(JSON.stringify(location));

	var url = ("https:" == document.location.protocol ? "wss://" : "ws://") + location.host + "/";
	var parts = location.pathname.split('/');
	for (var i = 1; i < parts.length - 2; i++) {
		url = url + parts[i] + "/";
	}
	url = url + "ws/last";
	console.log("Websocket URI: " + url);

	ws_url = url;
	ws_connect();

};
