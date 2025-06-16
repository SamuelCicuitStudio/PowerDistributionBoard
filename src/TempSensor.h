#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "ConfigManager.h"
#include "config.h"  // Includes ONE_WIRE_BUS definition

#define MAX_TEMP_SENSORS            4

class TempSensor {
public:
    TempSensor();

    void begin(ConfigManager* config);
    void requestTemperatures();
    float getTemperature(uint8_t index);
    uint8_t getSensorCount();

    void stopTemperatureTask();                             // Stop background task
    void startTemperatureTask(uint32_t intervalMs = 2000);  // Start with custom delay

private:
    static void temperatureTask(void* param);
    void printAddress(DeviceAddress address);

    ConfigManager* cfg = nullptr;
    uint8_t sensorCount = 0;
    uint32_t updateIntervalMs = 2000;  // default delay

    OneWire oneWire;
    DallasTemperature sensors;
    DeviceAddress sensorAddresses[MAX_TEMP_SENSORS];

    TaskHandle_t tempTaskHandle = nullptr;
};

#endif// TEMP_SENSOR_H