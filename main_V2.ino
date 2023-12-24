#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPmDNS.h>

const char* configFilePath = "/config.json";
const char* configResetFilePath = "/blank_config.json";

DynamicJsonDocument configJson(1024);
File configFile;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  delay(1000);
  
  if(!SPIFFS.begin()){
    Serial.println("Failed to mount file system");
    return;
  }
  delay(1000);

  readJsonFile();
  
  const char* ssid = configJson["wifi_credentials"][0]["ssid"];
  const char* password = configJson["wifi_credentials"][0]["password"];
  const char* reset = configJson["reset"];
  
  WiFi.begin(ssid, password);
  if (strcmp(reset, "true") == 0) {
    resetConfigFile();
    ESP.restart();
  }
  if(strcmp(reset,"false") == 0){ 
    if (strcmp(ssid, "rfbridge") == 0) {
      createAP(ssid, password);
      mdns();
    } 
    else {
      connectToWiFi(ssid, password);
      mdns();
    }
  }
}

void loop() {
  // put your main code here, to run repeatedly:

}

void readJsonFile(){
  File configFile = SPIFFS.open(configFilePath, "r");
  DeserializationError error = deserializeJson(configJson, configFile);
  if(error){
    Serial.println("error");
  }
  configFile.close(); 
}

void createAP(const char* ssid, const char* password) {
  Serial.println("Creating Access Point");
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
    Serial.println("Config file reset");
  } else {
    Serial.println("Error resetting config file");
  }
}

void mdns(){
  if (!MDNS.begin("rfbridge")) {
    Serial.println("Error setting up mDNS");
  } else {
    Serial.println("mDNS responder started");
  }
}

void connectToWiFi(const char* ssid, const char* password) {
  Serial.println("Connecting to WiFi");
  
  int attempts = 0;
  while (attempts < 5) {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED && attempts < 5) {
      delay(1000);
      Serial.println("Connecting to WiFi...");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to WiFi");
      break;
    } else {
      Serial.println("Connection failed. Retrying...");
      attempts++;
    }
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to WiFi after multiple attempts. Reverting to default WiFi credentials and starting AP.");
    createAP("rfbridge", "rfbridge");
    
  }
}
