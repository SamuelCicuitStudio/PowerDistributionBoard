/**
 * @brief Main heating loop & RTOS-oriented coordination.
 *
 * Once the device enters DeviceState::Running, the Device class coordinates
 * nichrome heater drive in one of two compile-time modes:
 *
 *  - DEVICE_LOOP_MODE_SEQUENTIAL
 *  - DEVICE_LOOP_MODE_ADVANCED
 *
 * The responsibilities are cleanly split:
 *
 *  - CurrentSensor:
 *      - Runs a continuous sampling task.
 *      - Maintains a 10s ring buffer of (timestamp, current).
 *
 *  - HeaterManager:
 *      - Owns all heater GPIOs.
 *      - Applies output masks atomically.
 *      - Logs output-mask changes into a small ring buffer.
 *
 *  - Device thermal integration (thermal task):
 *      - Sole owner of virtual wire temperatures and lockout state.
 *      - Periodically consumes:
 *          - current history from CurrentSensor,
 *          - output history from HeaterManager.
 *      - For each time segment, applies:
 *          - cooling toward ambient using per-wire tau,
 *          - heating based on measured I(t), active mask, and R(T),
 *          - clamping at 150°C and re-enable hysteresis.
 *      - Publishes results via HeaterManager::setWireEstimatedTemp().
 *
 *  - Device loop (sequential / advanced):
 *      - Uses checkAllowedOutputs() (config + thermal lockout) to determine
 *        eligible wires.
 *      - In SEQUENTIAL mode:
 *          - Picks the coolest eligible wire (virtual temperature),
 *            drives it alone for onTime, idles for offTime.
 *      - In ADVANCED mode:
 *          - Uses the existing planner to build a sequence of group masks
 *            approximating a target R_eq, respecting MAX_ACTIVE and
 *            allowedOutputs[].
 *          - Applies each mask for onTime / offTime via HeaterManager.
 *      - All timings use delayWithPowerWatch() to react immediately to:
 *          - 12V supply loss (handle12VDrop()),
 *          - STOP requests (event group).
 *
 * On entry:
 *  - waitForWiresNearAmbient() ensures a stable starting point.
 *  - Background DS18B20 monitoring is started.
 *  - CurrentSensor continuous sampling and the thermal task are active.
 *
 * On exit:
 *  - stopTemperatureMonitor() is called.
 *  - All heater outputs and indicators are forced OFF.
 *  - Control returns to loopTask(), which may transition to Idle/Shutdown
 *    or start another run.
 */

#ifndef DEVICE_H
#define DEVICE_H

#include <Arduino.h>
#include "HeaterManager.h"
#include "FanManager.h"
#include "TempSensor.h"
#include "CurrentSensor.h"
#include "Relay.h"
#include "CpDischg.h"
#include "Indicator.h"
#include "WiFiManager.h"
#include "NVSManager.h"
#include "utils.h"
#include "Buzzer.h"
#include "PowerTracker.h"
// -----------------------------------------------------------------------------
// Constants and Macros
// -----------------------------------------------------------------------------

/// Voltage ratio required on the DC bus before enabling full-power operation.
#define GO_THRESHOLD_RATIO (0.78f * CONF->GetFloat(DC_VOLTAGE_KEY, DEFAULT_DC_VOLTAGE))

// -----------------------------------------------------------------------------
// Loop Mode Selection (compile-time)
// -----------------------------------------------------------------------------

/**
 * @brief Selects the behavior of Device::StartLoop().
 *
 *  - DEVICE_LOOP_MODE_ADVANCED:
 *        Multi-output / grouped drive using the resistance planner and
 *        batch-style recharge behavior.
 *
 *  - DEVICE_LOOP_MODE_SEQUENTIAL:
 *        Simple mode: drives one allowed output at a time based on virtual
 *        temperature (coolest-first), using ON_TIME / OFF_TIME.
 */
#define DEVICE_LOOP_MODE_ADVANCED   0
#define DEVICE_LOOP_MODE_SEQUENTIAL 1

/// Default mode (preserves advanced grouped behavior).
#define DEVICE_LOOP_MODE DEVICE_LOOP_MODE_ADVANCED

// -----------------------------------------------------------------------------
// Global Synchronization Objects
// -----------------------------------------------------------------------------

extern SemaphoreHandle_t gStateMtx;
extern EventGroupHandle_t gEvt;

// Event flags for high-level state transitions.
#define EVT_WAKE_REQ  (1 << 0)
#define EVT_RUN_REQ   (1 << 1)
#define EVT_STOP_REQ  (1 << 2)

// Safe state transition lock helpers.
static inline bool StateLock()   { return xSemaphoreTake(gStateMtx, portMAX_DELAY) == pdTRUE; }
static inline void StateUnlock() { xSemaphoreGive(gStateMtx); }

