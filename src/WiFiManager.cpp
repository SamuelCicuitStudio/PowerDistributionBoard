
#include "WiFiManager.h"

/************************************************************************************************/
/*                           WIFI Manager class definition                                     */
/************************************************************************************************/

// Constructor for WiFiManager
/**
 * @brief Constructs a WiFiManager object with provided managers.
 *
 * @param Config Pointer to the ConfigManager for configuration management.
 */
WiFiManager::WiFiManager(ConfigManager* Config,SleepTimer* Sleep,PowerManager* Pow )
    : Config(Config),Sleep(Sleep), server(80),Pow(Pow){}

/**
 * @brief Initializes the WiFi Manager, setting up the necessary configurations and modes.
 *
 * This method checks the current mode (Access Point or WiFi), attempts to connect or start an AP, 
 * and handles user input for configuration.
 */
void WiFiManager::begin() {
    if (DEBUGMODE){
        Serial.println("###########################################################");
        Serial.println("#                 Starting WIFI Manager                   #");
        Serial.println("###########################################################");
    }
    ReassignFlag=false;
    keepAlive = false;
    WifiState = true;
    prev_WifiState = false;
    
    startAccessPoint();
}

/************************************************************************************************/
/*                                        SET AP CREDENTIAL                                     */
/************************************************************************************************/

/**
 * @brief Sets the Access Point (AP) credentials.
 *
 * @param ssid The SSID for the AP.
 * @param password The password for the AP.
 */
void WiFiManager::setAPCredentials(const char* ssid, const char* password) {
    Config->PutString(DEVICE_WIFI_HOTSPOT_NAME_KEY,ssid);
    Config->PutString(DEVICE_AP_AUTH_PASS_KEY,password);

    if (DEBUGMODE) {
        Serial.print("\n WiFiManager: AP credentials set - SSID: ");
        Serial.print(ssid);
        Serial.print(", Password: ");
        Serial.println(password);
    }
}


/************************************************************************************************/
/*                                         START AP MODE                                        */
/************************************************************************************************/
/**
 * @brief Starts the Access Point mode for the WiFiManager.
 *
 * This function initializes the ESP32 as a WiFi Access Point, configures
 * its settings, and sets up the necessary routes for handling incoming
 * HTTP requests. It handles saving and retrieving WiFi credentials,
 * toggling relays, and managing sensor configurations.
 *
 * @note Requires DEBUGMODE to be enabled for verbose output.
 * 
 * @return void
 */
