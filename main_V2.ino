#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <RCSwitch.h>
#include <WiFiClientSecure.h>

AsyncWebServer server(80);
const char* homeAssistantIP = "192.168.68.162";
const int homeAssistantPort = 443;
const char* configFilePath = "/config.json";
const char* configResetFilePath = "/blank_config.json";
bool arestart = false;
bool isWifiSet = false;
bool debug = false;

DynamicJsonDocument configJson(512);
RCSwitch mySwitch = RCSwitch();
File configFile;




void setup() {
  Serial.begin(9600);
  delay(1000);
  mySwitch.enableReceive(32);
  if (!SPIFFS.begin()) {
    printErrorCodetoSerial(1, debug);
    //Serial.println(F("Failed to mount file system"));
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
    if (mySwitch.available()) {
        unsigned long currentMillis = millis();

        if (configJson["LearningEnabled"]) {
            addDevicetoJSON(mySwitch.getReceivedValue());
            delay(3000);

            // Call the function to send the webhook
            
        } else {
          
          unsigned long receivedCode = mySwitch.getReceivedValue();
          if(checkRF(receivedCode)){
            sendWebhook(receivedCode);
          }
        }

        mySwitch.resetAvailable();
    }
  
  if (arestart) {
    delay(1000);
    ESP.restart();
    arestart = false;
  }
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

void printErrorCodetoSerial(uint8_t errorCode, bool enableSerial){
  if(enableSerial){
    Serial.println("Error Code: " + String(errorCode));
  }
}

void readJsonFile() {
  configFile = SPIFFS.open(configFilePath, "r");
  DeserializationError error = deserializeJson(configJson, configFile);
  if (error) {
    printErrorCodetoSerial(2, debug);
    //Serial.println(F("Error reading config file"));
  }
  configFile.close();
}

bool checkRF(unsigned long rfcode){
  JsonArray rfcodesArray = configJson["rfcodes"].as<JsonArray>();
  for (JsonObject entry : rfcodesArray) {
    if (entry["code"].as<unsigned long>() == rfcode) {
       return true;
    }
  }
  return false;
}
void sendWebhook(unsigned long rfcode) {

 

    WiFiClientSecure client;
    client.setInsecure();  // Disable SSL certificate validation

            String url = "/api/webhook/" + String(rfcode);

            if (client.connect(homeAssistantIP, homeAssistantPort)) {
                // Make a POST request
                client.print("POST " + url + " HTTP/1.1\r\n");
                client.print("Host: " + String(homeAssistantIP) + "\r\n");
                client.print("Connection: close\r\n");
                client.print("Content-Length: 0\r\n\r\n");
 
                client.stop();
                
            }
 
            return;
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

  server.on("/enableLearn", HTTP_GET, [](AsyncWebServerRequest *request){
    configJson["LearningEnabled"] = true;
    request->redirect("/");
  });
  server.on("/disableLearn", HTTP_GET, [](AsyncWebServerRequest *request){
    configJson["LearningEnabled"] = false;
    request->redirect("/");
  });
server.on("/deleteRfCode", HTTP_POST, [](AsyncWebServerRequest *request) {
    String codeToDelete = request->arg("code").c_str();
    unsigned long code = strtoul(codeToDelete.c_str(), NULL, 10);
    deleteRfCodeFromJson(code);

    request->send(200, "text/plain", "RF code deleted successfully");
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
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
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
  } else {
    printErrorCodetoSerial(3, debug);
    //Serial.println(F("Error resetting config file"));
  }
}

void mdns(){
  if (!MDNS.begin("rfbridge")) {
    printErrorCodetoSerial(4, debug);
    //Serial.println(F("Error setting up mDNS"));
  } else {
  }
}

void connectToWiFi(const char* ssid, const char* password) {
  
  int attempts = 0;
  while (attempts < 5) {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED && attempts < 5) {
      delay(1000);
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      
      break;
    } else {
      Serial.println(F("Connection failed. Retrying..."));
      attempts++;
    }
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    printErrorCodetoSerial(5, debug);
    //Serial.println(F("Failed to connect to WiFi after multiple attempts. Reverting to default WiFi credentials and starting AP."));
    createAP("rfbridge", "rfbridge");
  }
}

void addDevicetoJSON(unsigned long receivedCode){
  JsonArray rfcodesArray = configJson["rfcodes"].as<JsonArray>();
      for (JsonObject entry : rfcodesArray) {
        if (entry["code"].as<unsigned long>() == receivedCode) {
            // Code already exists, you can choose to update the webhook or reject the new learning attempt
           printErrorCodetoSerial(6, debug);
            return;
        }
      }
         JsonObject newEntry = rfcodesArray.createNestedObject();
          newEntry["code"] = receivedCode;
          configJson["LearningEnabled"] = false;
          saveConfigToFile();
        
    
}

void deleteRfCodeFromJson(unsigned long codeToDelete) {
  // Ensure that the JSON document contains an array named "rfcodes"
  JsonArray rfcodesArray = configJson["rfcodes"].as<JsonArray>();

  // Iterate through the array to find and remove the specified code
  for (int i = 0; i < rfcodesArray.size(); i++) {
    unsigned long code = rfcodesArray[i]["code"].as<unsigned long>();
    if (code == codeToDelete) {
      // Remove the code from the array
      rfcodesArray.remove(i);

      // Save the updated configuration to the file
      saveConfigToFile();

      // Exit the loop since the code has been found and removed
      return;
    }
  }

  // If the codeToDelete was not found, you may want to log or handle this situation
  printErrorCodetoSerial(7, debug); // Use a new error code for this scenario, e.g., 7
}
