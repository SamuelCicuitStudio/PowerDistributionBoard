#ifndef DEVICE_H
#define DEVICE_H

#include <Arduino.h>
#include "HeaterManager.h"
#include "FanManager.h"
#include "TempSensor.h"
#include "CurrentSensor.h"
#include "Relay.h"
#include "BypassMosfet.h"
#include "CpDischg.h"
#include "Indicator.h"
#include "WiFiManager.h"
#include "NVSManager.h"
#include "utils.h"
#include "Buzzer.h"

// -----------------------------------------------------------------------------
// Constants and Macros
// -----------------------------------------------------------------------------
#define GO_THRESHOLD_RATIO (0.78f * CONF->GetFloat(DC_VOLTAGE_KEY, DEFAULT_DC_VOLTAGE))

// -----------------------------------------------------------------------------
// Global Synchronization Objects
// -----------------------------------------------------------------------------
extern SemaphoreHandle_t gStateMtx;
extern EventGroupHandle_t gEvt;

// Event flags for state transitions
#define EVT_WAKE_REQ  (1 << 0)
#define EVT_RUN_REQ   (1 << 1)
#define EVT_STOP_REQ  (1 << 2)

// Safe state transition lock/unlock
static inline bool StateLock()   { return xSemaphoreTake(gStateMtx, portMAX_DELAY) == pdTRUE; }
static inline void StateUnlock() { xSemaphoreGive(gStateMtx); }

// -----------------------------------------------------------------------------
// Enumerations
// -----------------------------------------------------------------------------
/**
 * @brief Defines the operating recharge modes for the device.
 */
enum class RechargeMode : uint8_t {
    BatchRecharge = 0,  ///< Relay OFF during pulses, then ON until voltage threshold reached
    KeepRecharge  = 1   ///< Limited recharge path kept active during pulses
};

// -----------------------------------------------------------------------------
// Device Class Definition
// -----------------------------------------------------------------------------
/**
 * @class Device
 * @brief Central controller class managing the main hardware subsystems.
 *
 * This class coordinates system startup, safe shutdown, state transitions,
 * and periodic monitoring of temperature, indicators, and operational loops.
 */
class Device {
public:
    // -------------------------------------------------------------------------
    // Singleton Interface
    // -------------------------------------------------------------------------

    /**
     * @brief Initializes the Device singleton with subsystem pointers.
     */
    static void Init(HeaterManager* heater,
                     TempSensor* temp,
                     CurrentSensor* current,
                     Relay* relay,
                     BypassMosfet* bypass,
                     CpDischg* discharger,
                     Indicator* ledIndicator);

    /**
     * @brief Returns the singleton instance (nullptr until initialized).
     */
    static Device* Get();

    /// Public access for legacy compatibility
    static Device* instance;

    // -------------------------------------------------------------------------
    // Construction & Initialization
    // -------------------------------------------------------------------------
    Device(HeaterManager* heater,
           TempSensor* temp,
           CurrentSensor* current,
           Relay* relay,
           BypassMosfet* bypass,
           CpDischg* discharger,
           Indicator* ledIndicator);

    void begin();          ///< Performs system startup and initialization
    void StartLoop();      ///< Main operational loop
    void shutdown();       ///< Executes safe shutdown and discharge sequence
    void checkAllowedOutputs();  ///< Updates allowed output states based on configuration

    // -------------------------------------------------------------------------
    // Mode Control
    // -------------------------------------------------------------------------
    void setRechargeMode(RechargeMode mode) { rechargeMode = mode; }

    // -------------------------------------------------------------------------
    // RTOS Task Management
    // -------------------------------------------------------------------------
    void startLoopTask();                              ///< Starts main loop as RTOS task
    static void loopTaskWrapper(void* pvParameters);   ///< Wrapper for FreeRTOS task
    void loopTask();                                   ///< Actual loop logic

    void startTemperatureMonitor();                    ///< Launches temperature monitoring
    static void monitorTemperatureTask(void* param);   ///< Temperature monitoring task handler
    void stopTemperatureMonitor();                     ///< Stops temperature monitoring

    void stopLoopTask();                               ///< Stops main loop task

    void updateLed();                                  ///< Manual LED update
    static void LedUpdateTask(void* param);            ///< LED update RTOS task

    // -------------------------------------------------------------------------
    // Subsystem References
    // -------------------------------------------------------------------------
    HeaterManager* heaterManager;
    TempSensor*    tempSensor;
    CurrentSensor* currentSensor;
    Relay*         relayControl;
    BypassMosfet*  bypassFET;
    CpDischg*      discharger;
    Indicator*     indicator;

    // -------------------------------------------------------------------------
    // State and Configuration
    // -------------------------------------------------------------------------
    volatile DeviceState currentState = DeviceState::Idle;
    volatile RechargeMode rechargeMode = RechargeMode::BatchRecharge;
    bool allowedOutputs[10] = {false};

    // -------------------------------------------------------------------------
    // RTOS Task Handles
    // -------------------------------------------------------------------------
    TaskHandle_t loopTaskHandle       = nullptr;
    TaskHandle_t tempMonitorTaskHandle = nullptr;
    TaskHandle_t ledTaskHandle         = nullptr;

private:
    // Constructor remains public for backward compatibility
};

// -----------------------------------------------------------------------------
// Global Accessor Macro
// -----------------------------------------------------------------------------
#define DEVICE Device::Get()

#endif // DEVICE_H
