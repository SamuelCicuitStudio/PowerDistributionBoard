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
    // Constructor
    WiFiManager(Device* dev);

    // ───── Public Interface ─────
    void begin();                                         // 🔧 Initialize and start Wi-Fi manager
    void disableWiFiAP();                                 // 📴 Disable the Access Point
    void StartWifiAP();                                   // 📡 Start the Access Point and web server
    void resetTimer();                                    // 🔄 Call this when user activity is detected
    void onUserConnected();                               // ✅ Mark user as connected
    void onAdminConnected();                              // ✅ Mark admin as connected
    void onDisconnected();                                // ❌ Handle disconnection
    bool isUserConnected() const;                         // 🔐 Check if a user is connected
    bool isAdminConnected() const;                        // 🔐 Check if admin is connected
    bool isAuthenticated(AsyncWebServerRequest* request); // 🔐 Validate session access
    void heartbeat();                                     // ⏱ Monitor client keep-alive every 3s

    // ───── Status Flags ─────
    bool keepAlive = false;                               // 📶 Updated by /heartbeat ping
    bool WifiState = false;                               // 📶 Current Wi-Fi connection state
    bool prev_WifiState = false;                          // 📶 Previous Wi-Fi connection state


    // ───── Internal Handlers ─────
    void handleRoot(AsyncWebServerRequest* request);      // 🌐 Serve index.html on root path

    // ───── Wi-Fi Components ─────
    AsyncWebServer server;                                // 🌐 HTTP server instance

    // ───── Inactivity Timeout ─────
    static void inactivityTask(void* param);              // ⏱ RTOS task to monitor inactivity
    TaskHandle_t inactivityTaskHandle = nullptr;          // ⏱ RTOS task handle
    unsigned long lastActivityMillis = 0;                 // ⏱ Last activity timestamp
    void startInactivityTimer();                          // ⏱ Start monitoring inactivity

    // ───── Link to Device Core ─────
    Device* dev;                                          // 🔗 Pointer to main device manager
};

#endif // WIFI_MANAGER_H
