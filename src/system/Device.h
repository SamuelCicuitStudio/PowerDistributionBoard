/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef DEVICE_H
#define DEVICE_H



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
 *          - clamping at 150Â°C and re-enable hysteresis.
 *      - Publishes results via HeaterManager::setWireEstimatedTemp().
 *
 *  - Device loop (sequential / advanced):
 *      - Uses checkAllowedOutputs() (config + thermal lockout) to determine
 *        eligible wires.
 *      - In SEQUENTIAL mode:
 *          - Picks the coolest eligible wire (virtual temperature),
 *            drives it alone for onTime, idles for offTime.
 *      - In ADVANCED mode:
 *          - Syncs WireStateModel with HeaterManager + WireConfigStore.
 *          - Chooses a mask via WirePlanner (target resistance) and applies it
 *            through WireActuator/WireSafetyPolicy.
 *          - Pulses the applied mask for onTime / offTime.
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

#include <Arduino.h>
#include "control/HeaterManager.h"
#include "control/FanManager.h"
#include "sensing/TempSensor.h"
#include "sensing/CurrentSensor.h"
#include "io/Relay.h"
#include "control/CpDischg.h"
#include "control/Indicator.h"
#include "comms/WiFiManager.h"
#include "services/NVSManager.h"
#include "system/Utils.h"
#include "control/Buzzer.h"
#include "services/PowerTracker.h"
#include "system/WireSubsystem.h"
#include "sensing/BusSampler.h"
// -----------------------------------------------------------------------------
// Constants and Macros
// -----------------------------------------------------------------------------

/// Voltage ratio required on the DC bus before enabling full-power operation.
#define GO_THRESHOLD_RATIO 80
#define SAMPLINGSTALL false
// Set to 1 to bypass presence checks and treat all wires as available.
#define DEVICE_FORCE_ALL_WIRES_PRESENT 0

// -----------------------------------------------------------------------------
// Loop Mode Selection (compile-time)
// -----------------------------------------------------------------------------

// Runtime-selectable loop mode (persisted in NVS; see LOOP_MODE_KEY)
enum class LoopMode : uint8_t {
    Advanced  = 0,
    Sequential = 1
};

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

// ===== Fan control thresholds (Â°C) and timing =====
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
    friend class DeviceTransport;

    // Snapshot used by other modules to observe state changes.
    struct StateSnapshot {
        DeviceState state;
        uint32_t    sinceMs;
        uint32_t    seq;
    };

    enum class DevCmdType : uint8_t {
        SET_LED_FEEDBACK,
        SET_ON_TIME_MS,
        SET_OFF_TIME_MS,
        SET_DESIRED_VOLTAGE,
        SET_AC_FREQ,
        SET_CHARGE_RES,
        SET_DC_VOLT,
        SET_ACCESS_FLAG,
        SET_WIRE_RES,
        SET_TARGET_RES,
        SET_WIRE_OHM_PER_M,
        SET_WIRE_GAUGE,
        SET_BUZZER_MUTE,
        SET_MANUAL_MODE,
        SET_COOLING_PROFILE,
        SET_LOOP_MODE,
        SET_CURR_LIMIT,
        SET_RELAY,
        SET_OUTPUT,
        SET_FAN_SPEED,
        REQUEST_RESET
    };

    struct DevCommand {
        DevCmdType type;
        uint32_t   id;
        int32_t    i1 = 0;
        float      f1 = 0.0f;
        bool       b1 = false;
    };

    struct DevCommandAck {
        DevCmdType type;
        uint32_t   id;
        bool       success;
    };

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
    bool dischargeCapBank(float thresholdV = 5.0f, uint8_t maxRounds = 3);

    // -------------------------------------------------------------------------
    // State access helpers
    // -------------------------------------------------------------------------

    DeviceState      getState() const;
    StateSnapshot    getStateSnapshot() const;
    void             setState(DeviceState next);
    bool             waitForStateEvent(StateSnapshot& out, TickType_t toTicks);
    bool             submitCommand(DevCommand& cmd);
    bool             waitForCommandAck(DevCommandAck& ack, TickType_t toTicks);
    void             prepareForDeepSleep();

    // -------------------------------------------------------------------------
    // Thermal Model Interface (history-based)
// -------------------------------------------------------------------------

    void initWireThermalModelOnce();            ///< Initialize virtual wire states.
    float wireResistanceAtTemp(uint8_t idx, float T) const;
    uint16_t getActiveMaskFromHeater() const;   ///< Convenience: read current mask.
    void calibrateIdleCurrent();                ///< Measure & store baseline idle current.
    bool calibrateCapVoltageGain();             ///< Calibrate empirical cap voltage gain using current sensor.
    bool calibrateCapacitance();                ///< Calibrate capacitor bank capacitance by timed discharge.
    bool runCalibrationsStandalone(uint32_t timeoutMs = 10000); ///< Full calibration without starting loop.

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

    // ---------------------------------------------------------------------
    // Wire subsystem public accessors (for HeaterManager / Transport / WiFi)
    // ---------------------------------------------------------------------
    WireConfigStore&      getWireConfigStore()      { return wireConfigStore; }
    WireStateModel&       getWireStateModel()       { return wireStateModel; }
    WireThermalModel&     getWireThermalModel()     { return wireThermalModel; }
    WirePresenceManager&  getWirePresenceManager()  { return wirePresenceManager; }
    WirePlanner&          getWirePlanner()          { return wirePlanner; }
    WireTelemetryAdapter& getWireTelemetryAdapter() { return wireTelemetryAdapter; }
    float                 getCapBankCapF() const    { return capBankCapF; }

