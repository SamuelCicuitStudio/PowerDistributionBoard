#ifndef CPDISCHG_H
#define CPDISCHG_H

#include <Arduino.h>
#include "HeaterManager.h"
#include "Utils.h"
 #include "Config.h"  // Include Config.h for default values
// Voltage divider setup: 470k / 4.7k = ~100:1
#define ADC_REF_VOLTAGE         3.3f
#define ADC_MAX                 4095.0f
#define VOLTAGE_DIVIDER_RATIO   100.0f  // Adjust based on actual hardware divider
#define SAFE_VOLTAGE_THRESHOLD  5.0f    // Target voltage to consider "discharged"

class CpDischg {
public:
    void begin(HeaterManager* heater);
    void discharge();  // Now blocking

private:
    HeaterManager* heaterManager = nullptr;
};

#endif // CPDISCHG_H
