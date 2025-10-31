#include "CurrentSensor.h"

void CurrentSensor::begin() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                  Starting Current Manager               #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    // Create the mutex first so future reads are protected
    _mutex = xSemaphoreCreateMutex();

    pinMode(ACS_LOAD_CURRENT_VOUT_PIN, INPUT);
    DEBUG_PRINTLN("[CurrentSensor] Initialized 📈");
}

float CurrentSensor::readCurrent() {
    // We'll take NUM_SAMPLES readings, average them, convert to amps.
    constexpr uint8_t NUM_SAMPLES = 25;

    if (!lock()) {
        // If somehow the mutex fails, we'll still try a best-effort single read.
        int adcFallback = analogRead(ACS_LOAD_CURRENT_VOUT_PIN);
        float voltage_mv_fb = analogToMillivolts(adcFallback);
        float delta_mv_fb   = voltage_mv_fb - ACS781_ZERO_CURRENT_MV;
        float current_fb    = delta_mv_fb / ACS781_SENSITIVITY_MV_PER_A;
        return current_fb;
    }

    long sumAdc = 0;

    // Take multiple samples for stability (atomic section)
    for (uint8_t i = 0; i < NUM_SAMPLES; i++) {
        sumAdc += analogRead(ACS_LOAD_CURRENT_VOUT_PIN);
    }

    // Average ADC value
    int adc = sumAdc / NUM_SAMPLES;

    // Convert ADC reading to millivolts
    float voltage_mv = analogToMillivolts(adc);

    // Delta from zero-current reference (1650 mV @ 0 A)
    float delta_mv = voltage_mv - ACS781_ZERO_CURRENT_MV;

    // Convert to current (A)
    float current = delta_mv / ACS781_SENSITIVITY_MV_PER_A;

    unlock();

    // DEBUG_PRINTF("[CurrentSensor] ADC=%d, V=%.2fmV, I=%.2fA ⚡\n", adc, voltage_mv, current);
    return current;
}

float CurrentSensor::analogToMillivolts(int adcValue) {
    return (adcValue / ADC_MAX) * ADC_REF_VOLTAGE * 1000.0f;
}
