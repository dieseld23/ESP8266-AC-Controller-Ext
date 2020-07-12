/* Copyright 2019 Motea Marius

  This example code will create a webserver that will provide basic control to
  AC units using the web application build with javascript/css. User config zone
  need to be updated if a different class than Collix need to be used.
  Javasctipt file may also require minor changes as in current version it will
  not allow to set fan speed if Auto mode is selected (required for Midea).

*/

// Modification to code - Dan Maslach 7/7/2020 */
// Additions mostly from https://github.com/mdhiggins/ESP8266-HTTP-IR-Blaster */

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>  // Useful to access to ESP by hostname.local
#include <FS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <Ticker.h>	 // For LED status
#include <WebSocketsServer.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <WiFiUdp.h>
//// ###### User configuration space for AC library classes ##########
#include <ir_Midea.h>  //  replace library based on your AC unit model, check https://github.com/crankyoldgit/IRremoteESP8266
// 0xA202FFFFFF7E  - energy saver
// 0xA201FFFFFF7C  - ionizer

#define AUTO_MODE kMideaACAuto
#define COOL_MODE kMideaACCool
#define DRY_MODE kMideaACDry
#define HEAT_MODE kMideaACHeat
#define FAN_MODE kMideaACFan
#define FAN_AUTO kMideaACFanAuto
#define FAN_MIN kMideaACFanLow
#define FAN_MED kMideaACFanMed
#define FAN_HI kMideaACFanHigh
#define ENERGY_SAVER kMideaACEnergySaver
#define IONIZER kMideaACToggleSwingV

/// ##### Start user configuration ######
const uint16_t kIrLed = 15;	 // ESP8266 GPIO pin to use for IR blaster.
const int configpin = 10;
int port = 80;
const int ledpin = LED_BUILTIN;
// const int led2pin = 16;
const int led3pin = 14;
/// ##### End user configuration ######

const bool enableMDNSServices = true;
const size_t bufferSize = JSON_OBJECT_SIZE(22) + 300;  // adjust to size of data coming from tstat
char wifi_config_name[] = "ESP Setup";				   // Default
char host_name[20] = "garageac";					   // Default
char tstatIP[20] = "garagetstat";					   // Default Venstar Tstat IP Address
unsigned long previousMillis = 0;
unsigned long currentMillis = 0;
bool shouldSaveConfig = false;	// Flag for saving data
bool setLED = false;
struct state {
	uint8_t temperature = 85;
	uint8_t fan = 0;
	uint8_t operation = 0;
	bool powerStatus;
	bool extControl = true;
};
struct tstatData {
	const char *name = "";
	int mode = 0;
	int currentState = 0;
	int activestage = 0;
	int fan = 0;
	int fanstate = 0;
	int tempunits = 0;
	int schedule = 0;
	int schedulepart = 0;
	int away = 0;
	int spacetemp = 79;
	int heattemp = 78;
	int cooltemp = 75;
	int cooltempmin = 35;
	int cooltempmax = 99;
	int heattempmin = 35;
	int heattempmax = 99;
	int setpointdelta = 2;
	int availablemodes = 0;
};
IRMideaAC ac(kIrLed);  // Library initialization, change it according to the imported library file.
tstatData tstat;
File fsUploadFile;
state acState;
Ticker led1tick;
Ticker led3tick;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdateServer;
WebSocketsServer webSocket(81);

//+=============================================================================
// convert the file extension to the MIME type
//
String getContentType(String filename) {
	if (filename.endsWith(".html"))
		return "text/html";
	else if (filename.endsWith(".css"))
		return "text/css";
	else if (filename.endsWith(".js"))
		return "application/javascript";
	else if (filename.endsWith(".ico"))
		return "image/x-icon";
	else if (filename.endsWith(".gz"))
		return "application/x-gzip";
	return "text/plain";
}

//+=============================================================================
//  send the right file to the client (if it exists)
//
bool handleFileRead(String path) {
	//  Serial.println("handleFileRead: " + path);
	if (path.endsWith("/"))
		path += "index.html";
	// If a folder is requested, send the index file
	String contentType = getContentType(path);
	// Get the MIME type
	String pathWithGz = path + ".gz";
	if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
		// If the file exists, either as a compressed archive, or normal
		if (SPIFFS.exists(pathWithGz))	// If there's a compressed version available
			path += ".gz";				// Use the compressed verion
		File file = SPIFFS.open(path, "r");
		//  Open the file
		server.streamFile(file, contentType);
		//  Send it to the client
		file.close();
		// Close the file again
		// Serial.println(String("\tSent file: ") + path);
		return true;
	}
	// Serial.println(String("\tFile Not Found: ") + path);
	// If the file doesn't exist, return false
	return false;
}

