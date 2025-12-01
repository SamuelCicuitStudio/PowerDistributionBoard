/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>
#include <OneWire.h>

#include "services/NVSManager.h"
#include "system/Config.h"


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

// Default update period: 5 seconds
#ifndef TEMP_SENSOR_UPDATE_INTERVAL_MS
#define TEMP_SENSOR_UPDATE_INTERVAL_MS 5000UL
#endif

enum class TempRole : uint8_t { Unknown=0, Board0, Board1, Heatsink };

class TempSensor {
public:
    explicit TempSensor(OneWire* oneWireBus)
        : ow(oneWireBus) {}

    void begin();

    // Background task helpers
    void requestTemperatures();
    void stopTemperatureTask();
    void startTemperatureTask(uint32_t intervalMs = TEMP_SENSOR_UPDATE_INTERVAL_MS);

    // Cached read (Â°C), NON-BLOCKING
    float   getTemperature(uint8_t index);
    uint8_t getSensorCount();

    // Roles & labels
    float  getHeatsinkTemp();
    float  getBoardTemp(uint8_t which /*0 or 1*/);
    int    indexForRole(TempRole role) const;
    String getLabelForIndex(uint8_t index);

    // Identification core (called by begin)
    void identifyAndPersistSensors();

    // Public for diagnostics
    uint8_t  sensorCount        = 0;
    uint32_t updateIntervalMs   = TEMP_SENSOR_UPDATE_INTERVAL_MS;
    uint8_t  sensorAddresses[MAX_TEMP_SENSORS][8];
    TaskHandle_t tempTaskHandle = nullptr;

    static void printAddress(uint8_t address[8]);
    static void temperatureTask(void* param);

private:
    OneWire* ow = nullptr;

    // Cached readings
    float lastTempsC[MAX_TEMP_SENSORS] = {0};
    bool  lastValid[MAX_TEMP_SENSORS]  = {false};

    // OneWire scratch
    uint8_t scratchpad[9];

    // Mutex guards: OneWire, addresses, caches, interval, sensorCount
    SemaphoreHandle_t _mutex = nullptr;
    inline bool lock(TickType_t timeoutTicks = portMAX_DELAY) const {
        if (_mutex == nullptr) return true;
        return (xSemaphoreTake(_mutex, timeoutTicks) == pdTRUE);
    }
    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }

    // Role->index map
    struct { int board0 = -1; int board1 = -1; int heatsink = -1; } map_;

    // Helpers
    static String addrToHex(const uint8_t a[8]);
    static bool   hexToAddr(const String& hex, uint8_t out[8]);
    static bool   equalAddr(const uint8_t a[8], const uint8_t b[8]);
    int           findIndexByAddr(const uint8_t addr[8]) const;

    void discoverSensors();
    void updateAllTemperaturesBlocking();
};

#endif


