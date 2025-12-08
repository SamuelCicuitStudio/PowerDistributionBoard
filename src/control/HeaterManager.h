/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef HEATER_MANAGER_H
#define HEATER_MANAGER_H

/**************************************************************
 *  HeaterManager.h
 * ---------------
 *  Singleton nichrome heater manager, NVS-style.
 *
 *  Usage pattern:
 *      HeaterManager::Init();      // ensure singleton constructed
 *      WIRE->begin();              // configure pins, load NVS, compute geometry
 *
 *      WIRE->setOutput(1, true);
 *      WireInfo w1 = WIRE->getWireInfo(1);
 *
 **************************************************************/

#include "system/Utils.h"
#include "services/NVSManager.h"
#include "system/Config.h"  // Rxx keys, WIRE_OHM_PER_M_KEY, defaults
#include "sensing/CurrentSensor.h"
// ---------------------------------------------------------------------
// Material constants (nichrome, approximate)
// ---------------------------------------------------------------------
#define NICHROME_RESISTIVITY    1.10e-6f // Î©Â·m
#define NICHROME_DENSITY        8400.0f  // kg/mÂ³
#define NICHROME_SPECIFIC_HEAT  450.0f   // J/(kgÂ·K) (reserved)

class Device;
/**
 * @brief Aggregated information for one heater wire.
 */
struct WireInfo {
    uint8_t index;               ///< 1..10 channel index
    float   resistanceOhm;       ///< Calibrated cold resistance [Î©]
    float   lengthM;             ///< Estimated length [m]
    float   crossSectionAreaM2;  ///< Estimated cross-section area [mÂ²]
    float   volumeM3;            ///< Volume [mÂ³]
    float   massKg;              ///< Mass [kg]
    float   temperatureC;        ///< Last estimated wire temperature [Â°C]
    bool    connected;           ///< true if last probe saw a plausible load
    float   presenceCurrentA;    ///< last measured current during probe [A]
};


class HeaterManager {
public:
    static constexpr uint8_t kWireCount = 10;

    // ---------------------------------------------------------------------
    // Output history (for RTOS/thermal integration)
    // ---------------------------------------------------------------------

    /**
     * @brief Output state transition event.
     *
     * Emitted whenever the effective 10-bit output mask changes.
     * Used by higher-level logic (e.g. thermal task) to reconstruct
     * which wires were active over time.
     */
    struct OutputEvent {
        uint32_t timestampMs;    ///< millis() when mask became active
        uint16_t mask;           ///< 10-bit mask (bit i => wire i+1 ON)
    };

    // Last-N transitions; small but enough, because control task
    // changes outputs relatively infrequently compared to current sampling.
    static constexpr size_t OUTPUT_HISTORY_SIZE = 128;

    // ---------------------------------------------------------------------
    // Singleton access (NVS-style)
    // ---------------------------------------------------------------------

    /**
     * @brief Ensure singleton is constructed.
     *
     * Call once at boot (recommended):
     *      HeaterManager::Init();
     *      WIRE->begin();
     */
    static void Init();

    /**
     * @brief Get global HeaterManager instance (creates on first call).
     * @return Pointer to singleton instance.
     */
    static HeaterManager* Get();

    // ---------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------

    /**
     * @brief Initialize hardware and internal wire model (idempotent).
     *
     * - Create mutex
     * - Configure ENAxx pins as outputs (all OFF)
     * - Load:
     *      - global Î©/m (WIRE_OHM_PER_M_KEY)
     *      - per-wire resistance R01..R10
     *      - target resistance (R0XTGT_KEY)
     * - Precompute geometry for each wire
     */
    void begin();

    // ---------------------------------------------------------------------
    // Output control (single-channel)
    // ---------------------------------------------------------------------

    /**
     * @brief Enable or disable a single output channel (1..10).
     *
     * Thread-safe.
     * Also updates the internal 10-bit mask and logs an OutputEvent
     * if the effective mask changes.
     */
    void setOutput(uint8_t index, bool enable);