//+=============================================================================
// Upload a new file to the SPIFFS
//
void handleFileUpload() {
	HTTPUpload &upload = server.upload();
	if (upload.status == UPLOAD_FILE_START) {
		String filename = upload.filename;
		if (!filename.startsWith("/")) {
			filename = "/" + filename;
		}
		// Serial.print("handleFileUpload Name: "); //Serial.println(filename);
		fsUploadFile = SPIFFS.open(filename, "w");
		// Open the file for writing in SPIFFS (create if it doesn't exist)
		filename = String();
	} else if (upload.status == UPLOAD_FILE_WRITE) {
		if (fsUploadFile) {
			fsUploadFile.write(upload.buf, upload.currentSize);
			// Write the received bytes to the file
		}
	} else if (upload.status == UPLOAD_FILE_END) {
		if (fsUploadFile) {
			// If the file was successfully created
			fsUploadFile.close();
			// Close the file again
			// Serial.print("handleFileUpload Size: ");
			// Serial.println(upload.totalSize);
			server.sendHeader("Location", "/success.html");
			// Redirect the client to the success page
			server.send(303);
		} else {
			server.send(500, "text/plain", "500: couldn't create file");
		}
	}
}

//+=============================================================================
// Upload a new file to the SPIFFS - File not found
//
void handleNotFound() {
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += server.uri();
	message += "\nMethod: ";
	message += (server.method() == HTTP_GET) ? "GET" : "POST";
	message += "\nArguments: ";
	message += server.args();
	message += "\n";
	for (uint8_t i = 0; i < server.args(); i++) {
		message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
	}
	server.send(404, "text/plain", message);
}

//+=============================================================================
// Toggle state
//
void led1Ticker() {
	int state = digitalRead(ledpin);  // get the current state of GPIO1 pin
	digitalWrite(ledpin, !state);	  // set pin to the opposite state
}

//+=============================================================================
// Turn off the Led after timeout
//
void led1TickerDisable() {
	Serial.println("Turning off the LED to save power.");
	digitalWrite(ledpin, HIGH);	 // Shut down the LED
	led1tick.detach();			 // Stopping the ticker
}

//+=============================================================================
// Toggle state
//
void led3Ticker() {
	if (digitalRead(led3pin) == HIGH) {
		led3TickerDisable();
	}
	digitalWrite(led3pin, HIGH);  // set pin to the opposite state
	Serial.println("ledon");
}

//+=============================================================================
// Turn off the Led after timeout
//
void led3TickerDisable() {
	digitalWrite(led3pin, LOW);	 // Shut down the LED
	led3tick.detach();			 // Stopping the ticker
}

//+=============================================================================
// Callback notifying us of the need to save config
//
void saveConfigCallback() {
	Serial.println("Should save config");
	shouldSaveConfig = true;
}

//+=============================================================================
// Gets called when WiFiManager enters configuration mode
//
void configModeCallback(WiFiManager *myWiFiManager) {
	Serial.println("Entered config mode");
	Serial.println(WiFi.softAPIP());
	// if you used auto generated SSID, print it
	Serial.println(myWiFiManager->getConfigPortalSSID());
	// entered config mode, make led toggle faster
	led1tick.attach(0.2, led1Ticker);
}

//+=============================================================================
// Gets called when device loses connection to the accesspoint
//
void lostWifiCallback(const WiFiEventStationModeDisconnected &evt) {
	Serial.println("Lost Wifi");
	// reset and try again, or maybe put it to deep sleep
	ESP.reset();
	delay(1000);
}

