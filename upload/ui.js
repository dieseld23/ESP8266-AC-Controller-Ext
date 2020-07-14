


const options = {
    url: 'ws://' + location.hostname + ':81/',
    //pingTimeout: 9000, 
    //pongTimeout: 7000, 
    //reconnectTimeout: 3000,
   // pingMsg: "heartbeat"
}
var websocketHeartbeatJs = new WebsocketHeartbeatJs(options);

websocketHeartbeatJs.onopen = function () {
	showPage();
	console.log("Websocket Connected" + ' to ws://' + location.hostname + ':81/', ['arduino']);

}
websocketHeartbeatJs.onmessage = function (e) {
	console.log(`onmessage: ${e.data}`);
	var msg = JSON.parse(e.data);
	updateStatus(msg);
}
websocketHeartbeatJs.onreconnect = function () {
    console.log('Reconnecting...');
}

function loadPage () {
}
/*
var connection = new WebSocket('ws://' + location.hostname + ':81/', ['arduino']);
connection.onopen = function () {
	showPage();
	console.log("Websocket Connected" + ' to ws://' + location.hostname + ':81/', ['arduino']);
	connection.send('Connect ' + new Date());
};
connection.onerror = function (error) {
	console.log('WebSocket Error ', error);
};
connection.onmessage = function (event) {
	//console.log('Server: ', event.data);
	var msg = JSON.parse(event.data);
	//console.log(msg);
	heartbeat();
	updateStatus(msg);
};
connection.onclose = function (event) {
	if (event.wasClean) {
		console.log(`[close] Connection closed cleanly, code=${event.code} reason=${event.reason}`);
	} else {
		// e.g. server process killed or network down
		// event.code is usually 1006 in WebsocketHeartbeatJs case
		console.log('[close] Connection died');
	}
}; 

function heartbeat() {

}
*/
function showPage() {
	document.getElementById("loader").style.display = "none";
	document.getElementById("main").style.display = "block";
  }

function updateStatus(data) {
	state = data;
	if (state["power"] === true) {
		$("#power").text(" ON");
		$("#power-btn").addClass("btn-info");
		$("#power-btn").removeClass("btn-default");
	} else {
		$("#power").text(" OFF");
		$("#power-btn").addClass("btn-default");
		$("#power-btn").removeClass("btn-info");
	}

	if (state["extControl"] === true) {
		$("#extControl").text(" ON");
		$("#extControl-btn").addClass("btn-info");
		$("#extControl-btn").removeClass("btn-default");
		$(".disableBtn").addClass("disable-Btn");

	} else {
		$("#extControl").text(" OFF");
		$("#extControl-btn").addClass("btn-default");
		$("#extControl-btn").removeClass("btn-info");
		$(".disableBtn").removeClass("disable-Btn");
	}

	$("#target_temp").text(state["temp"] + " F");
	setModeColor(state["mode"]);
	setFanColor(state["fan"]);
}

function postData(t) {
	updateStatus(t);
	console.log(t);
	websocketHeartbeatJs.send(JSON.stringify(t));
}

function mode_onclick(mode) {
	state["mode"] = mode;
	setModeColor(mode);
	postData(state);
}

function setModeColor(mode) {
	$(".mode-btn").addClass("btn-default");
	$(".mode-btn").removeClass("btn-info");

	if (mode === 0) {
		$("#mode_auto").removeClass("btn-default");
		$("#mode_auto").addClass("btn-info");
		setFanColor(0);
		state["fan"] = 0;
	} else if (mode === 1) {
		$("#mode_cooling").removeClass("btn-default");
		$("#mode_cooling").addClass("btn-info");
	} else if (mode === 2) {
		$("#mode_dehum").removeClass("btn-default");
		$("#mode_dehum").addClass("btn-info");
	} else if (mode === 3) {
		$("#mode_heating").removeClass("btn-default");
		$("#mode_heating").addClass("btn-info");
	} else if (mode === 4) {
		$("#mode_fan").removeClass("btn-default");
		$("#mode_fan").addClass("btn-info");
	}
}

function setFanColor(fan) {
	if (fan == 0) {
		$("#fan_auto").removeClass("btn-default");
		$("#fan_auto").addClass("btn-info");
	} else {
		$("#fan_auto").removeClass("btn-info");
		$("#fan_auto").addClass("btn-default");
	}
	for (var i = 1; i <= 3; ++i) {
		if (i <= fan) {
			$("#fan_lvl_" + i).attr("src", "level_" + i + "_on.svg");
		} else {
			$("#fan_lvl_" + i).attr("src", "level_" + i + "_off.svg");
		}
	}
}

function fan_onclick(fan) {
	if (state["mode"] !== 0) {
		state["fan"] = fan;
		setFanColor(fan);
		postData(state);
	}
}

function power_onclick(power) {
	if (state["power"]) {
		state["power"] = false;
		$("#power").text(" OFF");
		$("#power-btn").removeClass("btn-info");
		$("#power-btn").addClass("btn-default");
	} else {
		state["power"] = true;
		$("#power").text(" ON");
		$("#power-btn").addClass("btn-info");
		$("#power-btn").removeClass("btn-default");
	}
	postData(state);
}

function extControl_onclick(extControl) {
	if (state["extControl"]) {
		state["extControl"] = false;
		$("#extControl").text(" OFF");
		$("#extControl-btn").removeClass("btn-info");
		$("#extControl-btn").addClass("btn-default");
		$(".disableBtn").removeClass("disable-Btn");
	} else {
		state["extControl"] = true;
		$("#extControl").text(" ON");
		$("#extControl-btn").addClass("btn-info");
		$("#extControl-btn").removeClass("btn-default");
		$(".disableBtn").addClass("disable-Btn");
	}
	postData(state);
}

function temp_onclick(temp) {
	state["temp"] += temp;
	if (state["temp"] < 30) {
		state["temp"] = 30;
	}
	if (state["temp"] > 115) {
		state["temp"] = 115;
	}
	$("#target_temp").text(state["temp"] + " F");
	postData(state);
}
