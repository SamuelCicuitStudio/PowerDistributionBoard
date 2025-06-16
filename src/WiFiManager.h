#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "Device.h"

class Device;



class WiFiManager {
public:
    explicit WiFiManager(WiFiClass* WFi, Device* dev)
    : WFi(WFi), dev(dev), server(80) {}

    void begin();                                         // Initialize and start Wi-Fi manager
    void disableWiFiAP();                                 // Disable the Access Point
    void StartWifiAP();                                   // Start the Access Point and web server
    void resetTimer();                                    // 🔄 Call this when user activity is detected
    void onUserConnected();                               // ✅ Mark user as connected
    void onAdminConnected();                              // ✅ Mark admin as connected
    void onDisconnected();                                // ✅ Handle disconnection
    bool isUserConnected() const;                         // ✅ Check if any user is connected
    bool isAdminConnected() const;                        // ✅ Check if admin is connected

    bool keepAlive;                                       // Set by ping (favicon or keep-alive req)
    bool WifiState;                                       // Current Wi-Fi state
    bool prev_WifiState;                                  // Previous Wi-Fi state

private:
    void handleRoot(AsyncWebServerRequest* request);      // Serve index.html at root

    AsyncWebServer server;                                // HTTP server instance
    WiFiClass* WFi;                                       // Pointer to global WiFi instance


    static void inactivityTask(void* param);              // RTOS task for inactivity timeout
    TaskHandle_t inactivityTaskHandle = nullptr;          // Handle to inactivity task
    unsigned long lastActivityMillis = 0;                 // Last time of activity
    void startInactivityTimer();                          // Start the inactivity timer task

    Device* dev          ;                             // Pointer to main device manager
};

#endif // WIFI_MANAGER_H
