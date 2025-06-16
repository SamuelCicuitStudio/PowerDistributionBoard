#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "ConfigManager.h"

class WiFiManager {
public:
    WiFiManager(WiFiClass* WFi, ConfigManager* Config);           // Constructor with dependencies
    void begin();                                                 // Initialize and start Wi-Fi manager
    void disableWiFiAP();                                         // Disable the Access Point
    void StartWifiAP();                                           // Start the Access Point and web server
    void resetTimer();                                            // 🔄 Call this when user activity is detected

    bool keepAlive;                                               // Flag set by ping (favicon request)
    bool WifiState, prev_WifiState;                               // Current and previous Wi-Fi states

private:
    void handleRoot(AsyncWebServerRequest* request);              // Serve index.html at root

    AsyncWebServer server;                                        // HTTP server on port 80
    WiFiClass* WFi;                                               // Pointer to global WiFi instance
    ConfigManager* Config;                                        // Pointer to configuration system

    static void inactivityTask(void* param);                      // RTOS task to auto-disable Wi-Fi
    TaskHandle_t inactivityTaskHandle = nullptr;                  // Handle to inactivity task
    unsigned long lastActivityMillis = 0;                         // Timestamp of last activity
    void startInactivityTimer();                                  // Create the inactivity timer task
};

#endif // WIFI_MANAGER_H
