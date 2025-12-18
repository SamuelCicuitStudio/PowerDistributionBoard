/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#include "control/PiController.h"

PiController::PiController() = default;

void PiController::setGains(float kp, float ki) {
    _kp = kp;
    _ki = ki;
}

void PiController::setOutputLimits(float minOut, float maxOut) {
    if (!isfinite(minOut)) minOut = -INFINITY;
    if (!isfinite(maxOut)) maxOut = INFINITY;
    if (minOut > maxOut) {
        float tmp = minOut;
        minOut = maxOut;
        maxOut = tmp;
    }
    _outMin = minOut;
    _outMax = maxOut;
}

void PiController::setIntegralLimits(float minI, float maxI) {
    if (!isfinite(minI)) minI = -INFINITY;
    if (!isfinite(maxI)) maxI = INFINITY;
    if (minI > maxI) {
        float tmp = minI;
        minI = maxI;
        maxI = tmp;
    }
    _iMin = minI;
    _iMax = maxI;
    _integral = clamp_(_integral, _iMin, _iMax);
}

void PiController::reset(float integral, float lastOutput) {
    _integral = integral;
    _lastOutput = lastOutput;
    _integral = clamp_(_integral, _iMin, _iMax);
    _lastOutput = clamp_(_lastOutput, _outMin, _outMax);
}

float PiController::update(float error, float dtSec) {
    if (!isfinite(error)) {
        return _lastOutput;
    }
    if (!isfinite(dtSec) || dtSec <= 0.0f) {
        return _lastOutput;
    }

    const float p = _kp * error;
    float iTerm = _integral + (_ki * error * dtSec);
    iTerm = clamp_(iTerm, _iMin, _iMax);

    float out = p + iTerm;

    if (out > _outMax) {
        out = _outMax;
        if (isfinite(_outMax)) {
            iTerm = _outMax - p;
        }
    } else if (out < _outMin) {
        out = _outMin;
        if (isfinite(_outMin)) {
            iTerm = _outMin - p;
        }
    }

    _integral = clamp_(iTerm, _iMin, _iMax);
    _lastOutput = out;
    return out;
}

float PiController::getKp() const {
    return _kp;
}

float PiController::getKi() const {
    return _ki;
}

float PiController::getIntegral() const {
    return _integral;
}

float PiController::getLastOutput() const {
    return _lastOutput;
}

float PiController::clamp_(float v, float lo, float hi) const {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
