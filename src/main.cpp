#include <Arduino.h>
#include "config.h"

// ──────────────────────────────────────────────────────────────
//                      Module Headers
// ──────────────────────────────────────────────────────────────
#include "ConfigManager.h"
#include "WiFiManager.h"
#include "SwitchManager.h"
#include "Device.h"  // ✅ Include Device Manager

// ──────────────────────────────────────────────────────────────
//                      Global Instances
// ──────────────────────────────────────────────────────────────
Preferences     prefs;  // NVS storage for configuration

ConfigManager*    config        = nullptr;
WiFiManager*      wifi          = nullptr;
Indicator*        indicator     = nullptr;
HeaterManager*    heater        = nullptr;
CpDischg*         discharger    = nullptr;
FanManager*       fan           = nullptr;
CurrentSensor*    currentSensor = nullptr;
TempSensor*       tempSensor    = nullptr;
Relay*            mainRelay     = nullptr;
BypassMosfet*     bypassFET     = nullptr;
Device*           device        = nullptr;
SwitchManager*    sw            = nullptr;

// ──────────────────────────────────────────────────────────────
//                           Setup
// ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    // Initialize NVS storage
    prefs.begin(CONFIG_PARTITION, false);

    // Instantiate all modules
    config        = new ConfigManager(&prefs);
    wifi          = new WiFiManager(&WiFi, config);
    sw            = new SwitchManager(config,wifi);
    indicator     = new Indicator();
    heater        = new HeaterManager();
    discharger    = new CpDischg();
    fan           = new FanManager();
    currentSensor = new CurrentSensor();
    tempSensor    = new TempSensor();
    mainRelay     = new Relay();
    bypassFET     = new BypassMosfet();
    device        = new Device(config, heater, fan, tempSensor, currentSensor, mainRelay, bypassFET, discharger, indicator);  // ✅ Instantiate Device

    // Begin all module services
    config->begin();
    indicator->begin();
    heater->begin(config);
    fan->begin();
    currentSensor->begin();
    tempSensor->begin(config);
    mainRelay->begin();
    bypassFET->begin();
    discharger->begin(heater);
    wifi->begin();
    sw->TapDetect();// start the tap detect 
    device->begin();  // ✅ Launch Device logic (handles startup sequence)
}

// ──────────────────────────────────────────────────────────────
//                            Loop
// ──────────────────────────────────────────────────────────────
void loop() {
    vTaskDelay(5000);  // Event-based system; main loop idle
}
