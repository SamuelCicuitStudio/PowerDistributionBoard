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
 * nichrome heater drive using a fast warm-up + equilibrium scheduler.
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
 *          - first-order thermal model using tau/k/C,
 *          - heating based on measured I(t), active mask, and R(T),
 *          - clamping at 150 C and re-enable hysteresis.
 *      - Publishes results via HeaterManager::setWireEstimatedTemp().
 *
 *  - Device loop (fast warm-up + equilibrium):
 *      - Uses checkAllowedOutputs() (config + thermal lockout) to determine
 *        eligible wires.
 *      - Allocates one energy packet per wire per frame (full voltage when ON).
 *      - Boost phase breaks plateaus, hold phase maintains the target.
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
#include <HeaterManager.hpp>
#include <FanManager.hpp>
#include <TempSensor.hpp>
#include <CurrentSensor.hpp>
#include <Relay.hpp>
#include <CpDischg.hpp>
#include <Indicator.hpp>
#include <WiFiManager.hpp>
#include <NVSManager.hpp>
#include <Utils.hpp>
#include <Buzzer.hpp>
#include <PowerTracker.hpp>
#include <WireSubsystem.hpp>
#include <WirePresenceManager.hpp>
#include <BusSampler.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
// -----------------------------------------------------------------------------
// Constants and Macros
// -----------------------------------------------------------------------------

/// Voltage ratio required on the DC bus before enabling full-power operation.
#define GO_THRESHOLD_RATIO 80
#define SAMPLINGSTALL false
// Set to 1 to bypass presence checks and treat all wires as available.
#define DEVICE_FORCE_ALL_WIRES_PRESENT 0

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

