#include "WiFiManager.h"

// ─────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────
WiFiManager::WiFiManager(WiFiClass* WFi, ConfigManager* Config)
    : server(80), WFi(WFi), Config(Config) {}


// ─────────────────────────────────────────────────────────────
// Begin WiFi Manager
// ─────────────────────────────────────────────────────────────
void WiFiManager::begin() {
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting WIFI Manager 🌐               #");
    DEBUG_PRINTLN("###########################################################");

    if (!SPIFFS.begin(true)) {
        DEBUG_PRINTLN("❌ SPIFFS initialization failed!");
        return;
    }

    StartWifiAP();
}


// ─────────────────────────────────────────────────────────────
// Start Access Point
// ─────────────────────────────────────────────────────────────
void WiFiManager::StartWifiAP() {
    keepAlive = false;
    WifiState = true;
    prev_WifiState = false;

    DEBUG_PRINTLN("[WiFiManager] Starting Access Point ✅");

    String ssid     = Config->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY, DEVICE_WIFI_HOTSPOT_NAME);
    String password = Config->GetString(DEVICE_AP_AUTH_PASS_KEY, DEVICE_AP_AUTH_PASS_DEFAULT);

    if (!WiFi.softAP(ssid.c_str(), password.c_str())) {
        DEBUG_PRINTLN("[WiFiManager] Failed to start AP ❌");
        return;
    }

    DEBUG_PRINTF("✅ AP Started: %s\n", ssid.c_str());

    if (!WFi->softAPConfig(LOCAL_IP, GATEWAY, SUBNET)) {
        DEBUG_PRINTLN("[WiFiManager] Failed to set AP config ❌");
        return;
    }

    if (DEBUGMODE) {
        Serial.print("[WiFiManager] AP IP Address: ");
        Serial.println(WFi->softAPIP());
    }

    // ── Web routes ───────────────────────────────────────────
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleRoot(request);
    });

    server.on("/favicon.ico", HTTP_GET, [this](AsyncWebServerRequest* request) {
        keepAlive = true;
        request->send(204);  // No Content
    });

    server.serveStatic("/", SPIFFS, "/");
    server.serveStatic("/icons/", SPIFFS, "/icons/").setCacheControl("max-age=86400");

    server.begin();

    // Start auto-disable timer
    startInactivityTimer();
}


// ─────────────────────────────────────────────────────────────
// Handle Root Request
// ─────────────────────────────────────────────────────────────
void WiFiManager::handleRoot(AsyncWebServerRequest* request) {
    DEBUG_PRINTLN("[WiFiManager] Handling root request 🌐");
    keepAlive = true;
    request->send(SPIFFS, "/index.html", "text/html");
}


// ─────────────────────────────────────────────────────────────
// Disable Access Point
// ─────────────────────────────────────────────────────────────
void WiFiManager::disableWiFiAP() {
    DEBUG_PRINTLN("[WiFiManager] Disabling WiFi Access Point...");

    WifiState = false;
    prev_WifiState = true;

    WFi->softAPdisconnect(true);
    WFi->mode(WIFI_OFF);

    if (inactivityTaskHandle != nullptr) {
        vTaskDelete(inactivityTaskHandle);
        inactivityTaskHandle = nullptr;
        DEBUG_PRINTLN("[WiFiManager] Inactivity timer stopped 🛑");
    }

    DEBUG_PRINTLN("[WiFiManager] WiFi Access Point disabled ❌");
}


// ─────────────────────────────────────────────────────────────
// Reset Inactivity Timer
// ─────────────────────────────────────────────────────────────
void WiFiManager::resetTimer() {
    lastActivityMillis = millis();
}


// ─────────────────────────────────────────────────────────────
// RTOS Task: WiFi Inactivity Monitor
// ─────────────────────────────────────────────────────────────
void WiFiManager::inactivityTask(void* param) {
    WiFiManager* self = static_cast<WiFiManager*>(param);
    while (true) {
        if (self->WifiState) {
            unsigned long now = millis();
            if ((now - self->lastActivityMillis) > INACTIVITY_TIMEOUT_MS) {
                DEBUG_PRINTLN("[WiFiManager] Inactivity timeout reached ⏳");
                self->disableWiFiAP();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}


// ─────────────────────────────────────────────────────────────
// Start Inactivity Timer Task
// ─────────────────────────────────────────────────────────────
void WiFiManager::startInactivityTimer() {
    resetTimer();
    if (inactivityTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(
            WiFiManager::inactivityTask,
            "WiFiInactivity",
            2048,
            this,
            1,
            &inactivityTaskHandle,
            APP_CPU_NUM
        );
        DEBUG_PRINTLN("[WiFiManager] Inactivity timer started ⏱️");
    }
}