//+=============================================================================
// Gets info from Thermostat
//
void getVenstarStatus() {
	WiFiClient client;
	String url = "http://" + String(tstatIP) + "/query/info";
	HTTPClient http;
	http.setReuse(true);
	http.begin(client, url);
	int httpCode = http.GET();
	// Serial.println(httpCode);
	if (httpCode > 0) {
		DynamicJsonDocument doc(bufferSize);
		DeserializationError error = deserializeJson(doc, http.getString());
		if (error) {
			Serial.println("There was an error while deserializing");
			http.end();	 // Close connection
		} else {
			// JsonObject doc = jsonDocument.as<JsonObject>();
			tstat.name = doc["name"];					   // "GarageAC1234567890"
			tstat.mode = doc["mode"];					   // 0
			tstat.currentState = doc["state"];			   // 0
			tstat.activestage = doc["activestage"];		   // 0
			tstat.fan = doc["fan"];						   // 0
			tstat.fanstate = doc["fanstate"];			   // 0
			tstat.tempunits = doc["tempunits"];			   // 0
			tstat.schedule = doc["schedule"];			   // 0
			tstat.schedulepart = doc["schedulepart"];	   // 0
			tstat.away = doc["away"];					   // 0
			tstat.spacetemp = doc["spacetemp"];			   // 79
			tstat.heattemp = doc["heattemp"];			   // 78
			tstat.cooltemp = doc["cooltemp"];			   // 75
			tstat.cooltempmin = doc["cooltempmin"];		   // 35
			tstat.cooltempmax = doc["cooltempmax"];		   // 99
			tstat.heattempmin = doc["heattempmin"];		   // 35
			tstat.heattempmax = doc["heattempmax"];		   // 99
			tstat.setpointdelta = doc["setpointdelta"];	   // 2
			tstat.availablemodes = doc["availablemodes"];  // 0
		}
		// Serial.println(tstat.currentState);
		http.end();	 // Close connection
	}
}

//+=============================================================================
// Enable MDNS Function
//
void enableMDNS() {
	// Configure OTA Update
	ArduinoOTA.setPort(8266);
	ArduinoOTA.setHostname(host_name);
	ArduinoOTA.onStart([]() { Serial.println("Start"); });
	ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR)
			Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR)
			Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR)
			Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR)
			Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR)
			Serial.println("End Failed");
	});
	ArduinoOTA.begin();
	Serial.println("ArduinoOTA started");

	// Configure mDNS
	MDNS.addService("http", "tcp", port);	// Announce the ESP as an HTTP service
	Serial.println("MDNS http service added. Hostname is set to " + String(host_name) + ".local:" + String(port));
}

//+=============================================================================
// Setup HTTP server
//
void serverSetup() {
	server.on(
		"/file-upload", HTTP_POST, []() {				   // if the client posts to the upload page
			server.send(200);  // Send status 200 (OK) to tell the client we are ready to receive
		},
		handleFileUpload);	// Receive and save the file

	server.on("/file-upload", HTTP_GET, []() {				// if the client requests the upload page
		String html = "<form method=\"post\" enctype=\"multipart/form-data\">";
		html += "<input type=\"file\" name=\"name\">";
		html += "<input class=\"button\" type=\"submit\" value=\"Upload\">";
		html += "</form>";
		server.send(200, "text/html", html);
	});

	server.on("/", []() {
		server.sendHeader("Location", String("ui.html"), true);
		server.send(302, "text/plain", "");
	});

	server.on("/reset", []() {
		server.send(200, "text/html", "reset");
		delay(100);
		ESP.restart();
	});

	server.serveStatic("/", SPIFFS, "/", "max-age=86400");

	server.onNotFound(handleNotFound);
}

//+=============================================================================
// Setup/Start WebSocket
//
void startWebSocket() {					// Start a WebSocket server
	webSocket.begin();					// start the websocket server
	webSocket.onEvent(webSocketEvent);	// if there's an incomming websocket message, go to function 'webSocketEvent'
	Serial.println("WebSocket server started.");
}

//+=============================================================================
// WebSocket Event
//
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t lenght) {	// When a WebSocket message is received
	switch (type) {
		case WStype_DISCONNECTED:  // if the websocket is disconnected
			Serial.printf("[%u] Disconnected!\n", num);
			break;
		case WStype_CONNECTED: {  // if a new websocket connection is established
			IPAddress ip = webSocket.remoteIP(num);
			Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
			// rainbow = false;  // Turn rainbow off when a new connection is established
		} break;
		case WStype_TEXT:  // if new text data is received
			Serial.printf("[%u] get Text: %s\n", num, payload);

			DynamicJsonDocument root(1024);
			DeserializationError error = deserializeJson(root, payload);
			if (error) {
				Serial.println("Deserialization Error");
			} else {
				if (root.containsKey("temp")) {
					acState.temperature = (uint8_t)root["temp"];
				}
				if (root.containsKey("fan")) {
					acState.fan = (uint8_t)root["fan"];
				}
				if (root.containsKey("power")) {
					acState.powerStatus = root["power"];
				}
				if (root.containsKey("mode")) {
					acState.operation = root["mode"];
				}
				if (root.containsKey("extControl")) {
					acState.extControl = root["extControl"];
				}
				// String output;
				// serializeJson(root, output);
				// server.send(200, "text/plain", output);

				delay(200);
				controlAC();
			}
			break;
	}
}

