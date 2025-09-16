#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "ConfigManager.h"
#include "config.h"  // Keeps your keys/macros like TEMP_SENSOR_COUNT_KEY, etc.

#ifndef MAX_TEMP_SENSORS
#define MAX_TEMP_SENSORS 12
#endif

class TempSensor {
public:
    // Inject dependencies: config, OneWire bus, DallasTemperature wrapper
    explicit TempSensor(ConfigManager* config, OneWire* oneWireBus, DallasTemperature* dallas)
        : cfg(config), ow(oneWireBus), sensors(dallas) {}

    // Initializes the DallasTemperature bus and starts task if at least one valid sensor is found
    void begin();

    // Manual trigger (non-blocking request)
    void requestTemperatures();

    // Read last temperature (in °C) by index; returns NAN if index invalid
    float getTemperature(uint8_t index);

    // Returns persisted (or default) sensor count
    uint8_t getSensorCount();

    // RTOS task control
    void stopTemperatureTask();
    void startTemperatureTask(uint32_t intervalMs = 3000);

    // Helper: print an 8-byte DeviceAddress
    static void printAddress(DeviceAddress address);

    // RTOS worker (static trampoline)
    static void temperatureTask(void* param);

    // --- Public state (kept as in your original layout) ---
    ConfigManager* cfg = nullptr;
    uint8_t  sensorCount       = 0;
    uint32_t updateIntervalMs  = 2000;
    DeviceAddress sensorAddresses[MAX_TEMP_SENSORS];
    TaskHandle_t tempTaskHandle = nullptr;

private:
    OneWire*           ow       = nullptr;   // injected bus
    DallasTemperature* sensors  = nullptr;   // injected Dallas wrapper
};

#endif // TEMP_SENSOR_H
