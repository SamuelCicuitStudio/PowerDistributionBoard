#ifndef STATUS_SNAPSHOT_H
#define STATUS_SNAPSHOT_H

#include <Arduino.h>
#include "HeaterManager.h"
#include "TempSensor.h"

// Lightweight, periodic snapshot of fast-changing signals.
struct StatusSnapshot {
    float capVoltage = 0.0f;
    float current    = 0.0f;

    float temps[MAX_TEMP_SENSORS] = {0};                    // DS18B20s (cached)
    float wireTemps[HeaterManager::kWireCount] = {0};       // virtual wire temps
    bool  outputs[HeaterManager::kWireCount]   = {false};   // output states

    bool  relayOn   = false;
    bool  acPresent = false;

    uint32_t updatedMs = 0; // last refresh (millis)
};

#endif // STATUS_SNAPSHOT_H
