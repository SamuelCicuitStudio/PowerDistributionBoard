/**
 * @brief Main heating loop entry point.
 *
 * This function runs the core heater sequencing logic once the device has
 * transitioned to DeviceState::Running. It uses the shared thermal model,
 * current sensor feedback, and event flags to safely drive the nichrome wires
 * in one of two modes:
 *
 * - DEVICE_LOOP_MODE_SEQUENTIAL
 * - DEVICE_LOOP_MODE_ADVANCED (Batch / grouped drive)
 *
 * Common behavior (both modes):
 * - Blocks on entry with waitForWiresNearAmbient() to ensure all virtual wire
 *   temperatures are close to the current ambient before starting a new cycle.
 *   This keeps the thermal model consistent across multiple runs / re-arms.
 * - Starts background temperature monitoring (DS18B20) and enables the bypass
 *   MOSFET once the system is ready.
 * - Uses checkAllowedOutputs(), which combines configuration flags and
 *   thermal lockout state, so overheated wires (>= 150°C) are automatically
 *   excluded from scheduling until they cool below the re-enable threshold.
 * - Uses delayWithPowerWatch() in all timing phases to:
 *     - Abort immediately on 12V loss (handle12VDrop()).
 *     - React to STOP requests via the event group.
 * - The actual per-wire temperatures are not computed here but continuously
 *   maintained by LedUpdateTask() → applyHeatingFromCapture(), which:
 *     - Reads total current from ACS781.
 *     - Distributes power across active wires using R(T) and the parallel
 *       conductance model.
 *     - Applies heating, cooling toward ambient, 150°C clamp, and lockout.
 *
 * Sequential mode (DEVICE_LOOP_MODE_SEQUENTIAL):
 * - Only one heater output is driven at a time.
 * - For each activation step:
 *     - Re-evaluates allowedOutputs[] via checkAllowedOutputs().
 *     - Selects the COOLEST eligible wire using its virtual temperature
 *       (getWireEstimatedTemp), not simple round-robin.
 *     - Turns that wire ON for onTime, then OFF for offTime, both guarded by
 *       delayWithPowerWatch().
 * - This "coolest-first" strategy, combined with the thermal model and
 *   lockout, naturally equalizes wire usage and prevents any wire from
 *   exceeding the safe band around 150°C.
 *
 * Advanced mode (DEVICE_LOOP_MODE_ADVANCED):
 * - Drives multiple wires in parallel as groups to approximate a target
 *   equivalent resistance (R_eq) for batch recharge / discharge cycles.
 * - On each cycle:
 *     - Updates allowedOutputs[] (config + thermal safety).
 *     - Builds an allowedMask and computes an activation plan where each
 *       group is a bitmask of wires chosen to match the desired R_eq subject
 *       to max active outputs.
 *     - For each group:
 *         - Enables all wires in the mask simultaneously.
 *         - Holds them for onTime / offTime via delayWithPowerWatch().
 * - Between group sequences, the loop can wait for capacitor voltage recovery
 *   while keeping the device in Running state. Thermal lockout and the
 *   current-driven model ensure that overheated wires are skipped
 *   automatically and cooler wires are preferentially used.
 *
 * On exit:
 * - stopTemperatureMonitor() is called.
 * - All heater outputs and indicator LEDs are forced OFF/cleared.
 * - Control returns to the higher-level state machine in loopTask(), which
 *   may transition the device back to Idle/Shutdown or restart a new cycle.
 */
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
// LOOP MODE SWITCH (compile-time)
// -----------------------------------------------------------------------------
/*
 * Choose the behavior of Device::StartLoop():
 *
 * DEVICE_LOOP_MODE_ADVANCED   → keeps existing behavior (multi-output plan + recharge)
 * DEVICE_LOOP_MODE_SEQUENTIAL → new simple loop: toggles one allowed output at a time
 *                               using saved ON_TIME / OFF_TIME.
 *
 * To use the simple loop, set:
 *     #define DEVICE_LOOP_MODE DEVICE_LOOP_MODE_SEQUENTIAL
 * in this header (below), then rebuild.
 */
#define DEVICE_LOOP_MODE_ADVANCED   0
#define DEVICE_LOOP_MODE_SEQUENTIAL 1
#define DEVICE_LOOP_MODE DEVICE_LOOP_MODE_ADVANCED  // default preserves current behavior


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

#define MAX_ACTIVE 4
#define PREF_ABOVE true

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
    static void Init(TempSensor* temp,
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
    Device(TempSensor* temp,
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
    bool is12VPresent() const;
    void handle12VDrop();                // emergency stop when 12V is lost
    bool delayWithPowerWatch(uint32_t ms);  // like delay(), but aborts if 12V disappears
    void initWireThermalModelOnce();
    float wireResistanceAtTemp(uint8_t idx, float T) const;
    void updateWireCoolingAll();
    uint16_t getActiveMaskFromHeater() const;
    void applyHeatingFromCapture();
    void calibrateIdleCurrent();           ///< Measure & store idle current baseline

    // -------------------------------------------------------------------------
    // Subsystem References
    // -------------------------------------------------------------------------
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
    // ---- Wire temperature estimation model ----
    struct WireThermalState {
        float     T;                 // last estimated temperature [°C]
        uint32_t  lastUpdateMs;      // last time this wire was updated
        float     R0;                // cold resistance [Ω]
        float     C_th;              // thermal capacity [J/K] = m * cp
        float     tau;               // thermal time constant [s]
        bool      locked;            // true if hit T_MAX
        uint32_t  cooldownReleaseMs; // optional time-based hysteresis
    };

    static constexpr float WIRE_T_MAX_C        = 150.0f;
    static constexpr float WIRE_T_REENABLE_C   = 140.0f;
    static constexpr float NICHROME_CP_J_PER_KG = 450.0f;      // DS / literature
    static constexpr float NICHROME_ALPHA      = 0.00017f;     // dR/R per °C (tune!)
    static constexpr float DEFAULT_TAU_SEC     = 1.5f;         // tune per design
    static constexpr uint32_t LOCK_MIN_COOLDOWN_MS = 500;      // small safety margin

    WireThermalState wireThermal[HeaterManager::kWireCount];
    float ambientC          = 25.0f;
    bool  thermalInitDone   = false;
    uint32_t lastAmbientUpdateMs = 0;   // <--- add this

    // For associating active masks with captured current samples.
    uint16_t captureMasks[CURRENT_CAPTURE_MAX_SAMPLES];
    void updateAmbientFromSensors(bool force = false); // <--- add this
    void waitForWiresNearAmbient(float tolC, uint32_t maxWaitMs = 0);


};

// -----------------------------------------------------------------------------
// Global Accessor Macro
// -----------------------------------------------------------------------------
#define DEVICE Device::Get()

#endif // DEVICE_H
