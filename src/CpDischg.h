#ifndef CPDISCHG_H
#define CPDISCHG_H

#include <Arduino.h>
#include "config.h"
#include "HeaterManager.h"
#include "Relay.h"
#include "Utils.h"

class CpDischg {
public:
    explicit CpDischg(HeaterManager* heater, Relay* relay)
        : heaterManager(heater), relay(relay) {}

    inline void setHeaterManager(HeaterManager* h) { heaterManager = h; }
    inline void setRelay(Relay* r) { relay = r; }

    // NEW: toggle whether we gate the relay during measurements
    inline void setBypassRelayGate(bool enable) { bypassRelayGate = enable; }
    inline bool isBypassRelayGate() const { return bypassRelayGate; }

    void begin();
    void discharge();
    void startCapVoltageTask();
    void stopCapVoltageTask();

    float readCapVoltage() { return g_capVoltage; }

    // Latest measured voltage (updated by task)
    volatile float g_capVoltage = 0.0f;

private:
    // Performs exactly ONE measurement.
    // If bypassRelayGate==false and relay!=nullptr:
    //  1) remember relay state, 2) turn OFF, 3) read once, 4) restore state
    // Else: just read once.
    float measureOnceWithRelayGate();

    HeaterManager* heaterManager = nullptr;
    Relay*         relay         = nullptr;

    // NEW: runtime flag to skip relay gating when true
    volatile bool  bypassRelayGate = false;

public:
    TaskHandle_t   capVoltageTaskHandle = nullptr;
};

#endif // CPDISCHG_H