void controlAC() {
	if (acState.powerStatus && acState.extControl == false) {
		ac.on();
		ac.setTemp(acState.temperature);
		if (acState.operation == 0) {
			ac.setMode(AUTO_MODE);
			ac.setFan(FAN_AUTO);
			acState.fan = 0;
		} else if (acState.operation == 1) {
			ac.setMode(COOL_MODE);
		} else if (acState.operation == 2) {
			ac.setMode(DRY_MODE);
		} else if (acState.operation == 3) {
			ac.setMode(HEAT_MODE);
		} else if (acState.operation == 4) {
			ac.setMode(FAN_MODE);
		}

		if (acState.operation != 0) {
			if (acState.fan == 0) {
				ac.setFan(FAN_AUTO);
			} else if (acState.fan == 1) {
				ac.setFan(FAN_MIN);
			} else if (acState.fan == 2) {
				ac.setFan(FAN_MED);
			} else if (acState.fan == 3) {
				ac.setFan(FAN_HI);
			}
		}
	} else if (acState.extControl == true) {
		if (tstat.currentState == 2) {
			acState.operation = 1;	// COOL_MODE
			acState.temperature = 65;
			acState.fan = 0;  // FAN_AUTO

			DynamicJsonDocument root(1024);
			root["mode"] = acState.operation;
			root["fan"] = acState.fan;
			root["temp"] = acState.temperature;
			root["power"] = acState.powerStatus;
			String output;
			serializeJson(root, output);
			webSocket.sendTXT(1, output);  // first value is client id, I just put in 1 to fill it
			ac.on();
			ac.setTemp(acState.temperature);
			ac.setFan(acState.fan);
			ac.send();	// economode is seperate message so send it as a seperate command
			delay(200);
			ac.setEconoToggle(true);  // turn off Econo mode

		} else {
			ac.off();
		}
	} else {
		ac.off();
	}
	setLED = true;
	ac.send();
}

void dataSave() {
	if (SPIFFS.begin()) {
		Serial.println("Mounted file system");	
		if (SPIFFS.exists("/data.json")) {					// file exists, reading and loading
			Serial.println("Reading data file");
			File configFile = SPIFFS.open("/data.json", "r");
			if (configFile) {
				Serial.println("Opened data file");
				size_t size = configFile.size();
				std::unique_ptr<char[]> buf(new char[size]);					// Allocate a buffer to store contents of the file.
				configFile.readBytes(buf.get(), size);
				DynamicJsonDocument root(1024);
				DeserializationError error = deserializeJson(root, buf.get());
				if (!error) {
					Serial.println("\nParsed json");

					if (root.containsKey("temp")) {
						acState.temperature = (uint8_t)root["temp"];
					}
					if (root.containsKey("fan")) {
						acState.fan = (uint8_t)root["fan"];
					}
					if (root.containsKey("power")) {
						acState.powerStatus = root["power"];
					}
					if (root.containsKey("mode")) {
						acState.operation = root["mode"];
					}
					if (root.containsKey("extControl")) {
						acState.extControl = root["extControl"];
					}

				} else {
					Serial.println("Failed to load data json config");
				}
			}
		}

		else {
			Serial.println(" Initialize Data...");
			DynamicJsonDocument json(1024);
			json["temp"] = 86;
			json["fan"] = 0;
			json["power"] = false;
			json["mode"] = 0;
			json["extControl"] = true;
			writeDataFile(json);
		}
	} else {
		Serial.println("Failed to mount FS");
	}
}

