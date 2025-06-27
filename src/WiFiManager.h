#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "Device.h"
#include "esp_wifi.h"

class Device;  // Forward declaration to resolve circular dependency

// ─────────────────────────────────────────────────────────────
//                       WiFiManager Class
// ─────────────────────────────────────────────────────────────
// Handles Access Point (SoftAP) setup, Async Web Server,
// heartbeat tracking, session management, and inactivity timeout
// ─────────────────────────────────────────────────────────────

class WiFiManager {
public:
    // ─────────────────────── Constructor ───────────────────────
    // Accepts a pointer to the main Device controller
    explicit WiFiManager(Device* dev);

    // ───────────── Static Singleton Instance ─────────────
    // Globally accessible instance pointer
    static WiFiManager* instance;

    // ───────────── Public API ─────────────

    void begin();               // 🔧 Initialize Wi-Fi and start services
    void restartWiFiAP();       // 🔄 Restart Access Point and web server
    void StartWifiAP();         // 📡 Start SoftAP and register routes
    void disableWiFiAP();       // 📴 Turn off Wi-Fi and cleanup

    void resetTimer();          // 🔁 Reset inactivity timer
    void startInactivityTimer();// ⏱ Launch RTOS task to monitor idle time
    void heartbeat();           // ❤️ Called when client sends heartbeat ping

    void onUserConnected();     // 👤 Mark user as connected
    void onAdminConnected();    // 👤 Mark admin as connected
    void onDisconnected();      // 🚪 Invalidate all sessions

    bool isUserConnected() const;                     // 🔐 Check user session
    bool isAdminConnected() const;                    // 🔐 Check admin session
    bool isAuthenticated(AsyncWebServerRequest* request);  // 🔐 Check if request has valid session

    // ───────────── HTTP Routing ─────────────
    void handleRoot(AsyncWebServerRequest* request);  // 🌐 Handle "/" GET request

    // ───────────── Web Server ─────────────
    AsyncWebServer server;       // 🌍 Async HTTP server running on port 80

    // ───────────── RTOS Timing Tasks ─────────────
    static void inactivityTask(void* param);   // ⏳ Idle timeout RTOS task
    TaskHandle_t inactivityTaskHandle = nullptr;
    TaskHandle_t heartbeatTaskHandle = nullptr;
    unsigned long lastActivityMillis = 0;      // 🕒 Last known activity timestamp

    // ───────────── State Flags ─────────────
    bool keepAlive = false;         // 📶 Heartbeat active
    bool WifiState = false;         // 📡 Current Wi-Fi state
    bool prev_WifiState = false;    // 📡 Previous Wi-Fi state

    // ───────────── Link to Device ─────────────
    Device* dev;                    // 🔗 Pointer to main system controller
};

#endif // WIFI_MANAGER_H
