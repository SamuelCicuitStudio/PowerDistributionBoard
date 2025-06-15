#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "ConfigManager.h"


/************************************************************************************************/
/*                           WIFI Manager class definition                                     */
/************************************************************************************************/
class WiFiManager {
public:
    WiFiManager(WiFiClass* WFi,ConfigManager* Config);
    void begin();
    bool keepAlive;
    bool WifiState,prev_WifiState;
    void disableWiFiAP();
    void StartWifiAP();
private:

    void handleRoot(AsyncWebServerRequest* request);
    AsyncWebServer server;
    WiFiClass* WFi;
    ConfigManager* Config;
};
#endif // WIFI_MANAGER_H