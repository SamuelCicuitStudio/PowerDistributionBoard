#ifndef CURRENT_SENSOR_H
#define CURRENT_SENSOR_H

#include "Utils.h"
// Sensor characteristics (for ACS781LLRTR-100B-T)
#define ACS781_SENSITIVITY_MV_PER_A   13.2f
#define ACS781_ZERO_CURRENT_MV        1650.0f   // 3.3V / 2 = 1.65V
#define ADC_REF_VOLTAGE               3.3f
#define ADC_MAX                       4095.0f

class CurrentSensor {
public:
    CurrentSensor() : _mutex(nullptr) {}

    // Initialize the input pin and create mutex
    void begin();

    // Thread-safe read of current in Amperes
    float readCurrent();

private:
    float analogToMillivolts(int adcValue);

    // Mutex to serialize ADC access and math
    SemaphoreHandle_t _mutex;

    inline bool lock() const {
        if (_mutex == nullptr) return true;
        return (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE);
    }

    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }
};

#endif // CURRENT_SENSOR_H
