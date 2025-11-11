#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>
#include <OneWire.h>

#include "NVSManager.h"
#include "Config.h"

// Max supported sensors on the bus
#ifndef MAX_TEMP_SENSORS
#define MAX_TEMP_SENSORS 12
#endif

// Task configuration (tune as needed)
#ifndef TEMP_SENSOR_TASK_STACK_SIZE
#define TEMP_SENSOR_TASK_STACK_SIZE 2048
#endif

#ifndef TEMP_SENSOR_TASK_PRIORITY
#define TEMP_SENSOR_TASK_PRIORITY 3
#endif

#ifndef TEMP_SENSOR_TASK_CORE
#define TEMP_SENSOR_TASK_CORE 1
#endif

// Default update period: 5 seconds as requested
#ifndef TEMP_SENSOR_UPDATE_INTERVAL_MS
#define TEMP_SENSOR_UPDATE_INTERVAL_MS 5000UL
#endif

class TempSensor {
public:
    explicit TempSensor(OneWire* oneWireBus)
        : ow(oneWireBus) {}

    // Discover sensors & start periodic background sampling
    void begin();

    // Legacy API (kept for compatibility):
    // Now ONLY kicks a conversion on all sensors (used by background task),
    // never reads temperatures or returns them.
    void requestTemperatures();

    // Returns last measured temperature for sensor[index] (°C).
    // NON-BLOCKING, NO hardware / OneWire access.
    float getTemperature(uint8_t index);

    // Number of discovered sensors.
    // Uses runtime sensorCount; falls back to config if needed.
    uint8_t getSensorCount();

    // Stop / start the background updater task.
    void stopTemperatureTask();
    void startTemperatureTask(uint32_t intervalMs = TEMP_SENSOR_UPDATE_INTERVAL_MS);

    static void printAddress(uint8_t address[8]);
    static void temperatureTask(void* param);

    // Public for diagnostics / compatibility
    uint8_t  sensorCount        = 0;
    uint32_t updateIntervalMs   = TEMP_SENSOR_UPDATE_INTERVAL_MS;
    uint8_t  sensorAddresses[MAX_TEMP_SENSORS][8];
    TaskHandle_t tempTaskHandle = nullptr;

private:
    OneWire* ow = nullptr;

    // Cached readings (no direct hardware in getters)
    float lastTempsC[MAX_TEMP_SENSORS]   = {0};
    bool  lastValid[MAX_TEMP_SENSORS]    = {false};

    // Shared OneWire scratchpad (used only under mutex in task)
    uint8_t scratchpad[9];

    // Mutex to protect:
    //  - OneWire bus
    //  - sensorAddresses
    //  - lastTempsC / lastValid
    //  - updateIntervalMs / sensorCount snapshot
    SemaphoreHandle_t _mutex = nullptr;

    inline bool lock(TickType_t timeoutTicks = portMAX_DELAY) const {
        if (_mutex == nullptr) return true;
        return (xSemaphoreTake(_mutex, timeoutTicks) == pdTRUE);
    }

    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }

    // Internal helpers
    void discoverSensors();
    void updateAllTemperaturesBlocking();
};

#endif
