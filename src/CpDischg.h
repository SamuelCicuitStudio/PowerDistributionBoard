#ifndef CPDISCHG_H
#define CPDISCHG_H

#include <Arduino.h>
#include "Config.h"
#include "HeaterManager.h"
#include "Relay.h"
#include "Utils.h"

class CpDischg {
public:
    explicit CpDischg( Relay* relay)
        :relay(relay) {}

    inline void setRelay(Relay* r) { relay = r; }

    inline void setBypassRelayGate(bool enable) { bypassRelayGate = enable; }
    inline bool isBypassRelayGate() const { return bypassRelayGate; }

    void begin();
    void discharge();

    // Sample for 20 ms and return lowest bus voltage
    float readCapVoltage();

private:
    float adcCodeToBusVolts(uint16_t raw) const;
    Relay*         relay         = nullptr;

    volatile bool  bypassRelayGate = true;
};

#endif // CPDISCHG_H
