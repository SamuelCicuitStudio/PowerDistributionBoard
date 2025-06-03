// PowerManager.h
#ifndef POWERMANAGER_H
#define POWERMANAGER_H

#include <Arduino.h>                      // Arduino core
#include <OneWire.h>                      // OneWire bus
#include <DallasTemperature.h>            // DS18B20 sensors
#include "Config.h"                       // ConfigManager definitions
#include "Logger.h"                       // Logger definitions
#include <freertos/FreeRTOS.h>            // FreeRTOS core
#include <freertos/task.h>                // FreeRTOS tasks

// Default mains frequency (Hz) if none in NVS
#ifndef AC_FREQUENCY
#define AC_FREQUENCY 50
#endif
// Default “power level” for nichrome channels (0–255)
#ifndef CHANNEL_POWER_DUTY
  // 230 V out of a 325 V charged bus → duty ≈ 230/325 × 255 ≃ 180
  #define CHANNEL_POWER_DUTY 180
#endif


// LEDC channels
#define OPT_PWM_CHANNEL     0  // for INA_OPT_PWM_PIN -> nichrome power
#define BYPASS_PWM_CHANNEL  1  // for INA_E_PIN       -> bypass inrush


class PowerManager {
public:
    // Singleton access
    static PowerManager* instance;                     
    static PowerManager* getInstance() { return instance; }

    // Constructor
    PowerManager(ConfigManager* config, Logger* log, DallasTemperature* sensor);

    // Initialize hardware and launch non-blocking startup
    void begin();
    // Update on/off durations at runtime (and persist to preferences)
    void setCycleTime(uint32_t onMs, uint32_t offMs);
    // Enable or disable per‐channel LED feedback during heating
    void setLedFeedback(bool enabled);


    // Status & telemetry
    void toggleSystem();
    void reportStatus();                             // Log key system state
    float getVoltage() const;                        // Last measured capacitor voltage (V)
    float getVoltagePercentage() const;              // Raw ADC % of calibration
    const float* getTemperatureArray() const;        // Pointer to Temps[4]
    void startVoltageMonitorTask();                  // (Re)start voltage monitor pinned to core 1

    // Heating sequence params
    volatile  bool                 systemOn,systemOnwifi;

    // --- Startup + control tasks ---
    static void startupTask(void* pv);
    static void switchMonitorTask(void* pv);
    static void tempMonitorTask(void* pv);
#ifndef TEST_MODE
#ifdef NO_HARD_RESISTOR
    static void capMaintenanceTask(void* pv);
#endif
#endif
    static void voltageMonitorTask(void* pv);
    static void safetyMonitorTask(void* pv);
    static void powerLossTask(void* pv);
    static void sequenceControlTask(void* pv);

    // --- Core state & dependencies ---
    ConfigManager*       Config;                     // NVS-backed prefs
    Logger*              Log;                        // Logger
    DallasTemperature*   Sensor;                     // DS18B20 driver

    // Temps
    DeviceAddress        tempDeviceAddress[4];
    float                Temps[4];

    // Button debounce + sequence task handle
    volatile  bool                 lastState;
    // Task handles
    TaskHandle_t       startupHandle;        // startupTask
    TaskHandle_t       voltageHandle;        // voltageMonitorTask
    TaskHandle_t       switchHandle;         // switchMonitorTask
    TaskHandle_t       tempHandle;           // tempMonitorTask
    TaskHandle_t       safetyHandle;         // safetyMonitorTask
    TaskHandle_t       powerLossHandle;      // powerLossTask
    TaskHandle_t       capMaintHandle;       // capMaintenanceTask (if used)
    TaskHandle_t       sequenceHandle;       // sequenceControlTask



    volatile  bool                 ledFeedbackEnabled;
    uint32_t             onTime;
    uint32_t             offTime;

    // Voltage calibration
    uint8_t                AcFreq;            // frequency
    volatile  float        measuredVoltage;            // Volts
    int                  calibMax;                   // ADC raw peak
    float                chargeResistorOhs;          // Ω from config

public:
    // Expose pin arrays globally
    static const int     nichromePins[10];           // ENA01_E_PIN…ENA010_E_PIN
    static const int     floorLedPins[10];           // FL01_LED_PIN…FL10_LED_PIN
};

#endif // POWERMANAGER_H
