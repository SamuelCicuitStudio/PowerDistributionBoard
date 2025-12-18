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

    void setGains(float kp, float ki);
    void setOutputLimits(float minOut, float maxOut);
    void setIntegralLimits(float minI, float maxI);

    void reset(float integral = 0.0f, float lastOutput = 0.0f);

    float update(float error, float dtSec);

    float getKp() const;
    float getKi() const;
    float getIntegral() const;
    float getLastOutput() const;

private:
    float clamp_(float v, float lo, float hi) const;

    float _kp         = 0.0f;
    float _ki         = 0.0f;
    float _integral   = 0.0f;
    float _lastOutput = 0.0f;

    float _outMin = -INFINITY;
    float _outMax = INFINITY;
    float _iMin   = -INFINITY;
    float _iMax   = INFINITY;
};

#endif // PI_CONTROLLER_H
