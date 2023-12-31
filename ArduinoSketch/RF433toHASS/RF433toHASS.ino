#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <RCSwitch.h>
#include <WiFiClientSecure.h>

#define configFilePath "/config.json"
#define configResetFilePath "/blank_config.json"
#define default_AP_Name "rfbridge"
#define RF_Receiver_Pin 32
#define WebserverPort 80
#define JSON_Memory_Size 750


class RFBridge{
  public:
  
  DynamicJsonDocument JSON_MEMORY{JSON_Memory_Size};
  
  bool init(){
    if (!SPIFFS.begin()) {
      return false;
    }
    return true;
  }
  
  bool readJsonFileToMemory() {
    configFile = SPIFFS.open(configFilePath, "r");
    DeserializationError error = deserializeJson(JSON_MEMORY, configFile);
    if (error) {
      return false;
    }
    configFile.close();
    return true;
  }
  
  bool saveJsonMemoryToFile() {
    configFile = SPIFFS.open(configFilePath, "w");
    if (configFile) {
      serializeJson(JSON_MEMORY, configFile);
      configFile.close();
      return true;
    }
    else{
      return false;
    }
  }
  
  private:
    File configFile;

    
};

RFBridge RFBridge;
AsyncWebServer server(WebserverPort);


RCSwitch mySwitch = RCSwitch();

void setup() {
  delay(1000);
  Serial.begin(9600);
  if(Serial){
    delay(1000);
    mySwitch.enableReceive(RF_Receiver_Pin);
    
    if(RFBridge.init()){
      Serial.println(F("Mounting the file system"));      
      delay(1000);
      if(RFBridge.readJsonFileToMemory()){
        Serial.println(F("JSON copied to memory"));
        
        Serial.println(F("Starting WIFI Configuration"));
        if(handleWifiConfiguration(RFBridge.JSON_MEMORY["wifi_credentials"][0]["ssid"], RFBridge.JSON_MEMORY["wifi_credentials"][0]["password"], RFBridge.JSON_MEMORY["reset"])){
          Serial.println(F("Wifi Connected"));
        }else{
          Serial.println(F("AP Connected"));
        }
        
        setupRoutes();
        server.begin();
        Serial.println(F("Server is starting"));

        }
      }
      else{
        Serial.println(F("Error reading json file"));
        return;
      }
    }
    else{
      Serial.println(F("Failed to mount file system"));
      return;
    }
  }




void loop() {
  if (mySwitch.available()) {
    Serial.println("RF Available");

    if (RFBridge.JSON_MEMORY["LearningEnabled"]) {
      addDevicetoJSON(mySwitch.getReceivedValue());
      delay(3000);
    } else {
      if (checkRF(mySwitch.getReceivedValue())) {
          Serial.println("Sending Webhook");
          sendWebhook(mySwitch.getReceivedValue());
          delay(1000);
        
      }
    }

  
    mySwitch.resetAvailable();
  }

  if (RFBridge.JSON_MEMORY["restart_esp"]) {
    delay(1000);
    ESP.restart();
    RFBridge.JSON_MEMORY["restart_esp"] = false;
  }
}

bool handleWifiConfiguration(const char* ssid, const char* password, const char* reset) {
  if (strcmp(reset, "true") == 0) {
    resetConfigFile();
    RFBridge.JSON_MEMORY["restart_esp"] = true;
  } 
  else if (strcmp(reset, "false") == 0) {
    Serial.println("Checking if the default ap name is the same as the current ssid name");
    if (!RFBridge.JSON_MEMORY["isWifiSet"]) {
      Serial.println("Wifi is not set switching to AP");
      if(createAP(default_AP_Name, default_AP_Name)){
        Serial.println("AP Mode started");
      }else{
        Serial.println("Error with setting up AP Mode");
      }
    } 
    else {
      connectToWiFi(ssid, password);
    }
  }
  else{
    return false;
  }
  mdns();
  return true;
}

bool checkRF(unsigned long rfcode){
  JsonArray rfcodesArray = RFBridge.JSON_MEMORY["rfcodes"].as<JsonArray>();
  for (JsonObject entry : rfcodesArray) {
    if (entry["code"].as<unsigned long>() == rfcode) {
      Serial.println("Match found");
      return true;
    }
  }
  return false;
}

void sendWebhook(unsigned long rfcode) {
  WiFiClientSecure client;
  client.setInsecure();  // Disable SSL certificate validation
  String url = "/api/webhook/" + String(rfcode);
    if (client.connect(RFBridge.JSON_MEMORY["webhook_settings"][0]["homeAssistantIP"].as<String>().c_str(), RFBridge.JSON_MEMORY["webhook_settings"][0]["homeAssistantPort"].as<int>())) {
      // Make a POST request
      client.print("POST " + url + " HTTP/1.1\r\n");
      client.print("Host: " + String(RFBridge.JSON_MEMORY["webhook_settings"][0]["homeAssistantIP"].as<String>()) + "\r\n");
      client.print("Connection: close\r\n");
      client.print("Content-Length: 0\r\n\r\n");
      client.stop();
    }
}

