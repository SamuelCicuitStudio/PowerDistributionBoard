/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef THERMAL_PI_CONTROLLERS_H
#define THERMAL_PI_CONTROLLERS_H

#include <Arduino.h>
#include "system/Config.h"
#include "services/NVSManager.h"
#include "control/PiController.h"

class ThermalPiControllers {
public:
    static void Init();
    static ThermalPiControllers* Get();

    void begin();

    PiController& wire();
    PiController& floor();

    float getWireKp() const;
    float getWireKi() const;
    float getFloorKp() const;
    float getFloorKi() const;

    void setWireKp(float kp, bool persist = true);
    void setWireKi(float ki, bool persist = true);
    void setFloorKp(float kp, bool persist = true);
    void setFloorKi(float ki, bool persist = true);
    void setWireGains(float kp, float ki, bool persist = true);
    void setFloorGains(float kp, float ki, bool persist = true);

private:
    ThermalPiControllers() = default;
    void loadFromNvs();

    static ThermalPiControllers* s_instance;

    PiController _wirePi;
    PiController _floorPi;
};

#define THERMAL_PI ThermalPiControllers::Get()

#endif // THERMAL_PI_CONTROLLERS_H
