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

double ThermalPiControllers::getWireKp() const {
    return _wirePi.getKp();
}

double ThermalPiControllers::getWireKi() const {
    return _wirePi.getKi();
}

double ThermalPiControllers::getFloorKp() const {
    return _floorPi.getKp();
}

double ThermalPiControllers::getFloorKi() const {
    return _floorPi.getKi();
}

void ThermalPiControllers::setWireKp(double kp, bool persist) {
    setWireGains(kp, _wirePi.getKi(), persist);
}

void ThermalPiControllers::setWireKi(double ki, bool persist) {
    setWireGains(_wirePi.getKp(), ki, persist);
}

void ThermalPiControllers::setFloorKp(double kp, bool persist) {
    setFloorGains(kp, _floorPi.getKi(), persist);
}

void ThermalPiControllers::setFloorKi(double ki, bool persist) {
    setFloorGains(_floorPi.getKp(), ki, persist);
}

void ThermalPiControllers::setWireGains(double kp, double ki, bool persist) {
    if (!isfinite(kp) || kp < 0.0f) kp = DEFAULT_WIRE_KP;
    if (!isfinite(ki) || ki < 0.0f) ki = DEFAULT_WIRE_KI;
    _wirePi.setGains(kp, ki);

    if (persist && CONF) {
        CONF->PutDouble(WIRE_KP_KEY, kp);
        CONF->PutDouble(WIRE_KI_KEY, ki);
    }
}

void ThermalPiControllers::setFloorGains(double kp, double ki, bool persist) {
    if (!isfinite(kp) || kp < 0.0f) kp = DEFAULT_FLOOR_KP;
    if (!isfinite(ki) || ki < 0.0f) ki = DEFAULT_FLOOR_KI;
    _floorPi.setGains(kp, ki);

    if (persist && CONF) {
        CONF->PutDouble(FLOOR_KP_KEY, kp);
        CONF->PutDouble(FLOOR_KI_KEY, ki);
    }
}

void ThermalPiControllers::loadFromNvs() {
    double wireKp = DEFAULT_WIRE_KP;
    double wireKi = DEFAULT_WIRE_KI;
    double floorKp = DEFAULT_FLOOR_KP;
    double floorKi = DEFAULT_FLOOR_KI;

    if (CONF) {
        wireKp = CONF->GetDouble(WIRE_KP_KEY, DEFAULT_WIRE_KP);
        wireKi = CONF->GetDouble(WIRE_KI_KEY, DEFAULT_WIRE_KI);
        floorKp = CONF->GetDouble(FLOOR_KP_KEY, DEFAULT_FLOOR_KP);
        floorKi = CONF->GetDouble(FLOOR_KI_KEY, DEFAULT_FLOOR_KI);
    }

    setWireGains(wireKp, wireKi, false);
    setFloorGains(floorKp, floorKi, false);
}
