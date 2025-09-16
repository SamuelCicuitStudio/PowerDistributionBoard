#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>
#include <OneWire.h>
#include "ConfigManager.h"
#include "config.h"

#ifndef MAX_TEMP_SENSORS
#define MAX_TEMP_SENSORS 12
#endif

class TempSensor {
public:
    explicit TempSensor(ConfigManager* config, OneWire* oneWireBus)
        : cfg(config), ow(oneWireBus) {}

    void begin();                               // Discover sensors
    void requestTemperatures();                 // Start conversions
    float getTemperature(uint8_t index);        // Read by index
    uint8_t getSensorCount();                   // Count of sensors

    void stopTemperatureTask();
    void startTemperatureTask(uint32_t intervalMs = 3000);

    static void printAddress(uint8_t address[8]);
    static void temperatureTask(void* param);

    ConfigManager* cfg = nullptr;
    uint8_t sensorCount = 0;
    uint32_t updateIntervalMs = 2000;
    uint8_t sensorAddresses[MAX_TEMP_SENSORS][8];  // ROM codes
    TaskHandle_t tempTaskHandle = nullptr;

private:
    OneWire* ow = nullptr;
    uint8_t scratchpad[9];  // buffer for reads
};

#endif
