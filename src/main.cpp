#include <Arduino.h>
#include "config.h"

// ──────────────────────────────────────────────────────────────
//                        Module Headers
// ──────────────────────────────────────────────────────────────
#include "ConfigManager.h"
#include "WiFiManager.h"
#include "SwitchManager.h"
#include "Device.h"  // ✅ Central system controller

// ──────────────────────────────────────────────────────────────
//                    Global Object Pointers
// ──────────────────────────────────────────────────────────────
Preferences       prefs;           // NVS storage for preferences

ConfigManager*    config        = nullptr;
Indicator*        indicator     = nullptr;
HeaterManager*    heater        = nullptr;
CpDischg*         discharger    = nullptr;
FanManager*       fan           = nullptr;
CurrentSensor*    currentSensor = nullptr;
TempSensor*       tempSensor    = nullptr;
Relay*            mainRelay     = nullptr;
BypassMosfet*     bypassFET     = nullptr;
Device*           device        = nullptr;
WiFiManager*      wifi          = nullptr;
SwitchManager*    sw            = nullptr;

// ──────────────────────────────────────────────────────────────
//                          Setup()
// ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // 🔧 Initialize NVS (must happen before using preferences)
    prefs.begin(CONFIG_PARTITION, false);

    // 🧠 Core Config Manager (must be first)
    config = new ConfigManager(&prefs);
    config->begin();

    // 💡 LED Indicators
    indicator = new Indicator();
    indicator->begin();

    // 🔥 Heater Outputs
    heater = new HeaterManager(config);
    heater->begin();

    // ⚡ Capacitor Discharge Manager
    discharger = new CpDischg(heater);
    discharger->begin();

    // 🌀 Fan PWM Control
    fan = new FanManager();
    fan->begin();

    // ⚙️ Current Monitoring
    currentSensor = new CurrentSensor();
    currentSensor->begin();

    // 🌡️ Temperature Sensors (DS18B20)
    tempSensor = new TempSensor(config);
    tempSensor->begin();

    // 🔌 Main Power Relay
    mainRelay = new Relay();
    mainRelay->begin();

    // ⛔ Inrush Bypass MOSFET
    bypassFET = new BypassMosfet();
    bypassFET->begin();

    // 📦 Main Device Logic (core controller)
    device = new Device(
        config,
        heater,
        fan,
        tempSensor,
        currentSensor,
        mainRelay,
        bypassFET,
        discharger,
        indicator
    );
    device->begin();

    // 🌐 Wi-Fi Access Point & Web Interface
    wifi = new WiFiManager(&WiFi, device);
    wifi->begin();

    // 🔘 Switch Detection (power button tap detection)
    sw = new SwitchManager(config, wifi);
    sw->TapDetect();
}

// ──────────────────────────────────────────────────────────────
//                           Loop()
// ──────────────────────────────────────────────────────────────
void loop() {
    vTaskDelay(5000);  // 💤 System runs on tasks; loop stays idle
}
