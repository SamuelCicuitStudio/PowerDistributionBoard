#ifndef RELAY_H
#define RELAY_H

#include <Arduino.h>
#include "config.h"

/**
 * Relay
 * ------
 * Manages the power relay that controls input to the capacitor.
 * Active HIGH: setting pin HIGH enables the relay.
 */
class Relay {
public:
    /**
     * Initialize the relay control pin
     */
    void begin();

    /**
     * Turn the relay ON (active)
     */
    void turnOn();

    /**
     * Turn the relay OFF (inactive)
     */
    void turnOff();

    /**
     * Get current state
     * @return true if ON, false if OFF
     */
    bool isOn() const;

private:
    bool state = false;
};

#endif // RELAY_H
