var URL = "ws://192.168.178.81:3000/watch"
Pebble.addEventListener("ready", function(e) {
	var ws = new WebSocket(URL, "watch");
	ws.onmessage = function(ev) {
		console.log("<" + ev.data);
		Pebble.sendAppMessage(JSON.parse(ev.data));
	}

	Pebble.addEventListener('appmessage', function(e) {
		console.log(JSON.stringify(e.payload));
		ws.send(JSON.stringify(e.payload));
	});
});
