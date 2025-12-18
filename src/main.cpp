#include <Arduino.h>
#include "system/Config.h"

// **************************************************************
//                        Module Headers
// **************************************************************
#include "services/NVSManager.h"
#include "comms/WiFiManager.h"
#include "comms/SwitchManager.h"
#include "system/Device.h"
#include "services/SleepTimer.h"
#include "sensing/NtcSensor.h"
#include "services/CalibrationRecorder.h"
#include "services/ThermalPiControllers.h"

// OneWire bus instance
OneWire oneWire(ONE_WIRE_BUS);

// **************************************************************
// Global Object Pointers
// **************************************************************

// Non-volatile storage
Preferences prefs;

// Core modules
Indicator*     indicator     = nullptr;
CpDischg*      discharger    = nullptr;
CurrentSensor* currentSensor = nullptr;
TempSensor*    tempSensor    = nullptr;
Relay*         mainRelay     = nullptr;
SwitchManager* sw            = nullptr;

// **************************************************************
//              Wi-Fi Event Handler for AP Client Events
// **************************************************************
void WiFiEvent(WiFiEvent_t event) {
    if (!WiFiManager::instance || !BUZZ)
        return;

    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            // ðŸ”” Client connected to ESP32 AP
            BUZZ->bipClientConnected();
            break;

        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            // ðŸ”• Client disconnected from ESP32 AP
            BUZZ->bipClientDisconnected();
            break;

        default:
            break;
    }
}
// **************************************************************
//                          Setup()
// **************************************************************
void setup() {
  // --------------------------------------------------
  // 1. Debug / Diagnostics FIRST
  // --------------------------------------------------
  Debug::begin(SERIAL_BAUD_RATE);
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("==================================================");
  DEBUG_PRINTLN("[Setup] System boot");
  DEBUG_PRINTLN("==================================================");
  delay(2000);
  // --------------------------------------------------
  // 2. Persistent Storage & Configuration
  //    (Must be ready before any logic that depends on config)
  // --------------------------------------------------
  DEBUG_PRINTLN("[Setup] Mounting SPIFFS...");
  if (!SPIFFS.begin(true)) {
    DEBUG_PRINTLN("[FATAL] SPIFFS initialization failed!");
    // Critical system: halt here, don't continue blindly.
    while (true) {
      delay(500);
    }
  }
  DEBUG_PRINTLN("[Setup] SPIFFS mounted.");

  NVS::Init();
  CONF->begin();
  DEBUG_PRINTLN("[Setup] NVS + Config initialized.");

  // Sleep timer singleton (used for deep sleep entry)
  SleepTimer::Init();
  SLEEP->reset();

  // --------------------------------------------------
  // 3. Status / Indicators (so we can signal states & faults)
  // --------------------------------------------------
  RGB->RGBLed::Init(POWER_OFF_LED_PIN, READY_LED_PIN, LED_R3_LED_PIN);
  RGB->begin();
  RGB->setDeviceState(DevState::BOOT);   // Show we're in boot sequence

  indicator = new Indicator();
  indicator->begin();
  indicator->clearAll();
  // Buzzer (for alarms/feedback)
  Buzzer::Init();
  BUZZ->begin();

  DEBUG_PRINTLN("[Setup] Indicators + Buzzer initialized.");

  // --------------------------------------------------
  // 4. Core Power Path Components + Loads in SAFE STATE
  //    Make sure NOTHING is actively driving load.
  // --------------------------------------------------
  // Main Relay
  mainRelay = new Relay();
  mainRelay->begin();
  mainRelay->turnOff();                  // Ensure load path is open

  // Capacitor Discharge Manager
  discharger = new CpDischg(mainRelay);
  discharger->begin();
  discharger->setBypassRelayGate(false); // No forced bypass / no discharge drive

  // Heater manager + wire model (must be forced OFF before current calibration)
  HeaterManager::Init();
  WIRE->begin();
  WIRE->disableAll();                    // Absolutely no heater outputs

  // Fan manager (safe to init; it doesn't create load through ACS path)
  FanManager::Init();
  FAN->begin();

  DEBUG_PRINTLN("[Setup] Power path + Heater/Wire/Fan initialized in SAFE/OFF state.");

  // --------------------------------------------------
  // 5. Measurement & Protection
  //    Now that all paths are OPEN/OFF, we can trust 0 A for auto-zero.
  // --------------------------------------------------
  currentSensor = new CurrentSensor();
  currentSensor->begin();                // Auto-calibration at true 0A (inside CurrentSensor)

  tempSensor = new TempSensor(&oneWire);
  tempSensor->begin();

  DEBUG_PRINTLN("[Setup] Current & temperature sensing initialized (zero-cal done).");

  NtcSensor::Init();
  NTC->begin();
  CalibrationRecorder::Init();
  ThermalPiControllers::Init();
  THERMAL_PI->begin();

  // --------------------------------------------------
  // 6. Device Orchestrator
  //    At this point:
  //      - Config is loaded
  //      - All loads are OFF
  //      - Relay/bypass/discharger are safe
  //      - Current sensor is calibrated
  //      - Temps are online
  //    â†’ Hand over to Device state machine.
  // --------------------------------------------------
  Device::Init(tempSensor, currentSensor, mainRelay, discharger, indicator);
  DEVICE->begin();                       // Handles 12V detect, cap charge, etc.

  DEBUG_PRINTLN("[Setup] Device initialized.");

  // --------------------------------------------------
  // 7. Connectivity (non-critical, AFTER safety core is up)
  // --------------------------------------------------
  WiFiManager::Init();
  WiFi.onEvent(WiFiEvent);
  WIFI->begin();

  DEBUG_PRINTLN("[Setup] WiFiManager initialized.");

  // --------------------------------------------------
  // 8. User Input / Power Switch Handling (LAST)
  // --------------------------------------------------
  sw = new SwitchManager();
  sw->TapDetect();                       // Start tap detection / power logic

  DEBUG_PRINTLN("[Setup] SwitchManager initialized.");
  DEBUG_PRINTLN("==================================================");
  DEBUG_PRINTLN("[Setup] Boot sequence complete.");
  DEBUG_PRINTLN("==================================================");
}

// **************************************************************
//                           Loop()
// **************************************************************
void loop() {
  DEVICE->StartLoop();
}