// ===== Fan control thresholds (C) and timing =====
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
 *  - Execute fast warm-up + equilibrium heating using:
 *      - HeaterManager for mask control,
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

    struct LastEventInfo {
        bool     hasError = false;
        uint32_t errorMs = 0;
        uint32_t errorEpoch = 0;
        char     errorReason[96] = {0};
        bool     hasStop = false;
        uint32_t stopMs = 0;
        uint32_t stopEpoch = 0;
        char     stopReason[96] = {0};
    };

    enum class EventKind : uint8_t {
        Error = 1,
        Warning = 2
    };

    struct EventEntry {
        EventKind kind = EventKind::Error;
        uint32_t  ms = 0;
        uint32_t  epoch = 0;
        char      reason[96] = {0};
    };

    struct EventNotice {
        EventKind kind = EventKind::Error;
        uint32_t  ms = 0;
        uint32_t  epoch = 0;
        uint8_t   unreadWarn = 0;
        uint8_t   unreadErr = 0;
        char      reason[96] = {0};
    };

    enum class DevCmdType : uint8_t {
        SET_LED_FEEDBACK,
        SET_AC_FREQ,
        SET_CHARGE_RES,
        SET_ACCESS_FLAG,
        SET_WIRE_RES,
        SET_WIRE_OHM_PER_M,
        SET_WIRE_GAUGE,
        SET_BUZZER_MUTE,
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
    void StartLoop();                 ///< Main heating loop (fast warm-up + equilibrium).
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
    void             setLastErrorReason(const char* reason);
    void             addWarningReason(const char* reason);
    void             setLastStopReason(const char* reason);
    LastEventInfo     getLastEventInfo() const;
    size_t           getEventHistory(EventEntry* out, size_t maxOut) const;
    size_t           getErrorHistory(EventEntry* out, size_t maxOut) const;
    size_t           getWarningHistory(EventEntry* out, size_t maxOut) const;
    void             getUnreadEventCounts(uint8_t& warnCount, uint8_t& errCount) const;
    void             markEventHistoryRead();
    bool             waitForEventNotice(EventNotice& out, TickType_t toTicks);

    // -------------------------------------------------------------------------
    // Thermal Model Interface (history-based)
// -------------------------------------------------------------------------

    void initWireThermalModelOnce();            ///< Initialize virtual wire states.
    float wireResistanceAtTemp(uint8_t idx, float T) const;
    uint16_t getActiveMaskFromHeater() const;   ///< Convenience: read current mask.
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

    enum class EnergyRunPurpose : uint8_t {
        None = 0,
        WireTest = 1,
        ModelCal = 2,
        NtcCal = 3,
        FloorCal = 4
    };

    struct WireTargetStatus {
        bool     active      = false;
        EnergyRunPurpose purpose = EnergyRunPurpose::None;
        float    targetC     = NAN;
        float    ntcTempC    = NAN;
        float    activeTempC = NAN;
        float    dutyFrac    = 1.0f;
        uint8_t  activeWire  = 0;
        uint32_t packetMs    = 0;
        uint32_t frameMs     = 0;
        uint32_t updatedMs   = 0;
    };

    struct FloorControlStatus {
        bool     active      = false;
        float    targetC     = NAN;
        float    tempC       = NAN;
        float    wireTargetC = NAN;
        uint32_t updatedMs   = 0;
    };

    struct LoopTargetStatus {
        bool     active      = false;
        float    targetC     = NAN;
        uint32_t updatedMs   = 0;
    };

    struct AmbientWaitStatus {
        bool     active   = false;
        float    tolC     = NAN;
        uint32_t sinceMs  = 0;
        char     reason[16] = {0};
    };

    bool startWireTargetTest(float targetC, uint8_t wireIndex = 0);
    void stopWireTargetTest();
    WireTargetStatus getWireTargetStatus() const;
    FloorControlStatus getFloorControlStatus() const;
    LoopTargetStatus getLoopTargetStatus() const;
    AmbientWaitStatus getAmbientWaitStatus() const;
    bool confirmWiresCool();
    bool consumeWiresCoolConfirmation();
    bool isWiresCoolConfirmed() const;
    bool probeWirePresence();

    bool startEnergyCalibration(float targetC,
                                uint8_t wireIndex,
                                EnergyRunPurpose purpose,
                                float dutyFrac = 1.0f);
    void startControlTask();
    static void controlTaskWrapper(void* param);
    void controlTask();

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
    TaskHandle_t controlTaskHandle      = nullptr;
    TaskHandle_t wireTestTaskHandle     = nullptr;
    // ---------------------------------------------------------------------
    // Virtual Wire Thermal Model
    // ---------------------------------------------------------------------

    struct WireThermalState {
        float     T;                 ///< Last estimated temperature [C].
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
    static constexpr float   DEFAULT_TAU_SEC       = 35.0f;
    static constexpr uint32_t LOCK_MIN_COOLDOWN_MS = 500;
    static constexpr float   PHYSICAL_HARD_MAX_C   = 90.0f;   ///< Hard cutoff from real sensors
    static constexpr uint32_t AMBIENT_UPDATE_INTERVAL_MS = 1000; ///< Faster ambient tracking
    static constexpr float   AMBIENT_MAX_STEP_C    = 15.0f;   ///< Clamp ambient jumps
    static constexpr uint32_t NO_CURRENT_SAMPLE_TIMEOUT_MS = 750; ///< Watchdog for stalled current sampling

    WireThermalState wireThermal[HeaterManager::kWireCount];

    float    ambientC            = 25.0f;
    bool     thermalInitDone     = false;
    uint32_t lastAmbientUpdateMs = 0;

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
    uint16_t              allowedOverrideMask = 0;
    QueueHandle_t         stateEvtQueue  = nullptr;
    QueueHandle_t         eventEvtQueue  = nullptr;
    QueueHandle_t         cmdQueue       = nullptr;
    QueueHandle_t         ackQueue       = nullptr;
    TaskHandle_t          cmdTaskHandle  = nullptr;
    SemaphoreHandle_t     eventMtx       = nullptr;
    char                  lastErrorReason[96] = {0};
    uint32_t              lastErrorMs    = 0;
    uint32_t              lastErrorEpoch = 0;
    char                  lastStopReason[96] = {0};
    uint32_t              lastStopMs     = 0;
    uint32_t              lastStopEpoch  = 0;
    static constexpr size_t kEventHistorySize = 10;
    EventEntry            eventHistory[kEventHistorySize]{};
    uint8_t               eventHead = 0;
    uint8_t               eventCount = 0;
    EventEntry            errorHistory[kEventHistorySize]{};
    uint8_t               errorHistoryHead = 0;
    uint8_t               errorHistoryCount = 0;
    EventEntry            warnHistory[kEventHistorySize]{};
    uint8_t               warnHistoryHead = 0;
    uint8_t               warnHistoryCount = 0;
    uint8_t               unreadWarn = 0;
    uint8_t               unreadErr = 0;
    bool                  tempWarnLatched = false;

    // Last known heater mask (for continuity between thermal updates).
    uint16_t lastHeaterMask      = 0;
    uint8_t lastCapFanPct = 0;
    uint8_t lastHsFanPct  = 0;
    // ---------------------------------------------------------------------
    // Wire subsystem helpers (config + runtime + telemetry)
    // ---------------------------------------------------------------------
    WireConfigStore      wireConfigStore;
    WireStateModel       wireStateModel;
    WireThermalModel     wireThermalModel;
    WireTelemetryAdapter wireTelemetryAdapter;
    WirePresenceManager  wirePresenceManager;
    BusSampler*          busSampler = nullptr;

    SemaphoreHandle_t    controlMtx = nullptr;
    WireTargetStatus     wireTargetStatus{};
    FloorControlStatus   floorControlStatus{};
    LoopTargetStatus     loopTargetStatus{};
    AmbientWaitStatus    ambientWaitStatus{};
    bool                 wiresCoolConfirmed = false;
    uint32_t             wiresCoolConfirmMs = 0;
    void updateWireTestStatus(uint8_t wireIndex,
                              uint32_t packetMs,
                              uint32_t frameMs);
    // Internal helpers
    void syncWireRuntimeFromHeater();
    void updateAmbientFromSensors(bool force = false);
    void waitForWiresNearAmbient(float tolC, uint32_t maxWaitMs = 0,
                                 const char* reason = nullptr);
    void setAmbientWaitStatus(bool active, float tolC, const char* reason);
    void loadRuntimeSettings();
    void applyWireModelParamsFromNvs();
    bool pushEventNotice(const EventNotice& note);
    static void pushEventUnlocked(Device* self,
                                  EventKind kind,
                                  const char* reason,
                                  uint32_t nowMs,
                                  uint32_t epoch);
};

// -----------------------------------------------------------------------------
// Global Accessor
// -----------------------------------------------------------------------------

#define DEVICE Device::Get()

#endif // DEVICE_H
