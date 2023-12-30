#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <RCSwitch.h>
#include <WiFiClientSecure.h>


class FileSystemSPIFFS{
  public:
  DynamicJsonDocument JSON_MEMORY{512};
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
    const char* configFilePath = "/config.json";
    const char* configResetFilePath = "/blank_config.json";
    
};

FileSystemSPIFFS FileSystemSPIFFS;
AsyncWebServer server(80);


const char* homeAssistantIP = "192.168.68.162";
const int homeAssistantPort = 443;
const char* configFilePath = "/config.json";
const char* configResetFilePath = "/blank_config.json";
bool arestart = false;
bool isWifiSet = false;
bool debug = false;


RCSwitch mySwitch = RCSwitch();


void setup() {
  delay(1000);
  Serial.begin(9600);
  if(Serial){
    delay(1000);
    mySwitch.enableReceive(32);
    
    if(FileSystemSPIFFS.init()){
      Serial.println(F("Mounting the file system"));      
      delay(1000);
      if(FileSystemSPIFFS.readJsonFileToMemory()){
        Serial.println(F("JSON copied to memory"));
        
        Serial.println(F("Starting WIFI Configuration"));
        handleWifiConfiguration(FileSystemSPIFFS.JSON_MEMORY["wifi_credentials"][0]["ssid"], FileSystemSPIFFS.JSON_MEMORY["wifi_credentials"][0]["password"], FileSystemSPIFFS.JSON_MEMORY["reset"]);
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
    if (FileSystemSPIFFS.JSON_MEMORY["LearningEnabled"]) {
      addDevicetoJSON(mySwitch.getReceivedValue());
      delay(3000);
    } 
    else {
      
      if(checkRF(mySwitch.getReceivedValue())){
        sendWebhook(mySwitch.getReceivedValue());
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



bool checkRF(unsigned long rfcode){
  JsonArray rfcodesArray = FileSystemSPIFFS.JSON_MEMORY["rfcodes"].as<JsonArray>();
  for (JsonObject entry : rfcodesArray) {
    if (entry["code"].as<unsigned long>() == rfcode) {
      Serial.println("Match found");
       return true;
    }
  }
  return false;
}
void sendWebhook(unsigned long rfcode) {

 Serial.println("Sending webhook");

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

    FileSystemSPIFFS.JSON_MEMORY["wifi_credentials"][0]["ssid"] = ssid.c_str();
    FileSystemSPIFFS.JSON_MEMORY["wifi_credentials"][0]["password"] = password.c_str();
    FileSystemSPIFFS.JSON_MEMORY["reset"] = "false";

   FileSystemSPIFFS.saveJsonMemoryToFile();
    request->redirect("/");
    
    arestart = true;
  });
  
  server.on("/getPageInfo", HTTP_GET, [](AsyncWebServerRequest *request){
    sendJSONToClient(request);
  });

  server.on("/enableLearn", HTTP_GET, [](AsyncWebServerRequest *request){
    FileSystemSPIFFS.JSON_MEMORY["LearningEnabled"] = true;
    String content = "{\"success\": true }";
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", content);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);
  });

  
  server.on("/disableLearn", HTTP_GET, [](AsyncWebServerRequest *request){
    FileSystemSPIFFS.JSON_MEMORY["LearningEnabled"] = false;
    String content = "{\"success\": true }";
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", content);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);
  });



/*  
server.on("/deleteRfCode", HTTP_POST, [](AsyncWebServerRequest *request) {server.on("/disableLearn", HTTP_GET, [](AsyncWebServerRequest *request){
    String codeToDelete = request->arg("code").c_str();
    unsigned long code = strtoul(codeToDelete.c_str(), NULL, 10);
    deleteRfCodeFromJson(code);

    request->send(200, "text/plain", "RF code deleted successfully");
});
*/  
}

void sendJSONToClient(AsyncWebServerRequest *request) {
  
    // Convert the DynamicJsonDocument to a string
    String content;
    serializeJson(FileSystemSPIFFS.JSON_MEMORY, content);

    // Create an AsyncWebServerResponse instance and set CORS headers
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", content);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Send the response
    request->send(response);
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
   Serial.println(F("Error setting up mDNS"));
  } else {
    Serial.println(F("mDNS Starting"));
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
  JsonArray rfcodesArray = FileSystemSPIFFS.JSON_MEMORY["rfcodes"].as<JsonArray>();
      for (JsonObject entry : rfcodesArray) {
        if (entry["code"].as<unsigned long>() == receivedCode) {
            // Code already exists, you can choose to update the webhook or reject the new learning attempt
           printErrorCodetoSerial(6, debug);
            return;
        }
      }
         JsonObject newEntry = rfcodesArray.createNestedObject();
          newEntry["code"] = receivedCode;
          FileSystemSPIFFS.JSON_MEMORY["LearningEnabled"] = false;
          FileSystemSPIFFS.saveJsonMemoryToFile();
        
    
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
