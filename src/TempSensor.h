#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include "NVSManager.h"
#include "Config.h"

#ifndef MAX_TEMP_SENSORS
#define MAX_TEMP_SENSORS 12
#endif

class TempSensor {
public:
    explicit TempSensor( OneWire* oneWireBus)
        : ow(oneWireBus) {}

    void begin();                               // Discover sensors + start task
    void requestTemperatures();                 // Kick conversion on all sensors
    float getTemperature(uint8_t index);        // Read temperature by index
    uint8_t getSensorCount();                   // Return stored count from cfg

    void stopTemperatureTask();                 // Kill background updater task
    void startTemperatureTask(uint32_t intervalMs = 3000);

    static void printAddress(uint8_t address[8]);
    static void temperatureTask(void* param);

    // -------- public data (kept for compatibility with your code) --------
    uint8_t  sensorCount        = 0;
    uint32_t updateIntervalMs   = 2000;
    uint8_t  sensorAddresses[MAX_TEMP_SENSORS][8];  // ROM codes
    TaskHandle_t tempTaskHandle = nullptr;

private:
    OneWire* ow = nullptr;
    uint8_t scratchpad[9];  // shared read buffer

    // Mutex to protect OneWire bus, scratchpad, sensorAddresses access,
    // and updateIntervalMs snapshots.
    SemaphoreHandle_t _mutex = nullptr;

    // lock/unlock helpers
    inline bool lock() const {
        if (_mutex == nullptr) return true;
        return (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE);
    }

    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }
};

#endif
