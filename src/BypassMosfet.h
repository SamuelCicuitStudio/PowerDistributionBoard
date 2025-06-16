#ifndef BYPASS_MOSFET_H
#define BYPASS_MOSFET_H

#include <Arduino.h>
#include "config.h"  // for INA_RELAY_BYPASS_PIN

class BypassMosfet {
public:
    void begin();       // Initialize to safe (OFF) state
    void enable();      // Turn ON (bypass inrush resistor)
    void disable();     // Turn OFF
    bool isEnabled() const;

private:
    bool state = false;
};

#endif // BYPASS_MOSFET_H
