#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include "ConfigManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ───────────────────────────────────────────────
// ADC Constants (adjust as per your circuit)
// ───────────────────────────────────────────────
#define ADC_REF_VOLTAGE         3.3f
#define ADC_MAX                 4095.0f
#define VOLTAGE_DIVIDER_RATIO   100.0f  // Adjust based on hardware
#define SAFE_VOLTAGE_THRESHOLD  5.0f    // Threshold to consider capacitor "discharged"

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

// ───────────────────────────────────────────────
// RTOS Blink Task Parameters
// ───────────────────────────────────────────────
struct BlinkParams {
    uint8_t pin;
    int durationMs;
    bool originalState;
};

// ───────────────────────────────────────────────
// Globals
// ───────────────────────────────────────────────
extern volatile WiFiStatus wifiStatus;
extern volatile bool StartFromremote;



// ───────────────────────────────────────────────
// Utility Function Prototypes
// ───────────────────────────────────────────────
void blink(uint8_t pin, int durationMs = 100);
void disableAllPins();
void BlinkTask(void* parameter);
void taskMonitorTask(void* param);
void startTaskMonitor(uint32_t intervalMs);

#endif // UTILS_H