void WiFiManager::startAccessPoint() {
    
    if (DEBUGMODE) {
        Serial.println("WiFiManager: Starting Access Point ✅");
    };

    // Start the Access Point
    if (!WiFi.softAP(Config->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY,DEVICE_WIFI_HOTSPOT_NAME).c_str(), Config->GetString(DEVICE_AP_AUTH_PASS_KEY,DEVICE_AP_AUTH_PASS_DEFAULT).c_str())) {
        if (DEBUGMODE)Serial.println("Failed to start AP ❌");
        
        return;
    };

    // Configure the AP with static IP settings
    if (!WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET)) {
        if (DEBUGMODE)Serial.println("Failed to set AP config ❌");
        return;
    };

    if (DEBUGMODE) {
        Serial.print("WiFiManager: AP Started - IP Address: ");
        Serial.println(WiFi.softAPIP());
    };
    Sleep->reset();


    server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) { handleSettings(request); });
    server.on("/thankyou", HTTP_GET, [this](AsyncWebServerRequest* request) { handThanks(request); });
    server.on("/saveWiFi", HTTP_POST, [this](AsyncWebServerRequest* request) { handleSaveWiFi(request); });
    server.on("/wifiCredentialsPage", HTTP_GET, [this](AsyncWebServerRequest* request) { handleSetWiFi(request); });
    server.on("/settings", HTTP_GET, [this](AsyncWebServerRequest* request) {handleSettings(request);});
        
    // Example for battery status
    server.on("/getBatteryStatus", HTTP_GET, [this](AsyncWebServerRequest *request){
        // Example ADC reading for battery
        int batteryPercentage = Pow->getVoltage();;
        String jsonResponse = "{\"battery\": " + String(batteryPercentage) + "}";
        request->send(200, "application/json", jsonResponse);
    });

    // Example for capacitor charge
    server.on("/getCapacitorCharge", HTTP_GET, [this](AsyncWebServerRequest *request){
        int chargePercentage = Pow->getVoltage();
        String jsonResponse = "{\"charge\": " + String(chargePercentage) + "}";
        request->send(200, "application/json", jsonResponse);
    });

    // Example for timing update
    server.on("/setTiming", HTTP_POST, [this](AsyncWebServerRequest *request){
        String onTimeStr = request->arg("onTime");
        String offTimeStr = request->arg("offTime");

        // Convert the string arguments to long (or int) type
        uint32_t onTime = onTimeStr.toInt();
        uint32_t offTime = offTimeStr.toInt();
        // Call the function to set the timing
        Pow->setCycleTime(onTime, offTime);
        // Respond with a confirmation
        request->send(200, "application/json", "{\"status\": \"Timing updated\"}");
    });

    // LED Toggle Example
    server.on("/toggleLED", HTTP_POST, [this](AsyncWebServerRequest *request){
        // Extract the LED state from the request
        String ledState = request->arg("ledFeedback");

        // Assuming 'ledFeedback' is sent as "true" or "false"
        bool ledOn = (ledState == "true");

        // Set the LED feedback state (example function call)
        Pow->setLedFeedback(ledOn); 

        // Respond with a JSON confirmation
        request->send(200, "application/json", "{\"status\": \"LED state toggled\"}");
    });

    // Define the route to handle the system power toggle
    server.on("/togglePower", HTTP_GET, [this](AsyncWebServerRequest *request){
        float chargePercentage = Pow->getVoltage();;
        if (chargePercentage >= CHARGE_THRESHOLD_PERCENT) {
        Pow->systemOnwifi = !Pow->systemOnwifi;
        String statusText =  Pow->systemOnwifi ? "ON" : "OFF";
        Serial.println("System is " + statusText);
        
        // Respond with JSON containing the updated status
        String jsonResponse = "{ \"status\": \"" + statusText + "\" }";
        request->send(200, "application/json", jsonResponse);
        } else {
        // Respond with a message indicating insufficient battery
        request->send(200, "application/json", "{ \"status\": \"Battery charge must be above 85% to turn ON the system.\" }");
        }
    });

      // Return AC status
    server.on("/getACStatus", HTTP_GET, [this](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(128);
        doc["acStatus"] = digitalRead(DETECT_12V_PIN) ? "Connected" : "Disconnected";
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });


    server.on("/favicon.ico", HTTP_GET, [this](AsyncWebServerRequest *request){
        keepAlive = true; // set keep wifi alive true
        request->send(204); // No Content
    });

        // Return the four DS18B20 temperatures as JSON
    server.on("/getTemperatures", HTTP_GET, [this](AsyncWebServerRequest *request){
        const float* temps = Pow->getTemperatureArray();    // pointer to Temps[4]
        DynamicJsonDocument doc(256);
        JsonArray arr = doc.createNestedArray("temperatures");
        for (int i = 0; i < 4; ++i) {
            arr.add(temps[i]);
        }
        String payload;
        serializeJson(doc, payload);
        request->send(200, "application/json", payload);
    });

    server.serveStatic("/", SPIFFS, "/");  // Serve all files from SPIFFS root
/*************************************************************************************** */
    // Serve static files like icons, CSS, JS, etc.
    server.serveStatic("/icons/", SPIFFS, "/icons/").setCacheControl("max-age=86400");
    server.begin();
}

/************************************************************************************************/
/*                                        HANDLE SETTINGS                                          */
/************************************************************************************************/
void WiFiManager::handleSettings(AsyncWebServerRequest* request) {
    if (DEBUGMODE) {
        Serial.println("WiFiManager: Handling Settings request ✅");
    };
    keepAlive = true; // set keep wifi alive true

    request->send(SPIFFS, "/settings.html", "text/html");
}

/************************************************************************************************/
/*                                     HANDLE ROOT                                              */
/************************************************************************************************/

