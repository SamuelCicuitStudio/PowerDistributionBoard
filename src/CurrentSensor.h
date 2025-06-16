#ifndef CURRENT_SENSOR_H
#define CURRENT_SENSOR_H

#include <Arduino.h>
#include "config.h"

// Sensor characteristics (for ACS781LLRTR-100B-T)
#define ACS781_SENSITIVITY_MV_PER_A   13.2f
#define ACS781_ZERO_CURRENT_MV        1650.0f   // 3.3V / 2 = 1.65V
#define ADC_REF_VOLTAGE               3.3f
#define ADC_MAX                       4095.0f

class CurrentSensor {
public:
    void begin();
    float readCurrent();  // Returns current in Amperes

private:
    float analogToMillivolts(int adcValue);
};

#endif // CURRENT_SENSOR_H
