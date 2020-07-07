/* Copyright 2019 Motea Marius

  This example code will create a webserver that will provide basic control to
  AC units using the web application build with javascript/css. User config zone
  need to be updated if a different class than Collix need to be used.
  Javasctipt file may also require minor changes as in current version it will
  not allow to set fan speed if Auto mode is selected (required for Midea).

*/

/* Modification to code - Dan Maslach 7/7/2020 */


#include <FS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ESP8266mDNS.h>  // Useful to access to ESP by hostname.local
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <Ticker.h>	 // For LED status

//// ###### User configuration space for AC library classes ##########
#include <ir_Midea.h>  //  replace library based on your AC unit model, check https://github.com/crankyoldgit/IRremoteESP8266

#define AUTO_MODE kMideaACAuto
#define COOL_MODE kMideaACCool
#define DRY_MODE kMideaACDry
#define HEAT_MODE kMideaACHeat
#define FAN_MODE kMideaACFan
#define FAN_AUTO kMideaACFanAuto
#define FAN_MIN kMideaACFanLow
#define FAN_MED kMideaACFanMed
#define FAN_HI kMideaACFanHigh

/// ##### Start user configuration ######
const uint16_t kIrLed = 15;	 // ESP8266 GPIO pin to use for IR blaster.
const int configpin = 10;
int port = 80;
IRMideaAC ac(kIrLed);  // Library initialization, change it according to the
					   // imported library file.
const bool enableMDNSServices = true;
const int ledpin = LED_BUILTIN;
char wifi_config_name[] = "ACRemote";
char host_name[20] = "";
/// ##### End user configuration ######
bool shouldSaveConfig = false;	// Flag for saving data
struct state {
	uint8_t temperature = 22, fan = 0, operation = 0;
	bool powerStatus;
};

File fsUploadFile;

state acState;
Ticker ticker;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdateServer;

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
		if (SPIFFS.exists(
				pathWithGz))  // If there's a compressed version available
			path += ".gz";	  // Use the compressed verion
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
		if (!filename.startsWith("/"))
			filename = "/" + filename;
		// Serial.print("handleFileUpload Name: "); //Serial.println(filename);
		fsUploadFile = SPIFFS.open(filename, "w");
		// Open the file for writing in SPIFFS (create if it doesn't exist)
		filename = String();
	} else if (upload.status == UPLOAD_FILE_WRITE) {
		if (fsUploadFile)
			fsUploadFile.write(upload.buf, upload.currentSize);
		// Write the received bytes to the file
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
void tick() {
	int state = digitalRead(ledpin);  // get the current state of GPIO1 pin
	digitalWrite(ledpin, !state);	  // set pin to the opposite state
}

//+=============================================================================
// Callback notifying us of the need to save config
//
void saveConfigCallback() {
	Serial.println("Should save config");
	shouldSaveConfig = true;
}

//+=============================================================================
// Turn off the Led after timeout
//
void disableLed() {
	Serial.println("Turning off the LED to save power.");
	digitalWrite(ledpin, HIGH);	 // Shut down the LED
	ticker.detach();			 // Stopping the ticker
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
	ticker.attach(0.2, tick);
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
// Enable MDNS Function
//
void enableMDNS() {
	// Configure OTA Update
	ArduinoOTA.setPort(8266);
	ArduinoOTA.setHostname(host_name);
	ArduinoOTA.onStart([]() { Serial.println("Start"); });
	ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
	});
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
	MDNS.addService("http", "tcp",
					port);	// Announce the ESP as an HTTP service
	Serial.println("MDNS http service added. Hostname is set to " +
				   String(host_name) + ".local:" + String(port));
}

