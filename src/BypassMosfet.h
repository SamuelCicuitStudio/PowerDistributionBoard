#ifndef BYPASS_MOSFET_H
#define BYPASS_MOSFET_H

#include "Utils.h"

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
