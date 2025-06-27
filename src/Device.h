#ifndef DEVICE_H
#define DEVICE_H

#include "HeaterManager.h"
#include "FanManager.h"
#include "TempSensor.h"
#include "CurrentSensor.h"
#include "Relay.h"
#include "BypassMosfet.h"
#include "CpDischg.h"
#include "Indicator.h"
#include "WiFiManager.h"
#include "ConfigManager.h"
#include "utils.h"
#include <Arduino.h>
#include "Buzzer.h"

class Device {
public:
    Device(ConfigManager* cfg,
           HeaterManager* heater,
           FanManager* fan,
           TempSensor* temp,
           CurrentSensor* current,
           Relay* relay,
           BypassMosfet* bypass,
           CpDischg* discharger,
           Indicator* ledIndicator,
            BuzzerManager* buz);

    void begin();                          // System startup sequence
    void StartLoop();                      // Main output cycle loop
    void shutdown();                       // Clean shutdown and discharge
    void checkAllowedOutputs();            // Updates allowedOutputs[] from config

    // RTOS management
    void startLoopTask();                              // Starts the loop as RTOS task
    static void loopTaskWrapper(void* pvParameters);   // Wrapper for RTOS
    void loopTask();                                   // Actual loop logic

    void startTemperatureMonitor();                    // Starts temperature monitor
    static void monitorTemperatureTask(void* param);   // Temperature task handler
    void stopLoopTask();  // Add this to the public section of the Device class



    // Subsystem pointers
    ConfigManager* config;
    HeaterManager* heaterManager;
    FanManager* fanManager;
    TempSensor* tempSensor;
    CurrentSensor* currentSensor;
    Relay* relayControl;
    BypassMosfet* bypassFET;
    CpDischg* discharger;
    Indicator* indicator;
    BuzzerManager* buz;

    // State tracking
    volatile DeviceState currentState = DeviceState::Idle;
    bool allowedOutputs[10] = {false};

    // RTOS task handles
    TaskHandle_t loopTaskHandle = nullptr;         // ✅ Main loop RTOS task
    TaskHandle_t tempMonitorTaskHandle = nullptr;  // Temp monitor RTOS task
};

#endif // DEVICE_H
