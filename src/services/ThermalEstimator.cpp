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

    double maxPower = estimateMaxPowerW(calib);
    if (maxPower <= 0.0 && CONF) {
        const double iLim = CONF->GetFloat(CURR_LIMIT_KEY, DEFAULT_CURR_LIMIT_A);
        maxPower = iLim * CONF->GetFloat(AC_VOLTAGE_KEY, DEFAULT_AC_VOLTAGE);
    }
    if (maxPower < 1.0) maxPower = 1.0;
    out.maxPowerW = maxPower;

    return out;
}

void ThermalEstimator::persist(const Result& r) {
    if (CONF) {
        if (isfinite(r.tauSec))    CONF->PutDouble(WIRE_TAU_KEY, r.tauSec);
        if (isfinite(r.kLoss))     CONF->PutDouble(WIRE_K_LOSS_KEY, r.kLoss);
        if (isfinite(r.thermalC))  CONF->PutDouble(WIRE_C_TH_KEY, r.thermalC);
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
