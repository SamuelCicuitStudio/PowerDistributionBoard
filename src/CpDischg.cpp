#include "CpDischg.h"

// ----- ADC / Divider configuration -----
#define ADC_OFFSET              14    // Raw ADC offset in counts
#ifndef ADC_REF_VOLTAGE
#define ADC_REF_VOLTAGE         3.3f  // ESP32 ADC full-scale
#endif
#ifndef ADC_MAX
#define ADC_MAX                 4095.0f   // 12-bit ADC (0–4095)
#endif

#ifndef SAFE_VOLTAGE_THRESHOLD
#define SAFE_VOLTAGE_THRESHOLD  5.0f
#endif

// Divider: top to HV, bottom to GND
#ifndef DIVIDER_TOP_OHMS
#define DIVIDER_TOP_OHMS        470000.0f   // 470 kΩ
#endif
#ifndef DIVIDER_BOTTOM_OHMS
#define DIVIDER_BOTTOM_OHMS     4700.0f     // 4.7 kΩ
#endif

// Gain (bus volts = v_adc * ratio)
#ifndef VOLTAGE_DIVIDER_RATIO
#define VOLTAGE_DIVIDER_RATIO   ((DIVIDER_TOP_OHMS + DIVIDER_BOTTOM_OHMS) / DIVIDER_BOTTOM_OHMS)
#endif
// ---------------------------------------

void CpDischg::begin() {
    pinMode(CAPACITOR_ADC_PIN, INPUT);
#if defined(ARDUINO_ARCH_ESP32)
    analogSetPinAttenuation(CAPACITOR_ADC_PIN, ADC_11db);  // ~3.3V range
#endif
}

void CpDischg::discharge() {
    while (true) {
        float v = readCapVoltage();
        DEBUG_PRINTF("[CpDischg] Capacitor voltage: %.1f V ⚡\n", v);

        if (v <= SAFE_VOLTAGE_THRESHOLD) {
            break;
        }

        if (heaterManager) {
            for (int i = 1; i <= 10; ++i) {
                heaterManager->setOutput(i, true);
                delay(20);
                heaterManager->setOutput(i, false);
            }
        }
        delay(100);
    }
    if (heaterManager) heaterManager->disableAll();
}

// ---- Simple one-shot ADC read ----
float CpDischg::readCapVoltage() {
    uint16_t raw = analogRead(CAPACITOR_ADC_PIN);
    return adcCodeToBusVolts(raw);
}

// ---- ADC code -> bus volts ----
float CpDischg::adcCodeToBusVolts(uint16_t raw) const {
    // Apply offset in raw ADC counts
    int32_t correctedRaw = static_cast<int32_t>(raw) - ADC_OFFSET;
    if (correctedRaw < 0) correctedRaw = 0;   // clamp to 0

    float v_adc = (static_cast<float>(correctedRaw) / ADC_MAX) * ADC_REF_VOLTAGE;
    return v_adc * VOLTAGE_DIVIDER_RATIO;
}