/// Maximum number of simultaneously active heater outputs in advanced mode.
#define MAX_ACTIVE 4

/// Planner preference: favor equivalent resistances >= target when possible.
#define PREF_ABOVE true
// ===== Fan control thresholds (°C) and timing =====
#define HS_FAN_ON_C         45.0f   // start ramp for heatsink
#define HS_FAN_FULL_C       75.0f   // full speed by this temp
#define HS_FAN_OFF_C        40.0f   // off below (hysteresis)

#define CAP_FAN_ON_C        45.0f   // start ramp for capacitor/board
#define CAP_FAN_FULL_C      70.0f   // full speed by this temp
#define CAP_FAN_OFF_C       40.0f   // off below (hysteresis)

#define FAN_MIN_RUN_PCT     18      // some 12/24V fans need a kick to spin
#define FAN_CMD_DEADBAND_PCT 2      // ignore tiny % changes to avoid chatter
#define FAN_CTRL_PERIOD_MS  500     // loop period (0.5 s)

// -----------------------------------------------------------------------------
// Enumerations
// -----------------------------------------------------------------------------

/**
 * @brief Operating recharge strategies for the device.
 */
enum class RechargeMode : uint8_t {
    BatchRecharge = 0,  ///< Relay off during pulses, then on until voltage threshold is reached.
    KeepRecharge  = 1   ///< Maintain a restricted recharge path during pulses.
};

// -----------------------------------------------------------------------------
// Device Class
// -----------------------------------------------------------------------------

/**
 * @class Device
 * @brief Central controller for power path, heaters, sensing, and loop control.
 *
 * High-level responsibilities:
 *  - Manage state machine (Idle, Running, Fault, etc.).
 *  - Coordinate power path (relay, bypass MOSFET, discharge).
 *  - Start/stop:
 *      - main loop task,
 *      - temperature monitoring,
 *      - thermal integration task (history-based),
 *      - LED / indicator task.
 *  - Execute sequential / advanced heater loops using:
 *      - HeaterManager for mask control,
 *      - planner helpers for group selection,
 *      - thermal model outputs for safety decisions.
 */
class Device {
public:
    // -------------------------------------------------------------------------
    // Singleton Interface
    // -------------------------------------------------------------------------

    static void Init(TempSensor* temp,
                     CurrentSensor* current,
                     Relay* relay,
                     CpDischg* discharger,
                     Indicator* ledIndicator);

    static Device* Get();

    /// Public singleton pointer (legacy access).
    static Device* instance;

    // -------------------------------------------------------------------------
    // Construction & Lifecycle
    // -------------------------------------------------------------------------

    Device(TempSensor* temp,
           CurrentSensor* current,
           Relay* relay,
           CpDischg* discharger,
           Indicator* ledIndicator);

    void begin();                     ///< System startup and initialization.
    void StartLoop();                 ///< Main heating loop (sequential/advanced).
    void shutdown();                  ///< Safe shutdown and discharge.
    void checkAllowedOutputs();       ///< Refresh allowedOutputs[] from config + thermal.

    // -------------------------------------------------------------------------
    // Mode Control
    // -------------------------------------------------------------------------

    void setRechargeMode(RechargeMode mode) { rechargeMode = mode; }

    // -------------------------------------------------------------------------
    // RTOS Task Management
    // -------------------------------------------------------------------------

    void startLoopTask();                       ///< Start main loop as FreeRTOS task.
    static void loopTaskWrapper(void* param);   ///< Static wrapper for loopTask().
    void loopTask();                            ///< Top-level state machine / dispatcher.

    void startTemperatureMonitor();             ///< Start DS18B20 monitoring task.
    static void monitorTemperatureTask(void*);  ///< DS18B20 monitor task entry.
    void stopTemperatureMonitor();              ///< Stop DS18B20 monitoring.

    void startThermalTask();                    ///< Start history-based thermal integration.
    static void thermalTaskWrapper(void*);      ///< Thermal task wrapper.
    void thermalTask();                         ///< Thermal integration loop.

    void stopLoopTask();                        ///< Stop main loop task.

    void updateLed();                           ///< Update LEDs / indicators.
    static void LedUpdateTask(void* param);     ///< LED / status task.

    // -------------------------------------------------------------------------
    // Power & Safety Utilities
    // -------------------------------------------------------------------------

    bool is12VPresent() const;                  ///< Check 12 V supply presence.
    void handle12VDrop();                       ///< Emergency shutdown on 12 V loss.