//+=============================================================================
// Setup HTTP server
//
void serverSetup() {
	server.on("/state", HTTP_PUT, []() {
		DynamicJsonDocument root(1024);
		DeserializationError error = deserializeJson(root, server.arg("plain"));
		if (error) {
			server.send(404, "text/plain", "FAIL. " + server.arg("plain"));
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

			String output;
			serializeJson(root, output);
			server.send(200, "text/plain", output);

			delay(200);

			if (acState.powerStatus) {
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
			} else {
				ac.off();
			}
			ac.send();
		}
	});

	server.on(
		"/file-upload", HTTP_POST,
		// if the client posts to the upload page
		[]() {
			// Send status 200 (OK) to tell the client we are ready to receive
			server.send(200);
		},
		handleFileUpload);	// Receive and save the file

	server.on("/file-upload", HTTP_GET, []() {
		// if the client requests the upload page

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

	server.on("/state", HTTP_GET, []() {
		DynamicJsonDocument root(1024);
		root["mode"] = acState.operation;
		root["fan"] = acState.fan;
		root["temp"] = acState.temperature;
		root["power"] = acState.powerStatus;
		String output;
		serializeJson(root, output);
		server.send(200, "text/plain", output);
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
// Setup Wifi
//
bool setupWifi(bool resetConf) {
	// start ticker with 0.5 because we start in AP mode and try to connect
	ticker.attach(0.5, tick);

	// WiFiManager
	// Local intialization. Once its business is done, there is no need to keep
	// it around
	WiFiManager wifiManager;
	// reset settings - for testing
	if (resetConf)
		wifiManager.resetSettings();

	// set callback that gets called when connecting to previous WiFi fails, and
	// enters Access Point mode
	wifiManager.setAPCallback(configModeCallback);
	// set config save notify callback
	wifiManager.setSaveConfigCallback(saveConfigCallback);

	// Reset device if on config portal for greater than 3 minutes
	wifiManager.setConfigPortalTimeout(180);

	if (SPIFFS.begin()) {
		Serial.println("mounted file system");
		if (SPIFFS.exists("/config.json")) {
			// file exists, reading and loading
			Serial.println("reading config file");
			File configFile = SPIFFS.open("/config.json", "r");
			if (configFile) {
				Serial.println("opened config file");
				size_t size = configFile.size();
				// Allocate a buffer to store contents of the file.
				std::unique_ptr<char[]> buf(new char[size]);

				configFile.readBytes(buf.get(), size);
				DynamicJsonDocument json(1024);
				DeserializationError error = deserializeJson(json, buf.get());
				serializeJson(json, Serial);
				if (!error) {
					Serial.println("\nparsed json");

					if (json.containsKey("hostname"))
						strncpy(host_name, json["hostname"], 20);
				} else {
					Serial.println("failed to load json config");
				}
			}
		}
	} else {
		Serial.println("failed to mount FS");
	}

	WiFiManagerParameter custom_hostname(
		"hostname", "Choose a hostname to this IR Controller", host_name, 20);
	wifiManager.addParameter(&custom_hostname);

	// fetches ssid and pass and tries to connect
	// if it does not connect it starts an access point with the specified name
	// and goes into a blocking loop awaiting configuration

	if (!wifiManager.autoConnect(wifi_config_name)) {
		Serial.println("Failed to connect and hit timeout");
		// reset and try again, or maybe put it to deep sleep
		delay(3000);
		ESP.reset();
		delay(5000);
	}

	// if you get here you have connected to the WiFi
	strncpy(host_name, custom_hostname.getValue(), 20);

	// Reset device if lost wifi Connection
	WiFi.onStationModeDisconnected(&lostWifiCallback);

	Serial.println("WiFi connected! User chose hostname '" + String(host_name));
	// save the custom parameters to FS
	if (shouldSaveConfig) {
		Serial.println(" config...");
		DynamicJsonDocument json(50);
		json["hostname"] = host_name;

		File configFile = SPIFFS.open("/config.json", "w");
		if (!configFile) {
			Serial.println("failed to open config file for writing");
		}

		serializeJson(json, Serial);
		Serial.println("");
		Serial.println("Writing config file");
		serializeJson(json, configFile);
		configFile.close();
		json.clear();
		Serial.println("Config written successfully");
	}
	ticker.detach();

	// keep LED on
	digitalWrite(ledpin, LOW);
	return true;
}

//+=============================================================================
// MAIN SETUP
//
void setup() {
	pinMode(ledpin, OUTPUT);
	Serial.begin(115200);
	// Serial.println();
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
	// Turn off the led in 2s
	ticker.attach(2, disableLed);

	Serial.print("Local IP: ");
	Serial.println(WiFi.localIP().toString());
	Serial.println("URL to send commands: http://" + String(host_name) +
				   ".local:" + port);

	if (enableMDNSServices) {
		enableMDNS();
	}

	httpUpdateServer.setup(&server);

	serverSetup();

	server.begin();
}

//+=============================================================================
//
void loop() { server.handleClient(); }