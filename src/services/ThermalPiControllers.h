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

    double getWireKp() const;
    double getWireKi() const;
    double getFloorKp() const;
    double getFloorKi() const;

    void setWireKp(double kp, bool persist = true);
    void setWireKi(double ki, bool persist = true);
    void setFloorKp(double kp, bool persist = true);
    void setFloorKi(double ki, bool persist = true);
    void setWireGains(double kp, double ki, bool persist = true);
    void setFloorGains(double kp, double ki, bool persist = true);

private:
    ThermalPiControllers() = default;
    void loadFromNvs();

    static ThermalPiControllers* s_instance;

    PiController _wirePi;
    PiController _floorPi;
};

#define THERMAL_PI ThermalPiControllers::Get()

#endif // THERMAL_PI_CONTROLLERS_H
