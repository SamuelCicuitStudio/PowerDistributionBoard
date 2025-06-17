#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include "ConfigManager.h"

#define ADC_REF_VOLTAGE         3.3f
#define ADC_MAX                 4095.0f
#define VOLTAGE_DIVIDER_RATIO   100.0f  // Adjust based on actual hardware divider
#define SAFE_VOLTAGE_THRESHOLD  5.0f    // Target voltage to consider "discharged"

// ───────────────────────────────────────────────
// Device operational states
// ───────────────────────────────────────────────
enum class DeviceState {
    Idle,
    Running,
    Error,
    Shutdown
};

// ───────────────────────────────────────────────
// Wi-Fi connection levels
// ───────────────────────────────────────────────
enum class WiFiStatus {
    NotConnected,
    UserConnected,
    AdminConnected
};

// Struct for RTOS blink task
struct BlinkParams {
    uint8_t pin;
    int durationMs;
    bool originalState;
};

extern volatile WiFiStatus wifiStatus;  // Declare, do not define here

extern volatile bool StartFromremote;

// Blink utility function
void blink(uint8_t pin, int durationMs = 100);

// Disable all control pins (safe shutdown)
void disableAllPins();
float readCapVoltage() ;


#endif // UTILS_H