    /**
     * @brief Disable all outputs immediately.
     *
     * Thread-safe. Also logs an OutputEvent if any channel was ON.
     */
    void disableAll();

    /**
     * @brief Get the current digital state of one ENA pin.
     *
     * @return true if HIGH (ON), false otherwise.
     * Thread-safe.
     */
    bool getOutputState(uint8_t index) const;

    // ---------------------------------------------------------------------
    // Output control (mask-based, RTOS-friendly)
    // ---------------------------------------------------------------------

    /**
     * @brief Atomically apply a full 10-bit output mask.
     *
     * Each bit i corresponds to wire (i+1).
     * Only channels whose state changes are toggled.
     *
     * Thread-safe.
     * Emits one OutputEvent if the mask actually changes.
     */
    void setOutputMask(uint16_t mask);

    /**
     * @brief Get the current 10-bit output mask.
     *
     * Bit i set => wire (i+1) is ON.
     *
     * Thread-safe.
     */
    uint16_t getOutputMask() const;

    /**
     * @brief Fetch output mask transitions since a given sequence index.
     *
     * Intended for a single consumer (e.g. thermal integration task) to
     * incrementally read output changes and correlate them with current
     * samples.
     *
     * @param lastSeq  Input: last consumed sequence value (0 for "from oldest").
     * @param out      Output array of events.
     * @param maxOut   Capacity of @p out.
     * @param newSeq   Output: updated sequence value for next call.
     *
     * @return Number of events written to @p out.
     */
    size_t getOutputHistorySince(uint32_t lastSeq,
                                 OutputEvent* out,
                                 size_t maxOut,
                                 uint32_t& newSeq) const;

    // ---------------------------------------------------------------------
    // Wire resistance / target configuration
    // ---------------------------------------------------------------------

    /** Cache + persist a single wire resistance (Î©) for channel 1..10. Thread-safe. */
    void setWireResistance(uint8_t index, float ohms);

    /** Get cached wire resistance (Î©); returns 0.0f if index is invalid. */
    float getWireResistance(uint8_t index) const;

    /** Set global target resistance (Î©) for all outputs. Thread-safe. */
    void setTargetResistanceAll(float ohms);

    /** Get current global target resistance (Î©). */
    float getTargetResistance() const { return targetResOhms; }

    /** Get current global wire resistivity in Î©/m. */
    float getWireOhmPerM() const { return wireOhmPerM; }

    // ---------------------------------------------------------------------
    // Wire info / temperature
    // ---------------------------------------------------------------------

    /**
     * @brief Get a snapshot of WireInfo for a given index (1..10).
     * @return WireInfo with index=0 if invalid.
     */
    WireInfo getWireInfo(uint8_t index) const;

    /**
     * @brief Set last estimated temperature for a given wire (Â°C).
     *
     * Typically called by the thermal model / safety logic.
     */
    void setWireEstimatedTemp(uint8_t index, float tempC);

    /**
     * @brief Get last estimated temperature (Â°C) for a wire,
     *        or NAN if invalid / never set.
     */
    float getWireEstimatedTemp(uint8_t index) const;

    /**
     * @brief Reset all cached temperatures to a given ambient (e.g. 25Â°C).
     */
    void resetAllEstimatedTemps(float ambientC);

    // ---------------------------------------------------------------------
    // Wire presence detection
    // ---------------------------------------------------------------------

