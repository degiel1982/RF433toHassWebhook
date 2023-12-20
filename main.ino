#include <Arduino.h>
#include <esp_crt_bundle.h>
#include <ssl_client.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <SPIFFS.h>
#include <RCSwitch.h>
#include <ArduinoJson.h>

#define debug false

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

RCSwitch mySwitch = RCSwitch();

const char* ssid = "";
const char* password = "";
bool enableLearn = false;
String usedWebhook;
DynamicJsonDocument jsonDoc(1024); // Adjust the size as per your JSON structure
const char* rfcodesPath = "/rfcodes.json"; // Add this line
File rfcodes;
const char* homeAssistantIP = "";
const int homeAssistantPort = 443;

void handleWebSocketMessage(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

void setup() {
    #if debug
    Serial.begin(115200);
    #endif
    
    mySwitch.enableReceive(32);

    if (!SPIFFS.begin(true)) {
        #if debug
          Serial.println("An error occurred while mounting SPIFFS");
        #endif
       
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
      #if debug
        Serial.printf("WiFi Failed!\n");
      #endif
        return;
    }

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    ws.onEvent(handleWebSocketMessage);
    server.addHandler(&ws);
    server.begin();
}

void handleWebSocketMessage(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      #if debug
        Serial.println("WebSocket client connected");
        #endif
    } else if (type == WS_EVT_DISCONNECT) {
      #if debug
        Serial.println("WebSocket client disconnected");
        #endif
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            String message = (char *)data;
            #if debug
            Serial.printf("WebSocket message received: %s\n", message.c_str());
            #endif

            // Parse JSON
            DynamicJsonDocument jsonDoc(256); // Adjust the size as per your JSON structure
            DeserializationError error = deserializeJson(jsonDoc, message);
            
            if (error) {
              #if debug
                Serial.print(F("JSON parsing failed! Error: "));
                Serial.println(error.c_str());
                #endif
                return;
            }

            // Check for the "action" field in the JSON
            const char *action = jsonDoc["action"];
            const char *webhookName = jsonDoc["webhookName"];
            // Perform action based on the received JSON data
            if (strcmp(action, "stoplearn") == 0) {
              enableLearn = false;
              ws.textAll("Learning Mode Disabled");
            }
            if (strcmp(action, "learn") == 0) {
              if(enableLearn){
                ws.textAll("It is already in learning mode with webhook: "+ String(usedWebhook));
              }else{
              enableLearn = true;
              usedWebhook = String(webhookName);
              ws.textAll("Learning Mode Enabled");
              }
            }
        }
      }
}
#if debug
void printJsonFile() {
  File file = SPIFFS.open(rfcodesPath, "r");
  if (!file) {
    Serial.println("Failed to open rfcodes file for reading");
    return;
  }

  Serial.println("Printing JSON file contents:");
  while (file.available()) {
    Serial.write(file.read());
  }
  Serial.println();

  file.close();
}
#endif

void saveConfig() {
    // Load existing JSON content if the file exists
    rfcodes = SPIFFS.open(rfcodesPath, "r");
    DynamicJsonDocument jsonDoc(1024);

    if (rfcodes) {
        DeserializationError error = deserializeJson(jsonDoc, rfcodes);
        rfcodes.close();

        if (error) {
          #if debug
            Serial.print(F("Failed to read existing JSON! Error: "));
            Serial.println(error.c_str());
            #endif
            return;
        }
    }

    // Check if the code already exists
    unsigned long newCode = mySwitch.getReceivedValue();
    JsonArray rfcodesArray = jsonDoc["rfcodes"].as<JsonArray>();
    for (JsonObject entry : rfcodesArray) {
        if (entry["code"].as<unsigned long>() == newCode) {
            // Code already exists, you can choose to update the webhook or reject the new learning attempt
            ws.textAll(F("Code already exists. Learning stopped"));
            return;
        }
    }

    // Add the new entry
    JsonObject newEntry = rfcodesArray.createNestedObject();
    newEntry["webhook"] = usedWebhook;
    newEntry["code"] = newCode;

    // Save the updated JSON content to the file
    rfcodes = SPIFFS.open(rfcodesPath, "w");
    if (!rfcodes) {
      #if debug
        Serial.println("Failed to open rfcodes file for writing");
        #endif
        return;
    }

    serializeJson(jsonDoc, rfcodes);
    rfcodes.close();
#if debug
    printJsonFile();
    #endif
}


void loadConfig() {
  File file = SPIFFS.open(rfcodesPath, "r");
  if (!file) {
    #if debug
    Serial.println("Failed to open rfcodes file for reading");
    #endif
    return;
  }

  DeserializationError error = deserializeJson(jsonDoc, file);
  file.close();
#if debug
  if (error) {
    Serial.print(F("JSON parsing failed! Error: "));
    Serial.println(error.c_str());
  } else {
    Serial.println("JSON configuration loaded successfully");
    printJsonFile();
  }
  #endif
}

void sendWebhookToHomeAssistant() {
    File file = SPIFFS.open(rfcodesPath, "r");
    if (!file) {
      #if debug
        Serial.println("Failed to open rfcodes file for reading");
        #endif
        return;
    }

    DynamicJsonDocument jsonDoc(1024);
    DeserializationError error = deserializeJson(jsonDoc, file);
    file.close();
#if debug
    if (error) {
        Serial.print(F("JSON parsing failed! Error: "));
        Serial.println(error.c_str());
        return;
    }
    #endif

    JsonArray rfcodesArray = jsonDoc["rfcodes"].as<JsonArray>();

    WiFiClientSecure client;
    client.setInsecure();  // Disable SSL certificate validation

    for (JsonObject entry : rfcodesArray) {
        if (entry["code"].as<unsigned long>() == mySwitch.getReceivedValue()) {
            String webhookName = entry["webhook"].as<String>();
            ws.textAll("Match Found send: " + webhookName);

            String url = "/api/webhook/" + webhookName;

            if (client.connect(homeAssistantIP, homeAssistantPort)) {
                // Make a POST request
                client.print("POST " + url + " HTTP/1.1\r\n");
                client.print("Host: " + String(homeAssistantIP) + "\r\n");
                client.print("Connection: close\r\n");
                client.print("Content-Length: 0\r\n\r\n");

                // Read and print the server's response
                while (client.connected()) {
                    String line = client.readStringUntil('\n');
                    #if debug
                    Serial.println(line);
                    #endif
                }

                client.stop();
            } else {
              #if debug
                Serial.println("Connection failed");
                #endif
            }

            return;
        }
    }
}

void loop() {
    if (mySwitch.available()) {
        if (enableLearn) {
            loadConfig();
            saveConfig();
            enableLearn = false;
            // Call the function to send the webhook
            delay(3000);
        } else {
            sendWebhookToHomeAssistant();
        }

        mySwitch.resetAvailable();
    }
}
