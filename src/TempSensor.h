#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "ConfigManager.h"
#include "config.h"  // Defines ONE_WIRE_BUS

#define MAX_TEMP_SENSORS 4  // Adjust if more sensors are supported

/**
 * TempSensor
 * ----------
 * Handles DS18B20 sensors on a shared OneWire bus.
 * Supports periodic reading via RTOS task and manual requests.
 */
class TempSensor {
public:
    explicit TempSensor(ConfigManager* config)
    : cfg(config), oneWire(ONE_WIRE_BUS), sensors(&oneWire) {};

    void begin();                                           // Initializes the sensor bus
    void requestTemperatures();                             // Manual read trigger
    float getTemperature(uint8_t index);                    // Get latest reading
    uint8_t getSensorCount();                               // Total sensors detected

    void stopTemperatureTask();                             // Stop background task
    void startTemperatureTask(uint32_t intervalMs = 3000);  // Start with custom delay

private:
    static void temperatureTask(void* param);               // RTOS loop
    void printAddress(DeviceAddress address);               // Debug helper

    ConfigManager* cfg = nullptr;
    uint8_t sensorCount = 0;
    uint32_t updateIntervalMs = 2000;                       // RTOS delay

    OneWire oneWire = OneWire(ONE_WIRE_BUS);
    DallasTemperature sensors = DallasTemperature(&oneWire);
    DeviceAddress sensorAddresses[MAX_TEMP_SENSORS];

    TaskHandle_t tempTaskHandle = nullptr;
};

#endif // TEMP_SENSOR_H