    /**
     * @brief Probe each wire to determine if a load is present.
     *
     * How it works:
     *  - Saves current output mask.
     *  - For each channel:
     *      * Enables only that channel for a short pulse.
     *      * Samples current via @p cs.
     *      * Compares measured current vs expected(V/R).
     *      * Sets wires[i].connected + presenceCurrentA accordingly.
     *  - Restores the original mask at the end.
     *
     * Call when the system is IDLE / safe (no other loads toggling).
     *
     * @param cs                 CurrentSensor instance (already begin()).
     * @param busVoltage         Supply/bus voltage in volts.
     *                           If <=0, it will try DC_VOLTAGE_KEY / DESIRED_OUTPUT_VOLTAGE_KEY.
     * @param minValidFraction   Min(measured/expected) to accept as connected (e.g. 0.25).
     * @param maxValidFraction   Max(measured/expected) before treating as suspicious (e.g. 4.0).
     * @param settleMs           Wait after switching before sampling.
     * @param samples            Number of samples to average per channel.
     */
    void probeWirePresence(CurrentSensor& cs,
                           float busVoltage = 0.0f,
                           float minValidFraction = 0.25f,
                           float maxValidFraction = 4.0f,
                           uint16_t settleMs = 30,
                           uint8_t samples = 10);

    /**
     * @brief Update presence flags based on measured total current
     *        while a given mask is active.
     *
     * Use this during the main loop (e.g. from _runMaskedPulse) to
     * dynamically detect removed / open wires.
     *
     * Logic:
     *  - Compute expected total current from connected wires in @p mask.
     *  - If measured/expected < minValidRatio, mark all wires in mask as disconnected.
     */
    void updatePresenceFromMask(uint16_t mask,
                                float totalCurrentA,
                                float busVoltage = 0.0f,
                                float minValidRatio = 0.20f);

    /// @return true if at least one wire is still marked connected.
    bool hasAnyConnected() const;
    /**
     * @brief Update cached presence info for a single wire.
     */
    void setWirePresence(uint8_t index, bool connected, float presenceCurrentA);

private:
    // ---------------------------------------------------------------------
    // Singleton internals
    // ---------------------------------------------------------------------
    HeaterManager();
    HeaterManager(const HeaterManager&) = delete;
    HeaterManager& operator=(const HeaterManager&) = delete;

    static HeaterManager* s_instance;

    // ---------------------------------------------------------------------
    // Hardware mapping
    // ---------------------------------------------------------------------
    const uint8_t enaPins[kWireCount] = {
        ENA01_E_PIN, ENA02_E_PIN, ENA03_E_PIN, ENA04_E_PIN, ENA05_E_PIN,
        ENA06_E_PIN, ENA07_E_PIN, ENA08_E_PIN, ENA09_E_PIN, ENA10_E_PIN
    };

    // ---------------------------------------------------------------------
    // State
    // ---------------------------------------------------------------------
    WireInfo          wires[kWireCount];   ///< Per-channel wire info.
    float             wireOhmPerM;         ///< Î©/m from NVS.
    float             targetResOhms;       ///< Global target resistance.
    bool              _initialized;        ///< begin() completed.
    SemaphoreHandle_t _mutex;              ///< Protects ENA pins + caches.

    // Current effective 10-bit mask (bit i => wire i+1 ON).
    uint16_t          _currentMask = 0;

    // Output history ring buffer.
    OutputEvent       _history[OUTPUT_HISTORY_SIZE];
    uint32_t          _historyHead = 0;    ///< Next write index (monotonic).
    uint32_t          _historySeq  = 0;    ///< Monotonic sequence for events.

    // ---------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------
    bool lock() const;
    void unlock() const;

    void loadWireConfig();                 ///< Load Î©/m, Rxx, targetR, recompute geometry.
    void computeWireGeometry(WireInfo& w); ///< Compute length/area/volume/mass for one wire.

    /**
     * @brief Record a new output mask transition in the history buffer.
     *
     * Assumes _mutex is already held.
     * Only called when the effective mask actually changes.
     */
    void logOutputMaskChange(uint16_t newMask);
};

/**
 * @brief Convenience macro (like CONF).
 * Usage:
 *    HeaterManager::Init();
 *    WIRE->begin();
 *    WIRE->setOutput(1, true);
 */
#define WIRE HeaterManager::Get()

#endif // HEATER_MANAGER_H


