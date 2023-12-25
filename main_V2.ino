#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>

AsyncWebServer server(80);

const char* configFilePath = "/config.json";
const char* configResetFilePath = "/blank_config.json";
bool arestart = false;
bool isWifiSet = false;

DynamicJsonDocument configJson(512);

File configFile;

void setup() {
  Serial.begin(9600);
  delay(1000);

  if (!SPIFFS.begin()) {
    Serial.println(F("Failed to mount file system"));
    return;
  }
  delay(1000);

  readJsonFile();

  const char* ssid = configJson["wifi_credentials"][0]["ssid"];
  const char* password = configJson["wifi_credentials"][0]["password"];
  const char* reset = configJson["reset"];

  handleWifiConfiguration(ssid, password, reset);

  setupRoutes();

  server.begin();
}

void loop() {
  if (arestart) {
    delay(1000);
    ESP.restart();
    arestart = false;
  }
}

void readJsonFile() {
  configFile = SPIFFS.open(configFilePath, "r");
  DeserializationError error = deserializeJson(configJson, configFile);
  if (error) {
    Serial.println(F("Error reading config file"));
  }
  configFile.close();
}

void handleWifiConfiguration(const char* ssid, const char* password, const char* reset) {
  if (strcmp(reset, "true") == 0) {
    resetConfigFile();
    arestart = true;
  } else if (strcmp(reset, "false") == 0) {
    isWifiSet = (strcmp(ssid, "rfbridge") != 0);
    if (!isWifiSet) {
      createAP(ssid, password);
    } else {
      connectToWiFi(ssid, password);
    }
    mdns();
  }
}

void setupRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    const char* page = (isWifiSet) ? "/index.html" : "/wificonfig.html";
    request->send(SPIFFS, page, String(), false);
  });

  server.on("/w3.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, F("/w3.css"), F("text/css"));
  });

  server.on("/savewifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    String ssid = request->arg("ssid");
    String password = request->arg("password");

    configJson["wifi_credentials"][0]["ssid"] = ssid.c_str();
    configJson["wifi_credentials"][0]["password"] = password.c_str();
    configJson["reset"] = "false";

    saveConfigToFile();
    request->redirect("/");
    
    arestart = true;
  });
  server.on("/getPageInfo", HTTP_GET, [](AsyncWebServerRequest *request){
    sendJSONToClient(request);
  });
}
void sendJSONToClient(AsyncWebServerRequest *request) {
  
    // Convert the DynamicJsonDocument to a string
    String content;
    serializeJson(configJson, content);

    // Create an AsyncWebServerResponse instance and set CORS headers
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", content);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Send the response
    request->send(response);
}

void saveConfigToFile() {
  configFile = SPIFFS.open(configFilePath, "w");
  if (configFile) {
    serializeJson(configJson, configFile);
    configFile.close();
  }
}

void createAP(const char* ssid, const char* password) {
  Serial.println(F("Creating Access Point"));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
}

void resetConfigFile() {
  File blankConfigFile = SPIFFS.open(configResetFilePath, "r");
  File configFile = SPIFFS.open(configFilePath, "w");

  if (blankConfigFile && configFile) {
    while (blankConfigFile.available()) {
      configFile.write(blankConfigFile.read());
    }
    configFile.close();
    blankConfigFile.close();
    Serial.println(F("Config file reset"));
  } else {
    Serial.println(F("Error resetting config file"));
  }
}

void mdns(){
  if (!MDNS.begin("rfbridge")) {
    Serial.println(F("Error setting up mDNS"));
  } else {
    Serial.println(F("mDNS responder started"));
  }
}

void connectToWiFi(const char* ssid, const char* password) {
  Serial.println(F("Connecting to WiFi"));
  
  int attempts = 0;
  while (attempts < 5) {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED && attempts < 5) {
      delay(1000);
      Serial.println(F("Connecting to WiFi..."));
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("Connected to WiFi"));
      break;
    } else {
      Serial.println(F("Connection failed. Retrying..."));
      attempts++;
    }
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Failed to connect to WiFi after multiple attempts. Reverting to default WiFi credentials and starting AP."));
    createAP("rfbridge", "rfbridge");
  }
}