    /**
     * @brief Delay while monitoring power and STOP requests.
     *
     * @param ms Maximum delay in milliseconds.
     * @return false if aborted due to power loss or STOP, true otherwise.
     */
    bool delayWithPowerWatch(uint32_t ms);

    // -------------------------------------------------------------------------
    // Thermal Model Interface (history-based)
// -------------------------------------------------------------------------

    void initWireThermalModelOnce();            ///< Initialize virtual wire states.
    float wireResistanceAtTemp(uint8_t idx, float T) const;
    uint16_t getActiveMaskFromHeater() const;   ///< Convenience: read current mask.
    void calibrateIdleCurrent();                ///< Measure & store baseline idle current.

    /**
     * @brief Integrate thermal model using CurrentSensor + HeaterManager history.
     *
     * This is called exclusively from thermalTask() and:
     *  - consumes new current samples (getHistorySince),
     *  - consumes new output-mask events (getOutputHistorySince),
     *  - updates wireThermal[], lockouts, and HeaterManager temperatures.
     */
    void updateWireThermalFromHistory();
    // ---------------------------------------------------------------------
    // Over-current fault handling
    // ---------------------------------------------------------------------

    /**
     * @brief Handle a latched over-current: shut down all power paths safely.
     *
     * This is called automatically from delayWithPowerWatch() when the
     * CurrentSensor reports an over-current latch.
     */
    void handleOverCurrentFault();
    // Fan control task
    void startFanControlTask();
    void stopFanControlTask();
    static void fanControlTask(void* param);

    // -------------------------------------------------------------------------
    // Subsystem References
    // -------------------------------------------------------------------------

    TempSensor*    tempSensor         = nullptr;
    CurrentSensor* currentSensor      = nullptr;
    Relay*         relayControl       = nullptr;
    CpDischg*      discharger         = nullptr;
    Indicator*     indicator          = nullptr;

    // -------------------------------------------------------------------------
    // State and Configuration
    // -------------------------------------------------------------------------

    volatile DeviceState  currentState   = DeviceState::Idle;
    volatile RechargeMode rechargeMode   = RechargeMode::BatchRecharge;
    bool                  allowedOutputs[10] = { false };

    // -------------------------------------------------------------------------
    // RTOS Task Handles
    // -------------------------------------------------------------------------

    TaskHandle_t loopTaskHandle         = nullptr;
    TaskHandle_t tempMonitorTaskHandle  = nullptr;
    TaskHandle_t ledTaskHandle          = nullptr;
    TaskHandle_t thermalTaskHandle      = nullptr;
    TaskHandle_t fanTaskHandle          = nullptr;

private:
    // ---------------------------------------------------------------------
    // Virtual Wire Thermal Model
    // ---------------------------------------------------------------------

    struct WireThermalState {
        float     T;                 ///< Last estimated temperature [°C].
        uint32_t  lastUpdateMs;      ///< Last integration time.
        float     R0;                ///< Cold resistance [Ω].
        float     C_th;              ///< Thermal capacity [J/K].
        float     tau;               ///< Thermal time constant [s].
        bool      locked;            ///< Overtemperature lockout flag.
        uint32_t  cooldownReleaseMs; ///< Earliest ms to allow re-enable.
    };

    static constexpr float   WIRE_T_MAX_C          = 150.0f;
    static constexpr float   WIRE_T_REENABLE_C     = 140.0f;
    static constexpr float   NICHROME_CP_J_PER_KG  = 450.0f;
    static constexpr float   NICHROME_ALPHA        = 0.00017f;
    static constexpr float   DEFAULT_TAU_SEC       = 1.5f;
    static constexpr uint32_t LOCK_MIN_COOLDOWN_MS = 500;

    WireThermalState wireThermal[HeaterManager::kWireCount];

    float    ambientC            = 25.0f;
    bool     thermalInitDone     = false;
    uint32_t lastAmbientUpdateMs = 0;

    // Baseline current (non-heater loads), set by calibrateIdleCurrent().
    float    idleCurrentA        = 0.0f;

    // History cursors for incremental integration.
    uint32_t currentHistorySeq   = 0;   ///< Last consumed CurrentSensor seq.
    uint32_t outputHistorySeq    = 0;   ///< Last consumed HeaterManager seq.

    // Last known heater mask (for continuity between thermal updates).
    uint16_t lastHeaterMask      = 0;
    uint8_t lastCapFanPct = 0;
    uint8_t lastHsFanPct  = 0;
    // Internal helpers
    void updateAmbientFromSensors(bool force = false);
    void waitForWiresNearAmbient(float tolC, uint32_t maxWaitMs = 0);
};

// -----------------------------------------------------------------------------
// Global Accessor
// -----------------------------------------------------------------------------

#define DEVICE Device::Get()

#endif // DEVICE_H
