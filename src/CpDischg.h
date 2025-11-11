#ifndef CPDISCHG_H
#define CPDISCHG_H

#include <Arduino.h>
#include "Config.h"
#include "HeaterManager.h"
#include "Relay.h"
#include "Utils.h"
// ----- ADC / Divider configuration -----
// Tune via build flags / Config.h if needed.
#define ADC_OFFSET              14          // Raw ADC offset in counts
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
#define DIVIDER_TOP_OHMS        470000.0f   // 470 kΩ
#endif
#ifndef DIVIDER_BOTTOM_OHMS
#define DIVIDER_BOTTOM_OHMS     4700.0f     // 4.7 kΩ
#endif

#ifndef VOLTAGE_DIVIDER_RATIO
#define VOLTAGE_DIVIDER_RATIO \
    ((DIVIDER_TOP_OHMS + DIVIDER_BOTTOM_OHMS) / DIVIDER_BOTTOM_OHMS)
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

    Relay*       relay           = nullptr;
    volatile bool bypassRelayGate = true;

    // Shared state protected by voltageMutex:
    // - lastMinBusVoltage: last computed min over a valid 300ms window
    // - lastSampleTick:    tick count of last successful update
    float        lastMinBusVoltage = 0.0f;
    TickType_t   lastSampleTick    = 0;

    // Mutex to protect access to lastMinBusVoltage / lastSampleTick.
    SemaphoreHandle_t voltageMutex = nullptr;

    // Background sampling task handle.
    TaskHandle_t monitorTaskHandle = nullptr;
};

#endif // CPDISCHG_H
