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

#ifndef HEATER_MANAGER_H
#define HEATER_MANAGER_H

#include "Utils.h"
#include "NVSManager.h"
#include "Config.h"  // Rxx keys, WIRE_OHM_PER_M_KEY, defaults

/**
 * Aggregated info for one heater wire.
 */
struct WireInfo {
    uint8_t index;               ///< 1..10 channel index
    float   resistanceOhm;       ///< Calibrated cold resistance [Ω]
    float   lengthM;             ///< Estimated length [m]
    float   crossSectionAreaM2;  ///< Estimated cross-section area [m²]
    float   volumeM3;            ///< Volume [m³]
    float   massKg;              ///< Mass [kg]
    float   temperatureC;        ///< Last estimated wire temperature [°C]
};

class HeaterManager {
public:
    static constexpr uint8_t kWireCount = 10;

    // ---------------------------------------------------------------------
    // Singleton access (NVS-style)
    // ---------------------------------------------------------------------

    /**
     * @brief Ensure singleton is constructed.
     *
     * Call once at boot (optional but recommended), e.g.:
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
     * @brief Initialize hardware + wire model (idempotent).
     *
     * - Create mutex (once)
     * - Configure ENAxx pins as outputs (all OFF)
     * - Load:
     *      - global Ω/m (WIRE_OHM_PER_M_KEY)
     *      - per-wire resistance R01..R10
     *      - target resistance (R0XTGT_KEY)
     * - Precompute geometry for each wire
     */
    void begin();

    // ---------------------------------------------------------------------
    // Output control
    // ---------------------------------------------------------------------

    /** Enable or disable one of the 10 outputs (1..10). Thread-safe. */
    void setOutput(uint8_t index, bool enable);

    /** Disable ALL outputs immediately. Thread-safe. */
    void disableAll();

    /** Return current digital state of ENA pin (true if HIGH). Thread-safe. */
    bool getOutputState(uint8_t index) const;

    // ---------------------------------------------------------------------
    // Wire resistance / target configuration
    // ---------------------------------------------------------------------

    /** Cache + persist a single wire resistance (Ω) for channel 1..10. Thread-safe. */
    void setWireResistance(uint8_t index, float ohms);

    /** Get cached wire resistance (Ω); returns 0.0f if index is invalid. */
    float getWireResistance(uint8_t index) const;

    /** Set global target resistance (Ω) for all outputs. Thread-safe. */
    void setTargetResistanceAll(float ohms);

    /** Get current global target resistance (Ω). */
    float getTargetResistance() const { return targetResOhms; }

    /** Get current global wire resistivity in Ω/m. */
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
     * @brief Set last estimated temperature for a given wire (°C).
     *        (to be updated by higher-level safety logic)
     */
    void setWireEstimatedTemp(uint8_t index, float tempC);

    /**
     * @brief Get last estimated temperature (°C) for a wire,
     *        or NAN if invalid / never set.
     */
    float getWireEstimatedTemp(uint8_t index) const;

    /**
     * @brief Reset all cached temperatures to a given ambient (e.g. 25°C).
     */
    void resetAllEstimatedTemps(float ambientC);

private:
    // ---------------------------------------------------------------------
    // Singleton internals
    // ---------------------------------------------------------------------
    HeaterManager();                            // Private constructor
    HeaterManager(const HeaterManager&) = delete;
    HeaterManager& operator=(const HeaterManager&) = delete;

    static HeaterManager* s_instance;           // Singleton instance pointer

    // ---------------------------------------------------------------------
    // Material constants (nichrome, approximate)
    // ---------------------------------------------------------------------
    static constexpr float NICHROME_RESISTIVITY   = 1.10e-6f; // Ω·m
    static constexpr float NICHROME_DENSITY       = 8400.0f;  // kg/m³
    static constexpr float NICHROME_SPECIFIC_HEAT = 450.0f;   // J/(kg·K) (reserved)

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
    WireInfo          wires[kWireCount];  ///< Per-channel wire info
    float             wireOhmPerM;        ///< Ω/m from NVS
    float             targetResOhms;      ///< Global target resistance
    bool              _initialized;       ///< begin() completed
    SemaphoreHandle_t _mutex;             ///< Protects ENA + caches

    // ---------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------
    bool lock() const;
    void unlock() const;

    void loadWireConfig();                 ///< Load Ω/m, Rxx, targetR, recompute geometry.
    void computeWireGeometry(WireInfo& w); ///< Compute length/area/volume/mass for one wire.
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
