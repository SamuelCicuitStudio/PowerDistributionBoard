#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include "ConfigManager.h"
#include "SleepTimer.h"
#include "PowerManager.h"




/************************************************************************************************/
/*                           WIFI Manager class definition                                     */
/************************************************************************************************/
void handleFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
class WiFiManager {
public:
     WiFiManager(ConfigManager* Config,SleepTimer* Sleep ,PowerManager* Pow);

    void begin();
    void setAPCredentials(const char* ssid, const char* password);
    bool ReassignFlag;
    bool keepAlive;
    bool WifiState,prev_WifiState;
    void disableWiFiAP();
private:
    void startAccessPoint();
    void handleRoot(AsyncWebServerRequest* request);
    void handThanks(AsyncWebServerRequest* request);
    void handleSettings(AsyncWebServerRequest* request);

    void handleSetWiFi(AsyncWebServerRequest* request);
    void handleSaveWiFi(AsyncWebServerRequest* request);
 
    ConfigManager* Config;

    AsyncWebServer server;
    PowerManager* Pow;
    SleepTimer* Sleep ;
};
#endif // WIFI_MANAGER_H