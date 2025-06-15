#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include "ConfigManager.h"

// Struct used to pass blink parameters to RTOS task
struct BlinkParams {
    uint8_t pin;
    int durationMs;
    bool originalState;
};

// Public function to blink a pin using a self-deleting RTOS task
void blink(uint8_t pin, int durationMs = 100);

#endif // UTILS_H
