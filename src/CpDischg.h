#ifndef CPDISCHG_H
#define CPDISCHG_H


#include "HeaterManager.h"
#include "Utils.h"

// ───────────────────────────────────────────────
// Voltage divider constants for capacitor sensing
// ───────────────────────────────────────────────
#define ADC_REF_VOLTAGE         3.3f
#define ADC_MAX                 4095.0f
#define VOLTAGE_DIVIDER_RATIO   100.0f   // Adjust if resistors change
#define SAFE_VOLTAGE_THRESHOLD  5.0f     // Considered discharged below this

/**
 * CpDischg
 * --------
 * Manages safe capacitor discharge by sequentially enabling heater outputs
 * to dissipate energy. Uses voltage sensing to terminate operation.
 */
class CpDischg {
public:
    explicit CpDischg(HeaterManager* heater) : heaterManager(heater) {}

    void begin();        // Optional init if needed
    void discharge();    // Blocking discharge logic

private:
    HeaterManager* heaterManager = nullptr;
};

#endif // CPDISCHG_H
