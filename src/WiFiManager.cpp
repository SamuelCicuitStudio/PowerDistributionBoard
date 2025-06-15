#include "WiFiManager.h"

WiFiManager::WiFiManager(WiFiClass* WFi, ConfigManager* Config)
    : server(80), WFi(WFi), Config(Config) {}

void WiFiManager::begin() {
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting WIFI Manager                   #");
    DEBUG_PRINTLN("###########################################################");

    if (!SPIFFS.begin(true)) {
        DEBUG_PRINTLN("❌ SPIFFS initialization failed!");
        return;  // SPIFFS initialization failed
    }
    StartWifiAP();
}

void WiFiManager::StartWifiAP() {
    keepAlive = false;
    WifiState = true;
    prev_WifiState = false;

    DEBUG_PRINTLN("Starting Access Point ✅");

    String ssid = Config->GetString(DEVICE_WIFI_HOTSPOT_NAME_KEY, DEVICE_WIFI_HOTSPOT_NAME);
    String password = Config->GetString(DEVICE_AP_AUTH_PASS_KEY, DEVICE_AP_AUTH_PASS_DEFAULT);

    // Start the Access Point
    if (!WiFi.softAP(ssid.c_str(), password.c_str())) {
        DEBUG_PRINTLN(" Failed to start AP❌");
        return;
    }

    DEBUG_PRINT("✅ AP Started: ");
    DEBUG_PRINTLN(ssid);

    // Configure the AP with static IP settings
    if (!WFi->softAPConfig(LOCAL_IP, GATEWAY, SUBNET)) {
        DEBUG_PRINTLN("Failed to set AP config ❌");
        return;
    }

    if (DEBUGMODE) {
        Serial.print("AP Started - IP Address: ");
        Serial.println(WFi->softAPIP());
    }

    // Route: main root
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleRoot(request);
    });

    // Route: favicon ping
    server.on("/favicon.ico", HTTP_GET, [this](AsyncWebServerRequest* request) {
        keepAlive = true;          // keep WiFi alive
        request->send(204);        // No Content
    });

    // Serve all static files from SPIFFS root
    server.serveStatic("/", SPIFFS, "/");

    // Serve static assets with caching
    server.serveStatic("/icons/", SPIFFS, "/icons/").setCacheControl("max-age=86400");

    server.begin();
}

void WiFiManager::handleRoot(AsyncWebServerRequest* request) {
    DEBUG_PRINTLN("Handling welcome root request ✅");

    keepAlive = true;  // set keep wifi alive
    request->send(SPIFFS, "/index.html", "text/html");
}

void WiFiManager::disableWiFiAP() {
    DEBUG_PRINTLN("Disabling WiFi Access Point...✅");

    WifiState = false;
    prev_WifiState = true;

    WFi->softAPdisconnect(true);  // Disable AP mode
    WFi->mode(WIFI_OFF);          // Fully disable WiFi

    Serial.println("WiFi Access Point disabled.✅");
}
