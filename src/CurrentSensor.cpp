#include "CurrentSensor.h"

void CurrentSensor::begin() {
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                  Starting Current Manager               #");
    DEBUG_PRINTLN("###########################################################");

    pinMode(ACS_LOAD_CURRENT_VOUT_PIN, INPUT);
    DEBUG_PRINTLN("[CurrentSensor] Initialized 📈");
}

float CurrentSensor::readCurrent() {
    int adc = analogRead(ACS_LOAD_CURRENT_VOUT_PIN);
    float voltage_mv = analogToMillivolts(adc);
    float delta_mv = voltage_mv - ACS781_ZERO_CURRENT_MV;

    float current = delta_mv / ACS781_SENSITIVITY_MV_PER_A;

   // DEBUG_PRINTF("[CurrentSensor] ADC=%d, V=%.2fmV, I=%.2fA ⚡\n", adc, voltage_mv, current);
    return current;
}

float CurrentSensor::analogToMillivolts(int adcValue) {
    return (adcValue / ADC_MAX) * ADC_REF_VOLTAGE * 1000.0f;
}
