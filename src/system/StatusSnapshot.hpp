/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef STATUS_SNAPSHOT_H
#define STATUS_SNAPSHOT_H

#include <Arduino.h>
#include <HeaterManager.hpp>
#include <TempSensor.hpp>

// Lightweight, periodic snapshot of fast-changing signals.
struct StatusSnapshot {
    float capVoltage   = 0.0f;
    float capAdcScaled = 0.0f;  // raw ADC code / 100.0 (e.g., 4095 -> 40.95)
    float current      = 0.0f;
    float currentAcs   = 0.0f;

    float temps[MAX_TEMP_SENSORS] = {0};                    // DS18B20s (cached)
    double wireTemps[HeaterManager::kWireCount] = {0};      // virtual wire temps
    bool  outputs[HeaterManager::kWireCount]   = {false};   // output states
    bool  wirePresent[HeaterManager::kWireCount] = {false}; // presence flags

    bool  relayOn   = false;
    bool  acPresent = false;

    uint32_t updatedMs = 0; // last refresh (millis)
};

#endif // STATUS_SNAPSHOT_H


