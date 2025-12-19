/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#include "services/ThermalEstimator.h"
#include "services/CalibrationRecorder.h"
#include "system/Config.h"
#include "services/ThermalPiControllers.h"
#include <math.h>

ThermalEstimator* ThermalEstimator::Get() {
    static ThermalEstimator inst;
    return &inst;
}

ThermalEstimator::Result ThermalEstimator::computeSuggestions(const CalibrationRecorder* calib) const {
    Result out{};

    const double tau   = CONF ? CONF->GetDouble(WIRE_TAU_KEY, DEFAULT_WIRE_TAU_SEC)
                              : DEFAULT_WIRE_TAU_SEC;
    const double kLoss = CONF ? CONF->GetDouble(WIRE_K_LOSS_KEY, DEFAULT_WIRE_K_LOSS)
                              : DEFAULT_WIRE_K_LOSS;
    const double cTh   = CONF ? CONF->GetDouble(WIRE_C_TH_KEY, DEFAULT_WIRE_THERMAL_C)
                              : DEFAULT_WIRE_THERMAL_C;

    out.tauSec   = tau;
    out.kLoss    = kLoss;
    out.thermalC = cTh;

    out.wireKpCurrent  = CONF ? CONF->GetDouble(WIRE_KP_KEY, DEFAULT_WIRE_KP)
                              : DEFAULT_WIRE_KP;
    out.wireKiCurrent  = CONF ? CONF->GetDouble(WIRE_KI_KEY, DEFAULT_WIRE_KI)
                              : DEFAULT_WIRE_KI;
    out.floorKpCurrent = CONF ? CONF->GetDouble(FLOOR_KP_KEY, DEFAULT_FLOOR_KP)
                              : DEFAULT_FLOOR_KP;
    out.floorKiCurrent = CONF ? CONF->GetDouble(FLOOR_KI_KEY, DEFAULT_FLOOR_KI)
                              : DEFAULT_FLOOR_KI;

    double maxPower = estimateMaxPowerW(calib);
    if (maxPower <= 0.0 && CONF) {
        const double iLim = CONF->GetFloat(CURR_LIMIT_KEY, DEFAULT_CURR_LIMIT_A);
        maxPower = iLim * CONF->GetFloat(AC_VOLTAGE_KEY, DEFAULT_AC_VOLTAGE);
    }
    if (maxPower < 1.0) maxPower = 1.0;
    out.maxPowerW = maxPower;

    const double kEff = (kLoss > 1e-6) ? kLoss : DEFAULT_WIRE_K_LOSS;
    const double kWire = maxPower / kEff; // degC per duty

    // Choose conservative closed-loop time constants
    const double Tc_wire  = tau * 3.0;
    const double Tc_floor = tau * 9.0;

    if (isfinite(kWire) && kWire > 0.0 && isfinite(tau) && tau > 0.0) {
        out.wireKpSuggest = tau / (kWire * Tc_wire);
        out.wireKiSuggest = 1.0f / (kWire * Tc_wire);
    }

    if (isfinite(Tc_floor) && Tc_floor > 0.0) {
        // Assume plant gain ~1 degC floor per degC wire target
        out.floorKpSuggest = tau / Tc_floor;
        out.floorKiSuggest = 1.0f / Tc_floor;
    }

    return out;
}

void ThermalEstimator::persist(const Result& r) {
    if (CONF) {
        if (isfinite(r.tauSec))    CONF->PutDouble(WIRE_TAU_KEY, r.tauSec);
        if (isfinite(r.kLoss))     CONF->PutDouble(WIRE_K_LOSS_KEY, r.kLoss);
        if (isfinite(r.thermalC))  CONF->PutDouble(WIRE_C_TH_KEY, r.thermalC);
    }
    if (THERMAL_PI) {
        if (isfinite(r.wireKpSuggest) || isfinite(r.wireKiSuggest)) {
            const double kp = isfinite(r.wireKpSuggest) ? r.wireKpSuggest : THERMAL_PI->getWireKp();
            const double ki = isfinite(r.wireKiSuggest) ? r.wireKiSuggest : THERMAL_PI->getWireKi();
            THERMAL_PI->setWireGains(kp, ki, true);
        }
        if (isfinite(r.floorKpSuggest) || isfinite(r.floorKiSuggest)) {
            const double kp = isfinite(r.floorKpSuggest) ? r.floorKpSuggest : THERMAL_PI->getFloorKp();
            const double ki = isfinite(r.floorKiSuggest) ? r.floorKiSuggest : THERMAL_PI->getFloorKi();
            THERMAL_PI->setFloorGains(kp, ki, true);
        }
    }
}

double ThermalEstimator::estimateMaxPowerW(const CalibrationRecorder* calib) const {
    if (!calib) return 0.0f;
    CalibrationRecorder::Meta meta = calib->getMeta();
    const uint16_t total = meta.count;
    if (total == 0) return 0.0f;

    double maxP = 0.0f;
    CalibrationRecorder::Sample buf[32];
    uint16_t copied = 0;
    while (copied < total) {
        const size_t chunk = (total - copied) < 32 ? (total - copied) : 32;
        const size_t got = calib->copySamples(copied, buf, chunk);
        if (got == 0) break;
        for (size_t i = 0; i < got; ++i) {
            if (!isfinite(buf[i].voltageV) || !isfinite(buf[i].currentA)) continue;
            const double p = buf[i].voltageV * buf[i].currentA;
            if (isfinite(p) && p > maxP) maxP = p;
        }
        copied += got;
    }
    return maxP;
}
