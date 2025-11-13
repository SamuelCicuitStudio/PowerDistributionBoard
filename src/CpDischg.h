#ifndef CPDISCHG_H
#define CPDISCHG_H

#include <Arduino.h>
#include "Config.h"
#include "HeaterManager.h"
#include "Relay.h"
#include "Utils.h"

// ----- ADC / Divider configuration -----
// Cal constants can be overridden from Config.h via -D or #define

// Raw ADC offset in counts (keep your existing trim)
#define ADC_OFFSET              14

#ifndef ADC_REF_VOLTAGE
#define ADC_REF_VOLTAGE         3.3f        // ADC full-scale voltage
#endif

#ifndef ADC_MAX
#define ADC_MAX                 4095.0f     // 12-bit ADC
#endif

#ifndef SAFE_VOLTAGE_THRESHOLD
#define SAFE_VOLTAGE_THRESHOLD  5.0f        // Safe "fully discharged" level
#endif

// Divider: top to HV, bottom to GND
#ifndef DIVIDER_TOP_OHMS
#define DIVIDER_TOP_OHMS        470000.0f   // 470 kΩ (top)
#endif

// *** UPDATED: bottom resistor is 3.9 kΩ (was 4.7 kΩ) ***
#ifndef DIVIDER_BOTTOM_OHMS
#define DIVIDER_BOTTOM_OHMS     3900.0f     // 3.9 kΩ (bottom)
#endif

// *** NEW: allow for op-amp gain (your MCP6001 buffer is unity) ***
#ifndef OPAMP_GAIN
#define OPAMP_GAIN              1.0f
#endif

// Overall scale factor from ADC voltage to bus voltage:
// Vbus = Vadc * ((Rtop + Rbot) / Rbot) / OPAMP_GAIN
#ifndef VOLTAGE_SCALE
#define VOLTAGE_SCALE \
    ( ((DIVIDER_TOP_OHMS + DIVIDER_BOTTOM_OHMS) / DIVIDER_BOTTOM_OHMS) / (OPAMP_GAIN) )
#endif

class CpDischg {
public:
    explicit CpDischg(Relay* relay)
        : relay(relay) {}

    inline void setRelay(Relay* r) { relay = r; }
    inline void setBypassRelayGate(bool enable) { bypassRelayGate = enable; }
    inline bool isBypassRelayGate() const { return bypassRelayGate; }

    // Initialize ADC + start / ensure background monitor task.
    void begin();

    // Explicit, intentional capacitor discharge using heater outputs.
    // Only this function is allowed to toggle heaters for bleeding.
    void discharge();

    // Non-blocking:
    // Returns last background-computed minimum capacitor/bus voltage.
    // Does NOT read ADC, does NOT change any hardware state.
    float readCapVoltage();

private:
    // Convert raw ADC code -> bus voltage (no HW side-effects).
    float adcCodeToBusVolts(uint16_t raw) const;

    // Continuous monitor task entry point.
    static void monitorTaskThunk(void* param);

    // Continuous monitor logic:
    // - Samples ADC over windows
    // - Tracks min voltage
    // - Updates shared cached value under mutex
    void monitorTask(uint16_t windowMs,
                     uint16_t sampleDelayMs,
                     uint16_t staleWatchdogMs);

    // Ensure the monitor task is running; restart if it died or stalled.
    void ensureMonitorTask();

    Relay*       relay            = nullptr;
    volatile bool bypassRelayGate = true;

    // Shared state protected by voltageMutex
    float        lastMinBusVoltage = 0.0f;
    TickType_t   lastSampleTick    = 0;

    SemaphoreHandle_t voltageMutex   = nullptr;
    TaskHandle_t      monitorTaskHandle = nullptr;
};

#endif // CPDISCHG_H
