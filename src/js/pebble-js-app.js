var URL = "ws://192.168.183.223:3000/watch";

function start() {
	var ws = new WebSocket(URL, "watch");

	ws.onmessage = function(ev) {
		Pebble.sendAppMessage(JSON.parse(ev.data));
	}
	ws.onerror = ws.onclose = function() {
		Pebble.sendAppMessage({ SlyConnected: 0});
		ws.onclose = ws.onerror = null;
		setTimeout(start, 1000);
	};

	Pebble.addEventListener('appmessage', function(e) {
		console.log(JSON.stringify(e.payload));
		ws.send(JSON.stringify(e.payload));
	});
}

Pebble.addEventListener("ready", function(e) {
	start();
});
