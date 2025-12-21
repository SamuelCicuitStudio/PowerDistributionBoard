/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef CPDISCHG_H
#define CPDISCHG_H

#include <Arduino.h>
#include "system/Config.h"
#include "control/HeaterManager.h"
#include "io/Relay.h"
#include "system/Utils.h"

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
#define DIVIDER_TOP_OHMS        470000.0f   // 470 kÎ© (top)
#endif

// *** UPDATED: bottom resistor is 3.9 kÎ© (was 4.7 kÎ©) ***
#ifndef DIVIDER_BOTTOM_OHMS
#define DIVIDER_BOTTOM_OHMS     3900.0f     // 3.9 kÎ© (bottom)
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

/*** Empirical ADC→Bus calibration (bypasses divider math) ***/
// Enable this to force: Vbus = EMP_GAIN * Vadc + EMP_OFFSET
#ifndef CAP_USE_EMPIRICAL_CAL
#define CAP_USE_EMPIRICAL_CAL   1   // 1 = use empirical mapping; 0 = use resistor math
#endif

// Default: 319 V at 2.00 V on ADC pin  →  gain = 159.5 V/V
#ifndef CAP_EMP_GAIN
#define CAP_EMP_GAIN  (321.0f / 1.90f)   //
#endif

#ifndef CAP_EMP_OFFSET
#define CAP_EMP_OFFSET          2.0f      // Vbus offset in volts
#endif

#ifndef CAP_EMP_GAIN_MIN
#define CAP_EMP_GAIN_MIN        50.0f     // sanity lower bound for runtime gain
#endif

#ifndef CAP_EMP_GAIN_MAX
#define CAP_EMP_GAIN_MAX        500.0f    // sanity upper bound for runtime gain
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
    // Returns last raw ADC code as a scaled float (e.g., 4095 -> 40.95).
    float readCapAdcScaled();

    struct Sample {
        uint32_t timestampMs;
        float    voltageV;
    };

    // Voltage history (like CurrentSensor): timestamped samples since lastSeq.
    size_t getHistorySince(uint32_t lastSeq,
                           Sample* out,
                           size_t maxOut,
                           uint32_t& newSeq) const;

    // Single-shot voltage sample (immediate ADC read, scaled).
    float sampleVoltageNow();
    // Raw ADC sample (immediate) without scaling.
    uint16_t sampleAdcRaw() const;
    // Convert raw ADC code to ADC pin voltage (after offset).
    float adcCodeToAdcVolts(uint16_t raw) const;
    // Runtime adjustment of empirical gain (persist optional).
    void  setEmpiricalGain(float gain, bool persist = false);
    float getEmpiricalGain() const { return empiricalGain; }

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
    void loadEmpiricalGainFromConfig();

    Relay*       relay            = nullptr;
    volatile bool bypassRelayGate = true;

    // Shared state protected by voltageMutex
    float        lastMinBusVoltage = 0.0f;
    uint16_t     lastRawAdc        = 0;
    TickType_t   lastSampleTick    = 0;
    TickType_t   lastStaleWarnTick = 0;

    // Rolling history
    static constexpr size_t VOLT_HISTORY_SAMPLES = 256;
    Sample        _history[VOLT_HISTORY_SAMPLES]{};
    uint32_t      _historyHead = 0;
    uint32_t      _historySeq  = 0;

    SemaphoreHandle_t voltageMutex   = nullptr;
    TaskHandle_t      monitorTaskHandle = nullptr;

    // Runtime-tunable empirical calibration.
    float empiricalGain = CAP_EMP_GAIN;
};

#endif // CPDISCHG_H
