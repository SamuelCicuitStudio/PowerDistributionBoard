#ifndef DEVICE_H
#define DEVICE_H

#include "HeaterManager.h"
#include "FanManager.h"
#include "TempSensor.h"
#include "CurrentSensor.h"
#include "Relay.h"
#include "BypassMosfet.h"
#include "CpDischg.h"
#include "Indicator.h"         // ✅ Include for LED status management
#include "WiFiManager.h"       // ✅ Include for WiFi management



class Device {
public:
    Device(ConfigManager* cfg,             // Pointer to configuration manager
           HeaterManager* heater,          // Pointer to heater manager
           FanManager* fan,                // Pointer to fan manager
           TempSensor* temp,               // Pointer to temperature manager
           CurrentSensor* current,         // Pointer to current sensor
           Relay* relay,                   // Pointer to main relay
           BypassMosfet* bypass,           // Pointer to bypass MOSFET
           CpDischg* discharger,           // Pointer to capacitor discharge handler
           Indicator* ledIndicator        // ✅ Pointer to Indicator manager (for feedback LEDs)
            );

    void begin();                          // System startup sequence
    void loop();                           // Optional runtime tasks
    void checkAllowedOutputs();            // Updates allowedOutputs[] from config
    void StartLoop();                      // Main output cycle loop
    void shutdown();                       // Clean shutdown and discharge


    ConfigManager* config;                 // System preferences manager
    HeaterManager* heaterManager;          // Heater output control
    FanManager* fanManager;                // Fan speed and temp control
    TempSensor* tempSensor;                // Temperature sensors
    CurrentSensor* currentSensor;          // Current sensor (load monitor)
    Relay* relayControl;                   // Power input relay control
    BypassMosfet* bypassFET;               // Bypass MOSFET control
    CpDischg* discharger;                  // Capacitor discharge controller
    Indicator* indicator;                  // ✅ LED feedback controller


    volatile DeviceState currentState = DeviceState::Idle;       // Current operating state
    
    bool allowedOutputs[10] = {false};     // Cached state of outputs (true if user-enabled)

    TaskHandle_t tempMonitorTaskHandle = nullptr;        // RTOS task handle for temperature monitor
    static void monitorTemperatureTask(void* param);     // RTOS monitor loop
    void startTemperatureMonitor();                      // Starts RTOS temperature monitor

};

#endif // DEVICE_H
