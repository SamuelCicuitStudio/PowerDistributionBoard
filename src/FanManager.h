#ifndef FAN_MANAGER_H
#define FAN_MANAGER_H

#include "Utils.h"

class FanManager {
public:
    void begin();                     // Initialize PWM
    void setSpeedPercent(uint8_t pct); // Set fan speed in percentage (0–100)
    void stop();                     // Stop fan
    uint8_t getSpeedPercent() const;

private:
    uint8_t currentDuty = 0;
};

#endif // FAN_MANAGER_H
