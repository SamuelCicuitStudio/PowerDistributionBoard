#include <Arduino.h>
#include "config.h"

// ──────────────────────────────────────────────────────────────
//                        Module Headers
// ──────────────────────────────────────────────────────────────
#include "ConfigManager.h"
#include "WiFiManager.h"
#include "SwitchManager.h"
#include "Device.h"  // ✅ Central system controller

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// ──────────────────────────────────────────────────────────────
//                    Global Object Pointers
// ──────────────────────────────────────────────────────────────

// Non-volatile storage
Preferences prefs;  // 🔒 NVS storage for user/system settings

// Core Modules
ConfigManager*    config        = nullptr;  // 🧠 Configuration handler
Indicator*        indicator     = nullptr;  // 💡 LED status indicator
HeaterManager*    heater        = nullptr;  // 🔥 Controls heating elements
CpDischg*         discharger    = nullptr;  // ⚡ Capacitor discharge logic
FanManager*       fan           = nullptr;  // 🌀 Fan control via PWM
CurrentSensor*    currentSensor = nullptr;  // ⚙️ Current sensing
TempSensor*       tempSensor    = nullptr;  // 🌡️ DS18B20 temperature sensors
Relay*            mainRelay     = nullptr;  // 🔌 Main relay control
BypassMosfet*     bypassFET     = nullptr;  // ⛔ Inrush control MOSFET
BuzzerManager*    buzz          = nullptr;  // 🔔 Sound feedback (active-low buzzer)
Device*           device        = nullptr;  // 📦 Main device orchestrator
WiFiManager*      wifi          = nullptr;  // 🌐 Wi-Fi + web interface
SwitchManager*    sw            = nullptr;  // 🔘 Power button tap detection

// ──────────────────────────────────────────────────────────────
//              Wi-Fi Event Handler for AP Client Events
// ──────────────────────────────────────────────────────────────
void WiFiEvent(WiFiEvent_t event) {
    if (!WiFiManager::instance || !WiFiManager::instance->dev || !WiFiManager::instance->dev->buz)
        return;

    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            // 🔔 Client connected to ESP32 AP
            WiFiManager::instance->dev->buz->bipClientConnected();
            break;

        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            // 🔕 Client disconnected from ESP32 AP
            WiFiManager::instance->dev->buz->bipClientDisconnected();
            break;

        default:
            break;
    }
}

// ──────────────────────────────────────────────────────────────
//                          Setup()
// ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#          Starting System Setup 921600 Baud⚙️            #");
    DEBUG_PRINTLN("###########################################################");

    // 🔧 Initialize non-volatile storage (NVS)
    DEBUG_PRINTLN("[Setup] Initializing NVS (Preferences)...");
    prefs.begin(CONFIG_PARTITION, false);
    DEBUG_PRINTLN("[Setup] NVS Initialized. ✅");

    // 📁 Mount SPIFFS filesystem
    DEBUG_PRINTLN("[Setup] Mounting SPIFFS...");
    if (!SPIFFS.begin(true)) {
        DEBUG_PRINTLN("SPIFFS initialization failed! ❌");
        return;
    }
    DEBUG_PRINTLN("✅ SPIFFS successfully mounted.");
    delay(500);  // Let Serial settle

    // 🧠 Load system configuration
    config = new ConfigManager(&prefs);
    config->begin();

    // 💡 Initialize LED indicators
    indicator = new Indicator();
    indicator->begin();

    // 🔥 Initialize heater control logic
    heater = new HeaterManager(config);
    heater->begin();

    // ⚡ Initialize capacitor discharge manager
    discharger = new CpDischg(heater);
    discharger->begin();

    // 🌀 Initialize fan control
    fan = new FanManager();
    fan->begin();

    // ⚙️ Initialize current sensor
    currentSensor = new CurrentSensor();
    currentSensor->begin();

    // 🌡️ Initialize temperature sensors
    tempSensor = new TempSensor(config,&oneWire);
    tempSensor->begin();

    // 🔌 Initialize power relay
    mainRelay = new Relay();
    mainRelay->begin();

    // ⛔ Initialize bypass MOSFET
    bypassFET = new BypassMosfet();
    bypassFET->begin();

    // 🔔 Initialize buzzer
    buzz = new BuzzerManager();
    buzz->begin();

    // 📦 Create and start main system controller
    device = new Device(
        config,
        heater,
        fan,
        tempSensor,
        currentSensor,
        mainRelay,
        bypassFET,
        discharger,
        indicator,
        buzz
    );
    device->begin();

    // 🌐 Initialize Wi-Fi AP + web interface
    wifi = new WiFiManager(device);
    // 🔔 Register Wi-Fi event handler (for client connect/disconnect)
    WiFi.onEvent(WiFiEvent);
    wifi->begin();

    // 🔘 Setup tap detection for the power switch
    sw = new SwitchManager(config, wifi);
    sw->TapDetect();
}

// ──────────────────────────────────────────────────────────────
//                           Loop()
// ──────────────────────────────────────────────────────────────
void loop() {
    vTaskDelay(5000);  // 💤 System operates on FreeRTOS tasks
}