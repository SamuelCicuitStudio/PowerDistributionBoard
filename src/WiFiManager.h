#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "Device.h"
#include "esp_wifi.h"

class Device;  // Forward declaration

// ─────────────────────────────────────────────────────────────
// WiFiManager – Manages Access Point, Web Server, and Sessions
// ─────────────────────────────────────────────────────────────
class WiFiManager {
public:
    // ───────────── Constructor ─────────────
   WiFiManager(Device* dev);  // Inject Device dependency

    // ───────────── Public API ─────────────
    void begin();                          // 🔧 Initialize and start Wi-Fi manager
    void restartWiFiAP();                  // 🔄 Disable + re-enable full Wi-Fi stack
    void StartWifiAP();                    // 📡 Start Access Point + register web routes
    void disableWiFiAP();                  // 📴 Fully disable Wi-Fi + cleanup

    void resetTimer();                     // 🔁 Reset inactivity timer on user activity
    void startInactivityTimer();           // ⏱ Launch background inactivity RTOS task
    void heartbeat();                      // ❤️ Handle client heartbeat logic

    void onUserConnected();                // 👤 Mark user session as active
    void onAdminConnected();               // 👤 Mark admin session as active
    void onDisconnected();                 // 🚪 Invalidate user/admin session

    bool isUserConnected() const;          // 🔐 Check if user is authenticated
    bool isAdminConnected() const;         // 🔐 Check if admin is authenticated
    bool isAuthenticated(AsyncWebServerRequest* request);  // 🔐 Validate request session

    // ───────────── Server Routing ─────────────
    void handleRoot(AsyncWebServerRequest* request);  // 🌐 Serve root HTML

    // ───────────── Web Server Components ─────────────
    AsyncWebServer server;                 // 🌍 Main HTTP server (AP mode)

    // ───────────── Inactivity / Heartbeat ─────────────
    static void inactivityTask(void* param);   // ⏳ RTOS task to detect idle timeout
    TaskHandle_t inactivityTaskHandle = nullptr;
    TaskHandle_t heartbeatTaskHandle = nullptr;
    unsigned long lastActivityMillis = 0;      // ⏱ Timestamp of last user action

    // ───────────── State Flags ─────────────
    bool keepAlive = false;               // 📶 True if /heartbeat is active
    bool WifiState = false;               // 📶 Current AP state
    bool prev_WifiState = false;          // 📶 Previous AP state

    // ───────────── Link to Main Device ─────────────
    Device* dev;                          // 🔗 Pointer to Device for callbacks
};

#endif // WIFI_MANAGER_H
