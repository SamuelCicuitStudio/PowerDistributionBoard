#include <Arduino.h>
#include "config.h"

// ──────────────────────────────────────────────────────────────
//                        Module Headers
// ──────────────────────────────────────────────────────────────
#include "NVSManager.h"
#include "WiFiManager.h"
#include "SwitchManager.h"
#include "Device.h"

// OneWire bus instance
OneWire oneWire(ONE_WIRE_BUS);

// ──────────────────────────────────────────────────────────────
// Global Object Pointers
// ──────────────────────────────────────────────────────────────

// Non-volatile storage
Preferences prefs;

// Core modules
Indicator*     indicator     = nullptr;
HeaterManager* heater        = nullptr;
CpDischg*      discharger    = nullptr;
CurrentSensor* currentSensor = nullptr;
TempSensor*    tempSensor    = nullptr;
Relay*         mainRelay     = nullptr;
BypassMosfet*  bypassFET     = nullptr;
SwitchManager* sw            = nullptr;

// ──────────────────────────────────────────────────────────────
//              Wi-Fi Event Handler for AP Client Events
// ──────────────────────────────────────────────────────────────
void WiFiEvent(WiFiEvent_t event) {
    if (!WiFiManager::instance || !BUZZ)
        return;

    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            // 🔔 Client connected to ESP32 AP
            BUZZ->bipClientConnected();
            break;

        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            // 🔕 Client disconnected from ESP32 AP
            BUZZ->bipClientDisconnected();
            break;

        default:
            break;
    }
}
// ──────────────────────────────────────────────────────────────
//                          Setup()
// ──────────────────────────────────────────────────────────────
void setup() {
  Debug::begin(SERIAL_BAUD_RATE);

  // SPIFFS
  DEBUG_PRINTLN("[Setup] Mounting SPIFFS...");
  if (!SPIFFS.begin(true)) {
    DEBUG_PRINTLN("SPIFFS initialization failed!");
    return;
  }
  DEBUG_PRINTLN("SPIFFS successfully mounted.");
  NVS::Init();
  CONF->begin();

  RGB->RGBLed::Init(POWER_OFF_LED_PIN, READY_LED_PIN, false);
  RGB->begin();
  RGB->setDeviceState(DevState::BOOT);

  Buzzer::Init();    
  BUZZ->begin();    

  FanManager::Init();
  FAN->begin();

  // Heater
  heater = new HeaterManager();
  heater->begin();
  heater->disableAll();

  // Relay
  mainRelay = new Relay();
  mainRelay->begin();

  // Bypass MOSFET
  bypassFET = new BypassMosfet();
  bypassFET->begin();

  // Indicator
  indicator = new Indicator();
  indicator->begin();

  // Capacitor discharge
  discharger = new CpDischg(heater, mainRelay);
  discharger->begin();
  discharger->setBypassRelayGate(true);

  // Current sensor
  currentSensor = new CurrentSensor();
  currentSensor->begin();

  // Temperature sensors
  tempSensor = new TempSensor(&oneWire);
  tempSensor->begin();

  indicator->clearAll();

  // Device singleton
  Device::Init(heater, tempSensor, currentSensor, mainRelay, bypassFET, discharger, indicator);
  DEVICE->begin();

  // Wi-Fi singleton
  WiFiManager::Init();
  WiFi.onEvent(WiFiEvent);
  WIFI->begin();

  // Power switch tap detection
  sw = new SwitchManager();
  sw->TapDetect();
  mainRelay->turnOn();
}

// ──────────────────────────────────────────────────────────────
//                           Loop()
// ──────────────────────────────────────────────────────────────
void loop() {
  DEVICE->StartLoop();
}