void setupRoutes() {
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", String(), false);
  });

  server.on("/w3.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, F("/w3.css"), F("text/css"));
  });

  server.on("/savewifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    RFBridge.JSON_MEMORY["wifi_credentials"][0]["ssid"] = request->arg("ssid").c_str();
    RFBridge.JSON_MEMORY["wifi_credentials"][0]["password"] = request->arg("password").c_str();
    RFBridge.JSON_MEMORY["reset"] = "false";
    RFBridge.JSON_MEMORY["isWifiSet"] = "true";
    RFBridge.saveJsonMemoryToFile();
    request->redirect("/");
    
    RFBridge.JSON_MEMORY["restart_esp"] = true;
  });

server.on("/api", HTTP_POST, [](AsyncWebServerRequest *request){
  // Check if the request has a JSON payload
  if (request->hasParam("plain", true)) {
    // Parse the JSON data
    DynamicJsonDocument tempJSON(128); // Adjust the size as needed
    DeserializationError error = deserializeJson(tempJSON, request->getParam("plain", true)->value());

    // Check for parsing errors
    if (error) {
      Serial.print(F("JSON parsing failed: "));
      Serial.println(error.c_str());
      sendJsonResponse(*request, "{\"error\": \"" + String(error.c_str()) + "\" }");
    } else {
        //check command is "learning mode"
        if(strcmp(tempJSON["command"], "learningmode") == 0){
          //check if the parameter is false or true for disabling/enabling learning mode
          if(tempJSON["parameter"].as<bool>()){
            RFBridge.JSON_MEMORY["LearningEnabled"] = true;
            sendJsonResponse(*request, "{\"success\": true, \"message\":\"Learning mode enabled\" }");
          }
          else{
            RFBridge.JSON_MEMORY["LearningEnabled"] = false;
            sendJsonResponse(*request, "{\"success\": true, \"message\":\"Learning mode disabled\" }");
          }

        }
        else if(strcmp(tempJSON["command"], "getpageinfo") == 0){
             sendJsonMemoryToWebpage(request);
        }
        else{
        sendJsonResponse(*request, "{\"error\": \"command unknown\" }");
        }
    }
  } else {
    Serial.println(F("No JSON payload in the request."));
    
  }
});


}


void sendJsonResponse(AsyncWebServerRequest& request, String content) {
    AsyncWebServerResponse *response = request.beginResponse(200, "application/json", content);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request.send(response);
}

void sendJsonMemoryToWebpage(AsyncWebServerRequest *request) {
  
    // Convert the DynamicJsonDocument to a string
    String content;
    serializeJson(RFBridge.JSON_MEMORY, content);
    sendJsonResponse(*request, content);
}

/*  
server.on("/deleteRfCode", HTTP_POST, [](AsyncWebServerRequest *request) {server.on("/disableLearn", HTTP_GET, [](AsyncWebServerRequest *request){
    String codeToDelete = request->arg("code").c_str();
    unsigned long code = strtoul(codeToDelete.c_str(), NULL, 10);
    deleteRfCodeFromJson(code);

    request->send(200, "text/plain", "RF code deleted successfully");
});
*/  


bool createAP(const char* ssid, const char* password) {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  return true;
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
    Serial.println(F("Error resetting config file"));
  }
}

void mdns(){
  if (!MDNS.begin("rfbridge")) {
   Serial.println(F("Error setting up mDNS"));
  } else {
    Serial.println(F("mDNS Starting"));
  }
}

void connectToWiFi(const char* ssid, const char* password) {
  int attempts = 0;
  
  while (attempts < 5 && WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
    delay(1000);
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Failed to connect to WiFi after multiple attempts. Reverting to default WiFi credentials and starting AP."));
    RFBridge.JSON_MEMORY["isWifiSet"] = false;
    RFBridge.saveJsonMemoryToFile();
    RFBridge.JSON_MEMORY["restart_esp"] = true;
  }
}

void addDevicetoJSON(unsigned long receivedCode){
  JsonArray rfcodesArray = RFBridge.JSON_MEMORY["rfcodes"].as<JsonArray>();
      for (JsonObject entry : rfcodesArray) {
        if (entry["code"].as<unsigned long>() == receivedCode) {
            // Code already exists, you can choose to update the webhook or reject the new learning attempt

            return;
        }
      }
         JsonObject newEntry = rfcodesArray.createNestedObject();
          newEntry["code"] = receivedCode;
          RFBridge.JSON_MEMORY["LearningEnabled"] = false;
          RFBridge.saveJsonMemoryToFile();
        
    
}
/*
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
}*/