void writeDataFile(DynamicJsonDocument json) {
	File dataFile = SPIFFS.open("/data.json", "w");
	if (!dataFile) {
		Serial.println("Failed to open config file for writing");
	}

	serializeJson(json, Serial);
	Serial.println("");
	Serial.println("Writing data file");
	serializeJson(json, dataFile);
	dataFile.close();
	json.clear();
	Serial.println("Data written successfully");
}
//+=============================================================================
// Setup Wifi
//
bool setupWifi(bool resetConf) {
	led1tick.attach(0.5, led1Ticker);			// start ticker with 0.5 because we start in AP mode and try to connect
	// WiFiManager Local intialization. Once its business is done, there is no need to keep it around
	WiFiManager wifiManager;
	if (resetConf)				// reset settings - for testing
		wifiManager.resetSettings();
	
	wifiManager.setAPCallback(configModeCallback);		// set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
	wifiManager.setSaveConfigCallback(saveConfigCallback);	// set config save notify callback
	wifiManager.setConfigPortalTimeout(180);		// Reset device if on config portal for greater than 3 minutes

	if (SPIFFS.begin()) {
		Serial.println("Mounted file system");
		if (SPIFFS.exists("/config.json")) {						// file exists, reading and loading

			Serial.println("Reading config file");
			File configFile = SPIFFS.open("/config.json", "r");
			if (configFile) {
				Serial.println("Opened config file");
				size_t size = configFile.size();						// Allocate a buffer to store contents of the file.
				std::unique_ptr<char[]> buf(new char[size]);		
				configFile.readBytes(buf.get(), size);
				DynamicJsonDocument json(1024);
				DeserializationError error = deserializeJson(json, buf.get());
				serializeJson(json, Serial);
				if (!error) {
					Serial.println("\nParsed json");
					if (json.containsKey("hostname")) {
						strncpy(host_name, json["hostname"], 20);
					}
					if (json.containsKey("tstatIP")) {
						strncpy(tstatIP, json["tstatIP"], 20);
					}
				} else {
					Serial.println("Failed to load json config");
				}
			}
		}
	} else {
		Serial.println("Failed to mount FS");
	}

	WiFiManagerParameter custom_hostname("hostname", "Choose a hostname to this IR Controller", host_name, 20);
	WiFiManagerParameter custom_tstatIP("tstatIP", "Venstar Thermostat IP", tstatIP, 20);

	wifiManager.addParameter(&custom_hostname);
	wifiManager.addParameter(&custom_tstatIP);
	// fetches ssid and pass and tries to connect, if it does not connect it starts an access point with the specified name and goes into a blocking loop awaiting configuration
	if (!wifiManager.autoConnect(wifi_config_name)) {
		Serial.println("Failed to connect and hit timeout");			// reset and try again, or maybe put it to deep sleep
		delay(1000);
		ESP.reset();
		delay(2000);
	}

	// if you get here you have connected to the WiFi
	strncpy(host_name, custom_hostname.getValue(), 20);
	strncpy(tstatIP, custom_tstatIP.getValue(), 20);
	WiFi.onStationModeDisconnected(&lostWifiCallback);		// Reset device if lost wifi Connection
	Serial.println("WiFi connected! User chose hostname '" + String(host_name) + "'");
	if (shouldSaveConfig) {			// save the custom parameters to FS
		Serial.println(" Config...");
		DynamicJsonDocument json(100);
		json["hostname"] = host_name;
		json["tstatIP"] = tstatIP;
		File configFile = SPIFFS.open("/config.json", "w");
		if (!configFile) {
			Serial.println("Failed to open config file for writing");
		}
		serializeJson(json, Serial);
		Serial.println("");
		Serial.println("Writing config file");
		serializeJson(json, configFile);
		configFile.close();
		json.clear();
		Serial.println("Config written successfully");
	}
	led1tick.detach();
	digitalWrite(ledpin, LOW); 		// keep LED on
	return true;
}

//+=============================================================================
// MAIN SETUP
//
void setup() {
	pinMode(ledpin, OUTPUT);
	// pinMode(led2pin, OUTPUT);
	pinMode(led3pin, OUTPUT);
	Serial.begin(115200);
	ac.begin();
	delay(1000);

	Serial.println("");
	Serial.println("ESP8266 IR Controller");
	pinMode(configpin, INPUT_PULLUP);
	Serial.print("Config pin GPIO");
	Serial.print(configpin);
	Serial.print(" set to: ");
	Serial.println(digitalRead(configpin));
	if (!setupWifi(digitalRead(configpin) == LOW))
		return;

	Serial.println("WiFi configuration complete");

	if (strlen(host_name) > 0) {
		WiFi.hostname(host_name);
	} else {
		WiFi.hostname().toCharArray(host_name, 20);
	}

	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	wifi_set_sleep_type(LIGHT_SLEEP_T);
	digitalWrite(ledpin, LOW);

	led1tick.attach(2, led1TickerDisable);			// Turn off the led in 2s

	Serial.print("Local IP: ");
	Serial.println(WiFi.localIP().toString());
	Serial.println("URL to send commands: http://" + String(host_name) + ":" + port);
	Serial.println("Tstat IP set to: http://" + String(tstatIP) + ":" + port);

	if (enableMDNSServices) {
		enableMDNS();
	}

	httpUpdateServer.setup(&server);
	serverSetup();
	startWebSocket();
	server.begin();
}

//+=============================================================================
//
void loop() {
	webSocket.loop();  // constantly check for websocket events
	ArduinoOTA.handle();
	server.handleClient();
	MDNS.update();
	currentMillis = millis();
	if (acState.extControl == true) {
		if (currentMillis - previousMillis > 2000) {
			previousMillis = currentMillis;
			getVenstarStatus();
		}
	}

	if (setLED == true) {
		led3tick.attach(3000, led3Ticker);
	}
}