void WiFiManager::handleRoot(AsyncWebServerRequest* request) {
    if (DEBUGMODE) {
        Serial.println("WiFiManager: Handling welcome root request ✅");
    };
    keepAlive = true; // set keep wifi alive true
    Sleep->reset();
    esp_task_wdt_reset();
    request->send(SPIFFS, "/welcome.html", "text/html");
}
/************************************************************************************************/
/*                                     HANDLE THANKS                                              */
/************************************************************************************************/
void WiFiManager::handThanks(AsyncWebServerRequest* request) {
    if (DEBUGMODE) {
        Serial.println("WiFiManager: Handling thanks  request ✅");
    };
    keepAlive = true; // set keep wifi alive true
    Sleep->reset();
    esp_task_wdt_reset();
    request->send(SPIFFS, "/thankyou.html", "text/html");
    delay(500);
    
}
/************************************************************************************************/
/*                                         HANDLE SET WIFI                                      */
/************************************************************************************************/

void WiFiManager::handleSetWiFi(AsyncWebServerRequest* request) {
    if (DEBUGMODE) {
        Serial.println("WiFiManager: Handling set wifi request ✅");
    };
    keepAlive = true; // set keep wifi alive true
    Sleep->reset();
    esp_task_wdt_reset();
    request->send(SPIFFS, "/wifiCredentialsPage.html", "text/html");
}

/************************************************************************************************/
/*                                     HANDLE SAVE WIFI  HOTSPOT SETTINGS                       */
/************************************************************************************************/
void WiFiManager::handleSaveWiFi(AsyncWebServerRequest* request) {
    esp_task_wdt_reset();
    Sleep->reset();
    if (DEBUGMODE) {
        Serial.println("WiFiManager: Handling save WiFi request ✅");
    };
    keepAlive = true; // set keep wifi alive true
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
        String ssid = request->getParam("ssid", true)->value();
        String password = request->getParam("password", true)->value();
        
        esp_task_wdt_reset();
        if (DEBUGMODE) {
            Serial.print("WiFiManager: Received credentials - SSID: ");
            Serial.print(ssid);
            Serial.print(", Password: ");
            Serial.println(password);
        }
        esp_task_wdt_reset();
        if (ssid != "" && password != "") {
            Config->RemoveKey(DEVICE_WIFI_HOTSPOT_NAME_KEY);
            Config->RemoveKey(DEVICE_AP_AUTH_PASS_KEY);
            delay(200);
            Config->PutString(DEVICE_WIFI_HOTSPOT_NAME_KEY, ssid);
            Config->PutString(DEVICE_AP_AUTH_PASS_KEY, password);
            request->send(SPIFFS, "/thankyou.html", "text/html");
            esp_task_wdt_reset();
            Config->RestartSysDelay(5000);
        } else {
            request->send(400, "text/plain", "Invalid SSID or Password.");
        }
    } else {
        request->send(400, "text/plain", "Missing parameters.");
    }
}

/************************************************************************************************/
/*                                     HANDLE FILE SAVE UPLOAD                       */
/************************************************************************************************/

void handleFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File uploadFile;

    if (!index) {
        // Check if the file exists already and remove it to overwrite
        if (SPIFFS.exists(SLAVE_CONFIG_PATH)) {
            SPIFFS.remove(SLAVE_CONFIG_PATH);  // Remove the existing file
            if (DEBUGMODE)Serial.println("Existing file removed to overwrite.✅");
        }

        // Starting the upload
        if (DEBUGMODE)Serial.printf("Upload Start: %s ✅\n", filename.c_str());
        uploadFile = SPIFFS.open(SLAVE_CONFIG_PATH, FILE_WRITE);
        if (!uploadFile) {
            Serial.println("Failed to open file for writing❌");
            request->send(500, "text/plain", "Failed to open file");
            return;
        }
    }

    if (len) {
        // Write the received data to the file
        uploadFile.write(data, len);
        if (DEBUGMODE)Serial.printf("Received %d bytes ✅\n", len);
    }

    if (final) {
        // Finalize the upload
        uploadFile.close();
        if (DEBUGMODE)Serial.printf("Upload End: %s (%d bytes)✅\n", filename.c_str(), index + len);
        request->send(200, "text/plain", "File received successfully");
    }
}

void WiFiManager::disableWiFiAP() {
    Sleep->reset();
    if (DEBUGMODE)Serial.println("Disabling WiFi Access Point...✅");
    WifiState = false;
    prev_WifiState = true;
    WiFi.softAPdisconnect(true);  // Turn off the hotspot (Access Point mode)
    WiFi.mode(WIFI_OFF);          // Completely disable WiFi
    Serial.println("WiFi Access Point disabled.✅");
}