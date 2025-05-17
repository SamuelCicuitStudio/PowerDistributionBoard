#include <Arduino.h>             // Core Arduino library for ESP32
#include <Preferences.h>         // Persistent storage for ESP32
#include "SleepTimer.h"          ///< Handles power-saving sleep mode operations

// ======================================================
// System Managers - Handling Various Hardware Modules
// ======================================================
#include "ConfigManager.h"       // Configuration and persistent storage management
#include "RTCManager.h"          // Real-time clock (RTC) handling
#include "PowerManager.h"        // Power supply and battery monitoring
#include "Logger.h"              // System event logging and debugging
#include "WiFiManager.h"         // Wi-Fi connectivity and network management

// ========================================
// Global Object Pointers - System Managers
// ========================================
ConfigManager* config = nullptr;       // System configuration manager
RTCManager* RTC = nullptr;             // Real-time clock manager
PowerManager* powerMgr = nullptr;      // Power and battery monitoring
Logger* Log = nullptr;                 // System event logger
WiFiManager* wifi = nullptr;           // Wi-Fi manager
SleepTimer* Sleep = nullptr;           ///< Pointer to sleep manager for power efficiency

// ==============================
// Global Variables - Data Storage
// ==============================
struct tm timeInfo;   // Real-time clock data structure
Preferences prefs;    // Non-volatile storage for settings

// ==========================================
// Create a DallasTemperature
// ==========================================
// Setup a OneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature* Temp;

// ==============================
// Debounce Timing
// ==============================
const unsigned long DEBOUNCE_TIME_MS = 200;
volatile unsigned long lastWifiRestartTime = 0;
volatile unsigned long lastPowerSwitchTime = 0;
volatile unsigned long last230VACCheckTime = 0;

void setup() {
    Serial.begin(115200);  ///< Initialize serial communication for debugging

    // Initialize Preferences
    prefs.begin(CONFIG_PARTITION, false);  
    config = new ConfigManager(&prefs);
    config->begin();

    // Create a DallasTemperature sensor 
    DallasTemperature* Temp = new DallasTemperature(&oneWire);
    Temp->begin();

    // RTC Initialization
    RTC = new RTCManager(&timeInfo);
    RTC->setUnixTime(config->GetULong64(CURRENT_TIME_SAVED, DEFAULT_CURRENT_TIME_SAVED));

    // Logger and Sleep Timer
    Log = new Logger(RTC);
    Log->Begin();

    Sleep = new SleepTimer(RTC, config, Log);
    Sleep->timerLoop();

    // Power management
    powerMgr = new PowerManager(config, Log,Temp);
    powerMgr->begin();

    // Wi-Fi
    wifi = new WiFiManager(config, Sleep,powerMgr);
    wifi->begin();
}

void loop() {
    return;
}

