/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef PI_CONTROLLER_H
#define PI_CONTROLLER_H

#include <Arduino.h>
#include <math.h>

class PiController {
public:
    PiController();

    void setGains(double kp, double ki);
    void setOutputLimits(double minOut, double maxOut);
    void setIntegralLimits(double minI, double maxI);

    void reset(double integral = 0.0, double lastOutput = 0.0);

    double update(double error, double dtSec);

    double getKp() const;
    double getKi() const;
    double getIntegral() const;
    double getLastOutput() const;

private:
    double clamp_(double v, double lo, double hi) const;

    double _kp         = 0.0;
    double _ki         = 0.0;
    double _integral   = 0.0;
    double _lastOutput = 0.0;

    double _outMin = -INFINITY;
    double _outMax = INFINITY;
    double _iMin   = -INFINITY;
    double _iMax   = INFINITY;
};

#endif // PI_CONTROLLER_H
