/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#include "services/ThermalPiControllers.h"
#include <math.h>

ThermalPiControllers* ThermalPiControllers::s_instance = nullptr;

void ThermalPiControllers::Init() {
    (void)ThermalPiControllers::Get();
}

ThermalPiControllers* ThermalPiControllers::Get() {
    if (!s_instance) {
        s_instance = new ThermalPiControllers();
    }
    return s_instance;
}

void ThermalPiControllers::begin() {
    loadFromNvs();
}

PiController& ThermalPiControllers::wire() {
    return _wirePi;
}

PiController& ThermalPiControllers::floor() {
    return _floorPi;
}

float ThermalPiControllers::getWireKp() const {
    return _wirePi.getKp();
}

float ThermalPiControllers::getWireKi() const {
    return _wirePi.getKi();
}

float ThermalPiControllers::getFloorKp() const {
    return _floorPi.getKp();
}

float ThermalPiControllers::getFloorKi() const {
    return _floorPi.getKi();
}

void ThermalPiControllers::setWireKp(float kp, bool persist) {
    setWireGains(kp, _wirePi.getKi(), persist);
}

void ThermalPiControllers::setWireKi(float ki, bool persist) {
    setWireGains(_wirePi.getKp(), ki, persist);
}

void ThermalPiControllers::setFloorKp(float kp, bool persist) {
    setFloorGains(kp, _floorPi.getKi(), persist);
}

void ThermalPiControllers::setFloorKi(float ki, bool persist) {
    setFloorGains(_floorPi.getKp(), ki, persist);
}

void ThermalPiControllers::setWireGains(float kp, float ki, bool persist) {
    if (!isfinite(kp) || kp < 0.0f) kp = DEFAULT_WIRE_KP;
    if (!isfinite(ki) || ki < 0.0f) ki = DEFAULT_WIRE_KI;
    _wirePi.setGains(kp, ki);

    if (persist && CONF) {
        CONF->PutFloat(WIRE_KP_KEY, kp);
        CONF->PutFloat(WIRE_KI_KEY, ki);
    }
}

void ThermalPiControllers::setFloorGains(float kp, float ki, bool persist) {
    if (!isfinite(kp) || kp < 0.0f) kp = DEFAULT_FLOOR_KP;
    if (!isfinite(ki) || ki < 0.0f) ki = DEFAULT_FLOOR_KI;
    _floorPi.setGains(kp, ki);

    if (persist && CONF) {
        CONF->PutFloat(FLOOR_KP_KEY, kp);
        CONF->PutFloat(FLOOR_KI_KEY, ki);
    }
}

void ThermalPiControllers::loadFromNvs() {
    float wireKp = DEFAULT_WIRE_KP;
    float wireKi = DEFAULT_WIRE_KI;
    float floorKp = DEFAULT_FLOOR_KP;
    float floorKi = DEFAULT_FLOOR_KI;

    if (CONF) {
        wireKp = CONF->GetFloat(WIRE_KP_KEY, DEFAULT_WIRE_KP);
        wireKi = CONF->GetFloat(WIRE_KI_KEY, DEFAULT_WIRE_KI);
        floorKp = CONF->GetFloat(FLOOR_KP_KEY, DEFAULT_FLOOR_KP);
        floorKi = CONF->GetFloat(FLOOR_KI_KEY, DEFAULT_FLOOR_KI);
    }

    setWireGains(wireKp, wireKi, false);
    setFloorGains(floorKp, floorKi, false);
}
