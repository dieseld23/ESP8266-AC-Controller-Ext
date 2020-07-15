# 1 "C:\\Users\\Razer\\AppData\\Local\\Temp\\tmpb0bjh0zr"
#include <Arduino.h>
# 1 "C:/Users/Razer/Documents/PlatformIO/Projects/esp8266-AC-control/src/esp8266-AC-control.ino"
# 15 "C:/Users/Razer/Documents/PlatformIO/Projects/esp8266-AC-control/src/esp8266-AC-control.ino"
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <LittleFS.h>
#include <Ticker.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>

#include <ir_Midea.h>



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


const uint16_t kIrLed = 15;
const int configpin = 10;
int port = 80;
const int ledpin = LED_BUILTIN;

const int led3pin = 14;


bool firstRun = true;
const bool enableMDNSServices = true;
const size_t bufferSize = JSON_OBJECT_SIZE(22) + 300;
char wifi_config_name[] = "ESP Setup";
char host_name[20] = "garageac";
char tstatIP[20] = "garagetstat";
unsigned long previousMillis = 0;
unsigned long currentMillis = 0;
bool shouldSaveConfig = false;
bool setLED = false;
struct state {
 uint8_t temperature = 85;
 uint8_t fan = 0;
 uint8_t mode = 0;
 bool powerStatus;
 bool extControl = true;
 bool changed = false;
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
IRMideaAC ac(kIrLed);
tstatData tstat;
File fsUploadFile;
state acState;
state acStateOld;
Ticker led1tick;
Ticker led3tick;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdateServer;
WebSocketsServer webSocket(81);
String getContentType(String filename);
bool handleFileRead(String path);
bool compareACstate();
void handleFileUpload();
void handleNotFound();
void led1Ticker();
void led1TickerDisable();
void led3Ticker();
void led3TickerDisable();
void saveConfigCallback();
void configModeCallback(WiFiManager *myWiFiManager);
void lostWifiCallback(const WiFiEventStationModeDisconnected &evt);
void getVenstarStatus();
void enableMDNS();
int convertTemp(float temp, uint8_t type);
void serverSetup();
void startWebSocket();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void controlAC();
void sendDataToWeb();
void dataInitialize();
void dataWrite();
bool setupWifi(bool resetConf);
void setup();
void loop();
#line 108 "C:/Users/Razer/Documents/PlatformIO/Projects/esp8266-AC-control/src/esp8266-AC-control.ino"
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




bool handleFileRead(String path) {

 if (path.endsWith("/"))
  path += "index.html";

 String contentType = getContentType(path);

 String pathWithGz = path + ".gz";
 if (LittleFS.exists(pathWithGz) || LittleFS.exists(path)) {

  if (LittleFS.exists(pathWithGz))
   path += ".gz";
  File file = LittleFS.open(path, "r");

  server.streamFile(file, contentType);

  file.close();


  return true;
 }


 return false;
}




bool compareACstate() {
 if (acState.powerStatus != acStateOld.powerStatus || acState.extControl != acStateOld.extControl || acState.extControl != acStateOld.extControl ||
  acState.mode != acStateOld.mode || acState.fan != acStateOld.fan || acState.temperature != acStateOld.temperature) {
  acStateOld = acState;
  return true;
 } else {
  return false;
 }
}




void handleFileUpload() {
 HTTPUpload &upload = server.upload();
 if (upload.status == UPLOAD_FILE_START) {
  String filename = upload.filename;
  if (!filename.startsWith("/")) {
   filename = "/" + filename;
  }

  fsUploadFile = LittleFS.open(filename, "w");

  filename = String();
 } else if (upload.status == UPLOAD_FILE_WRITE) {
  if (fsUploadFile) {
   fsUploadFile.write(upload.buf, upload.currentSize);

  }
 } else if (upload.status == UPLOAD_FILE_END) {
  if (fsUploadFile) {

   fsUploadFile.close();



   server.sendHeader("Location", "/success.html");

   server.send(303);
  } else {
   server.send(500, "text/plain", "500: couldn't create file");
  }
 }
}




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




void led1Ticker() {
 int state = digitalRead(ledpin);
 digitalWrite(ledpin, !state);
}




void led1TickerDisable() {
 Serial.println("Turning off the LED to save power.");
 digitalWrite(ledpin, HIGH);
 led1tick.detach();
}




void led3Ticker() {
 if (digitalRead(led3pin) == HIGH) {

 }
 digitalWrite(led3pin, HIGH);
 Serial.println("ledon");
}




void led3TickerDisable() {
 digitalWrite(led3pin, HIGH);

 setLED = false;
}




void saveConfigCallback() {
 Serial.println("Should save config");
 shouldSaveConfig = true;
}




void configModeCallback(WiFiManager *myWiFiManager) {
 Serial.println("Entered config mode");
 Serial.println(WiFi.softAPIP());

 Serial.println(myWiFiManager->getConfigPortalSSID());

 led1tick.attach(0.2, led1Ticker);
}




void lostWifiCallback(const WiFiEventStationModeDisconnected &evt) {
 Serial.println("Lost Wifi");

 ESP.reset();
 delay(1000);
}




void getVenstarStatus() {
 WiFiClient client;
 String url = "http://" + String(tstatIP) + "/query/info";
 HTTPClient http;
 http.setReuse(true);
 http.begin(client, url);
 int httpCode = http.GET();

 if (httpCode > 0) {
  DynamicJsonDocument doc(bufferSize);
  DeserializationError error = deserializeJson(doc, http.getString());
  if (error) {
   Serial.println("There was an error while deserializing");
   http.end();
  } else {
   tstat.name = doc["name"];
   tstat.mode = doc["mode"];
   tstat.currentState = doc["state"];
   tstat.activestage = doc["activestage"];
   tstat.fan = doc["fan"];
   tstat.fanstate = doc["fanstate"];
   tstat.tempunits = doc["tempunits"];
   tstat.schedule = doc["schedule"];
   tstat.schedulepart = doc["schedulepart"];
   tstat.away = doc["away"];
   tstat.spacetemp = convertTemp(doc["spacetemp"].as<float>(), 0);
   tstat.heattemp = convertTemp(doc["heattemp"].as<float>(), 0);
   tstat.cooltemp = convertTemp(doc["cooltemp"].as<float>(), 0);
   tstat.cooltempmin = convertTemp(doc["cooltempmin"].as<float>(), 0);
   tstat.cooltempmax = convertTemp(doc["cooltempmax"].as<float>(), 0);
   tstat.heattempmin = convertTemp(doc["heattempmin"].as<float>(), 0);
   tstat.heattempmax = convertTemp(doc["heattempmax"].as<float>(), 0);
   tstat.setpointdelta = convertTemp(doc["setpointdelta"].as<float>(), 0);
   tstat.availablemodes = doc["availablemodes"];
  }
  http.end();
 }
}




void enableMDNS() {

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


 MDNS.addService("http", "tcp", port);
 Serial.println("MDNS http service added. Hostname is set to " + String(host_name) + ".local:" + String(port));
}




int convertTemp(float temp, uint8_t type) {
 if (type == 0) {
  return int(round((temp * 9) / 5) + 32);
 }
 else if (type == 1)
  return int(round((temp * 5) / 9) - 32);
 else {
  return 0;
 }
}




void serverSetup() {
 server.on(
  "/file-upload", HTTP_POST,
  []() {
   server.send(200);
  },
  handleFileUpload);

 server.on("/file-upload", HTTP_GET, []() {
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

 server.serveStatic("/", LittleFS, "/", "max-age=86400");

 server.onNotFound(handleNotFound);
}




void startWebSocket() {
 webSocket.begin();
 webSocket.onEvent(webSocketEvent);
 Serial.println("WebSocket server started.");
}




void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
 switch (type) {
  case WStype_DISCONNECTED: {
   Serial.printf("[%u] Disconnected!\n", num);
   break;
  }
  case WStype_CONNECTED: {
   IPAddress ip = webSocket.remoteIP(num);
   Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
   sendDataToWeb();
   break;
  }
  case WStype_TEXT: {
   String pl = "";
   for(int i = 0; i < length; i++) {
    pl += (char)payload[i];
   }
   if (pl == "heartbeat") {

    webSocket.sendTXT(num,pl);
   }
   else {
    Serial.printf("[%u] get Text: %s\n", num, payload);
    DynamicJsonDocument root(1024);
    DeserializationError error = deserializeJson(root, payload);
    if (error) {

    }
    else {
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
      acState.mode = root["mode"];
     }
     if (root.containsKey("extControl")) {
      acState.extControl = root["extControl"];
     }
     delay(200);
    }
    break;
   }
  }
 }
}



void controlAC() {
 if (compareACstate()) {
  if (acState.powerStatus == true && acState.extControl == false) {
   ac.on();
   ac.setTemp(acState.temperature);
   if (acState.mode == 0) {
    ac.setMode(AUTO_MODE);
    ac.setFan(FAN_AUTO);
    acState.fan = 0;
   } else if (acState.mode == 1) {
    ac.setMode(COOL_MODE);
   } else if (acState.mode == 2) {
    ac.setMode(DRY_MODE);
   } else if (acState.mode == 3) {
    ac.setMode(HEAT_MODE);
   } else if (acState.mode == 4) {
    ac.setMode(FAN_MODE);
   }

   if (acState.mode != 0) {
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
    acState.powerStatus = true;
    acState.mode = 1;
    acState.temperature = 65;
    acState.fan = 0;
    ac.on();
    ac.setTemp(acState.temperature);
    ac.setFan(acState.fan);
    ac.send();
    delay(200);
    ac.setEconoToggle(true);
   } else {
    acState.powerStatus = false;
    acState.mode = 0;
    acState.fan = 0;
    acState.temperature = tstat.cooltemp;
   }
  } else {
   ac.off();
  }
  sendDataToWeb();
  ac.send();
  setLED = true;
  dataWrite();
 }
}

void sendDataToWeb() {
 DynamicJsonDocument root(1024);
 root["mode"] = acState.mode;
 root["fan"] = acState.fan;
 root["temp"] = acState.temperature;
 root["power"] = acState.powerStatus;
 root["extControl"] = acState.extControl;
 String output;
 serializeJson(root, output);
 webSocket.broadcastTXT(output);
}




void dataInitialize() {
 if (LittleFS.exists("/data.json")) {
  Serial.println("Reading data file");
  File dataFile = LittleFS.open("/data.json", "r");
  if (dataFile) {
   Serial.println("Opened data file");
   size_t size = dataFile.size();
   std::unique_ptr<char[]> buf(new char[size]);
   dataFile.readBytes(buf.get(), size);
   DynamicJsonDocument root(1024);
   DeserializationError error = deserializeJson(root, buf.get());
   if (!error) {
    Serial.println("\nParsed json");
    if (root.containsKey("temp")) {
     acState.temperature = (uint8_t)root["temp"];
    } else {
     acState.temperature = 85;
    }
    if (root.containsKey("fan")) {
     acState.fan = (uint8_t)root["fan"];
    } else {
     acState.fan = 0;
    }
    if (root.containsKey("power")) {
     acState.powerStatus = root["power"];
    } else {
     acState.powerStatus = false;
    }
    if (root.containsKey("mode")) {
     acState.mode = root["mode"];
    } else {
     acState.mode = 0;
    }
    if (root.containsKey("extControl")) {
     acState.extControl = root["extControl"];
    } else {
     acState.extControl = true;
    }
    dataFile.close();
   } else {
    Serial.println("Failed to load data json config");
   }
  }
 }

 else {
  Serial.println(" Initialize Data...");
  acState.temperature = 85;
  acState.fan = 0;
  acState.powerStatus = false;
  acState.mode = 0;
  acState.extControl = true;
  dataWrite();
 }
}



void dataWrite() {
 DynamicJsonDocument json(1024);
 json["temp"] = acState.temperature;
 json["fan"] = acState.fan;
 json["power"] = acState.powerStatus;
 json["mode"] = acState.mode;
 json["extControl"] = acState.extControl;
 File dataFile = LittleFS.open("/data.json", "w");
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



bool setupWifi(bool resetConf) {
 led1tick.attach(0.5, led1Ticker);

 WiFiManager wifiManager;
 if (resetConf)
  wifiManager.resetSettings();

 wifiManager.setAPCallback(configModeCallback);
 wifiManager.setSaveConfigCallback(saveConfigCallback);
 wifiManager.setConfigPortalTimeout(180);

 if (LittleFS.begin()) {
  Serial.println("Mounted file system");
  if (LittleFS.exists("/config.json")) {

   Serial.println("Reading config file");
   File configFile = LittleFS.open("/config.json", "r");
   if (configFile) {
    Serial.println("Opened config file");
    size_t size = configFile.size();
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
    configFile.close();
   }
  }
 } else {
  Serial.println("Failed to mount FS");
 }

 WiFiManagerParameter custom_hostname("hostname", "Choose a hostname to this IR Controller", host_name, 20);
 WiFiManagerParameter custom_tstatIP("tstatIP", "Venstar Thermostat IP", tstatIP, 20);

 wifiManager.addParameter(&custom_hostname);
 wifiManager.addParameter(&custom_tstatIP);


 if (!wifiManager.autoConnect(wifi_config_name)) {
  Serial.println("Failed to connect and hit timeout");
  delay(1000);
  ESP.reset();
  delay(2000);
 }


 strncpy(host_name, custom_hostname.getValue(), 20);
 strncpy(tstatIP, custom_tstatIP.getValue(), 20);
 WiFi.onStationModeDisconnected(&lostWifiCallback);
 Serial.println("WiFi connected! User chose hostname '" + String(host_name) + "'");
 if (shouldSaveConfig) {
  Serial.println(" Config...");
  DynamicJsonDocument json(100);
  json["hostname"] = host_name;
  json["tstatIP"] = tstatIP;
  File configFile = LittleFS.open("/config.json", "w");
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
 digitalWrite(ledpin, LOW);
 return true;
}




void setup() {
 pinMode(ledpin, OUTPUT);

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

 led1tick.attach(2, led1TickerDisable);

 Serial.print("Local IP: ");
 Serial.println(WiFi.localIP().toString());
 Serial.println("URL to send commands: http://" + String(host_name) + ":" + port);
 Serial.println("Tstat IP set to: http://" + String(tstatIP) + ":" + port);

 if (enableMDNSServices) {
  enableMDNS();
 }

 httpUpdateServer.setup(&server);
 serverSetup();
 dataInitialize();
 startWebSocket();
 server.begin();
}



void loop() {
 webSocket.loop();
 ArduinoOTA.handle();
 server.handleClient();
 MDNS.update();
 currentMillis = millis();
 if (acState.extControl == true || firstRun == true) {
  if (currentMillis - previousMillis > 2000) {
   previousMillis = currentMillis;
   getVenstarStatus();

  }
 }
 controlAC();
 if (setLED == true) {

  led3tick.attach(3, led3TickerDisable);
 }
 firstRun = false;
}