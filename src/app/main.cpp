#include <Arduino.h>
#include <Config.hpp>

// **************************************************************
//                       Module Includes
// **************************************************************
#include <NVSManager.hpp>
#include <WiFiManager.hpp>
#include <SwitchManager.hpp>
#include <Device.hpp>
#include <SleepTimer.hpp>
#include <NtcSensor.hpp>
#include <CalibrationRecorder.hpp>
#include <FS.h>
#include <SPIFFS.h>

// OneWire bus (for digital temperature sensors like DS18B20)
OneWire oneWire(ONE_WIRE_BUS);

// **************************************************************
//                   Global Object Pointers
// **************************************************************

// Non-volatile storage (ESP32 Preferences/NVS)
Preferences prefs;

// Core system modules (created at runtime)
Indicator*     indicator     = nullptr;
CpDischg*      discharger    = nullptr;
CurrentSensor* currentSensor = nullptr;
TempSensor*    tempSensor    = nullptr;
Relay*         mainRelay     = nullptr;
SwitchManager* sw            = nullptr;

// **************************************************************
//         Wi-Fi Event Handler (AP client connect/disconnect)
// **************************************************************
void WiFiEvent(WiFiEvent_t event) {
  // If WiFiManager isn't ready or buzzer isn't available, ignore.
  if (!WiFiManager::instance || !BUZZ) return;

  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      // A phone/PC connected to the ESP32 Access Point
      BUZZ->bipClientConnected();
      break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      // A phone/PC disconnected from the ESP32 Access Point
      BUZZ->bipClientDisconnected();
      break;

    default:
      // Other WiFi events are not handled here
      break;
  }
}

// **************************************************************
//                           setup()
// **************************************************************
void setup() {
  // --------------------------------------------------
  // 1) Debug / Diagnostics FIRST
  //    (So we can see boot progress and failures)
  // --------------------------------------------------
  Debug::begin(SERIAL_BAUD_RATE);
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("==================================================");
  DEBUG_PRINTLN("[Setup] System boot");
  DEBUG_PRINTLN("==================================================");
  delay(2000);

  // --------------------------------------------------
  // 2) Filesystem + Persistent Storage + Config
  //    (Must be ready before any logic that uses config values)
  // --------------------------------------------------
  DEBUG_PRINTLN("[Setup] Mounting SPIFFS...");
  if (!SPIFFS.begin(true)) {
    DEBUG_PRINTLN("[FATAL] SPIFFS initialization failed!");
    // Critical system: stop here instead of running with unknown state.
    while (true) {
      delay(500);
    }
  }
  DEBUG_PRINTLN("[Setup] SPIFFS mounted.");

  // Enable a memory log buffer (useful for post-mortem/debug dumps)
  Debug::enableMemoryLog(1024 * 1024);

  // Initialize NVS + config manager
  NVS::Init();
  CONF->begin();
  DEBUG_PRINTLN("[Setup] NVS + Config initialized.");

  // --------------------------------------------------
  // 3) Force the power path and loads into a SAFE/OFF state
  //    Goal: nothing should energize outputs during boot.
  // --------------------------------------------------
  // Main relay (disconnects the load path)
  mainRelay = new Relay();
  mainRelay->begin();
  mainRelay->turnOff();  // Ensure load path is open

  // Deep-sleep timer singleton (controls sleep entry)
  SleepTimer::Init();
  SLEEP->reset();

  // Capacitor discharge manager (must NOT actively discharge on boot)
  discharger = new CpDischg(mainRelay);
  discharger->begin();
  discharger->setBypassRelayGate(false); // No forced bypass / no discharge drive

  // Heater/wire outputs: MUST be OFF before current sensor auto-zero
  HeaterManager::Init();
  WIRE->begin();
  WIRE->disableAll(); // Absolutely no heater outputs

  // Fan manager is safe to init here (doesn't energize the main load path)
  FanManager::Init();
  FAN->begin();

  DEBUG_PRINTLN("[Setup] Power path + Heater/Wire/Fan initialized in SAFE/OFF state.");

  // --------------------------------------------------
  // 4) Status Indicators + Buzzer
  //    (Now we can signal boot state, ready state, errors, alarms)
  // --------------------------------------------------
  RGB->RGBLed::Init(POWER_OFF_LED_PIN, READY_LED_PIN, LED_R3_LED_PIN);
  RGB->begin();
  RGB->setDeviceState(DevState::BOOT); // Indicate boot sequence

  indicator = new Indicator();
  indicator->begin();
  indicator->clearAll();

  Buzzer::Init();
  BUZZ->begin();

  DEBUG_PRINTLN("[Setup] Indicators + Buzzer initialized.");

  // --------------------------------------------------
  // 5) Sensors / Measurements / Protection
  //    Important: do this AFTER outputs are OFF, so auto-zero is valid.
  // --------------------------------------------------
  currentSensor = new CurrentSensor();
  currentSensor->begin(); // Auto-calibration at true 0A (inside CurrentSensor)

  tempSensor = new TempSensor(&oneWire);
  tempSensor->begin();

  DEBUG_PRINTLN("[Setup] Current & temperature sensing initialized (zero-cal done).");

  // NTC sensor subsystem + calibration recorder
  NtcSensor::Init();
  NTC->begin(POWER_ON_SWITCH_PIN);

  CalibrationRecorder::Init();

  // --------------------------------------------------
  // 6) Device Orchestrator / State Machine
  //    At this point:
  //      - Config loaded
  //      - Outputs forced OFF
  //      - Current sensor calibrated at 0A
  //      - Temperature sensors online
  //    -> Hand control to Device state machine.
  // --------------------------------------------------
  Device::Init(tempSensor, currentSensor, mainRelay, discharger, indicator);
  DEVICE->begin(); // Handles 12V detect, capacitor charge sequence, protections, etc.

  DEBUG_PRINTLN("[Setup] Device initialized.");

  // --------------------------------------------------
  // 7) Connectivity (non-critical)
  //    Start Wi-Fi AFTER the safety core is running.
  // --------------------------------------------------
  WiFiManager::Init();
  WiFi.onEvent(WiFiEvent);
  WIFI->begin();

  DEBUG_PRINTLN("[Setup] WiFiManager initialized.");

  // --------------------------------------------------
  // 8) User Input / Power Switch Handling (LAST)
  //    Start listening for taps/presses only after everything is stable.
  // --------------------------------------------------
  sw = new SwitchManager();
  sw->TapDetect(); // Start tap detection / power logic

  DEBUG_PRINTLN("[Setup] SwitchManager initialized.");
  DEBUG_PRINTLN("==================================================");
  DEBUG_PRINTLN("[Setup] Boot sequence complete.");
  DEBUG_PRINTLN("==================================================");
}

// **************************************************************
//                            loop()
// **************************************************************
void loop() {
  // Main app is event/task-driven; loop stays lightweight.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
