var out = function(message) {
	var div = document.createElement('div');
	div.innerHTML = message;
	document.getElementById('output').appendChild(div);
};

window.onload = function() {
	var url = 'ws://' + location.host + '/ws/last';

	console.log(JSON.stringify(location));


	websocket = new WebSocket(url);
	websocket.onopen = function(ev) {
		// out('CONNECTED');
		var msg = 'LAST';
		// out('SENT: ' + msg);
		websocket.send(msg);
	};

	websocket.onclose = function(ev) {
		out('DISCONNECTED');
	};

	websocket.onmessage = function(ev) {
		if (!ev.data) {
			// out('<span style="color: blue;">PING... </span>');
		} else {
			// out('<span style="color: blue;">RESPONSE: ' + ev.data + ' </span>');
			try {
				var loc = JSON.parse(ev.data);
				map_marker(loc);
			} catch (x) {
				;
			}
		}
	};

	websocket.onerror = function(ev) {
		out('<span style="color: red; ">ERROR: </span> ' + ev.data);
	};
};