private:
    // Centralized hook for state transitions.
    void onStateChanged(DeviceState prev, DeviceState next);
    bool pushStateEvent(const StateSnapshot& snap);
    void startCommandTask();
    static void commandTask(void* param);
    void handleCommand(const DevCommand& cmd);

    // -------------------------------------------------------------------------
    // -------------------------------------------------------------------------
    // RTOS Task Handles
    // -------------------------------------------------------------------------

    TaskHandle_t loopTaskHandle         = nullptr;
    TaskHandle_t tempMonitorTaskHandle  = nullptr;
    TaskHandle_t ledTaskHandle          = nullptr;
    TaskHandle_t thermalTaskHandle      = nullptr;
    TaskHandle_t fanTaskHandle          = nullptr;
    // ---------------------------------------------------------------------
    // Virtual Wire Thermal Model
    // ---------------------------------------------------------------------

    struct WireThermalState {
        float     T;                 ///< Last estimated temperature [Â°C].
        uint32_t  lastUpdateMs;      ///< Last integration time.
        float     R0;                ///< Cold resistance [Î©].
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
    static constexpr float   PHYSICAL_HARD_MAX_C   = 90.0f;   ///< Hard cutoff from real sensors
    static constexpr uint32_t AMBIENT_UPDATE_INTERVAL_MS = 1000; ///< Faster ambient tracking
    static constexpr float   AMBIENT_MAX_STEP_C    = 15.0f;   ///< Clamp ambient jumps
    static constexpr uint32_t NO_CURRENT_SAMPLE_TIMEOUT_MS = 750; ///< Watchdog for stalled current sampling

    static constexpr float   COOLING_SCALE_AIR      = 1.0f;   ///< Natural convection/radiation in free air
    static constexpr float   COOLING_SCALE_BURIED   = DEFAULT_COOLING_SCALE_BURIED;  ///< Slower cooling when embedded (floor)

    WireThermalState wireThermal[HeaterManager::kWireCount];

    float    ambientC            = 25.0f;
    bool     thermalInitDone     = false;
    uint32_t lastAmbientUpdateMs = 0;

    // Baseline current (non-heater loads), set by calibrateIdleCurrent().
    float    idleCurrentA        = 0.0f;

    // Capacitor bank capacitance (Farads), set by calibrateCapacitance().
    float    capBankCapF         = DEFAULT_CAP_BANK_CAP_F;

    // History cursors for incremental integration.
    uint32_t currentHistorySeq   = 0;   ///< Last consumed CurrentSensor seq.
    uint32_t voltageHistorySeq   = 0;   ///< Last consumed voltage seq (legacy/fallback).
    uint32_t busHistorySeq       = 0;   ///< Last consumed BusSampler seq.
    uint32_t outputHistorySeq    = 0;   ///< Last consumed HeaterManager seq.
    uint32_t lastCurrentSampleMs = 0;   ///< Timestamp of last current sample seen

    // State and Configuration
    volatile DeviceState  currentState   = DeviceState::Idle;
    volatile RechargeMode rechargeMode   = RechargeMode::KeepRecharge;
    uint32_t              stateSeq       = 0;
    uint32_t              stateSinceMs   = 0;
    uint32_t              cmdSeq         = 0;
    bool                  allowedOutputs[10] = { false };
    QueueHandle_t         stateEvtQueue  = nullptr;
    QueueHandle_t         cmdQueue       = nullptr;
    QueueHandle_t         ackQueue       = nullptr;
    TaskHandle_t          cmdTaskHandle  = nullptr;
    bool                  manualMode     = false;

    // Last known heater mask (for continuity between thermal updates).
    uint16_t lastHeaterMask      = 0;
    uint8_t lastCapFanPct = 0;
    uint8_t lastHsFanPct  = 0;
    LoopMode loopModeSetting     = LoopMode::Advanced;
    bool     coolingFastProfile  = DEFAULT_COOLING_PROFILE_FAST;
    float    coolingScale        = COOLING_SCALE_AIR;
    float    coolingKCold        = DEFAULT_COOL_K_COLD;
    float    coolingMaxDropC     = DEFAULT_MAX_COOL_DROP_C;
    float    coolingBuriedScale  = DEFAULT_COOLING_SCALE_BURIED;

    // ---------------------------------------------------------------------
    // Wire subsystem helpers (config + runtime + planner + telemetry)
    // ---------------------------------------------------------------------
    WireConfigStore      wireConfigStore;
    WireStateModel       wireStateModel;
    WireThermalModel     wireThermalModel;
    WirePresenceManager  wirePresenceManager;
    WirePlanner          wirePlanner;
    WireTelemetryAdapter wireTelemetryAdapter;
    BusSampler*          busSampler = nullptr;
    // Internal helpers
    void syncWireRuntimeFromHeater();
    void updateAmbientFromSensors(bool force = false);
    void waitForWiresNearAmbient(float tolC, uint32_t maxWaitMs = 0);
    void loadRuntimeSettings();
    void applyCoolingProfile(bool fastProfile);
};

// -----------------------------------------------------------------------------
// Global Accessor
// -----------------------------------------------------------------------------

#define DEVICE Device::Get()

#endif // DEVICE_H
