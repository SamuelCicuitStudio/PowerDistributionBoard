#include "system/WireSubsystem.h"
#include "system/Config.h"
#include "control/CpDischg.h"
#include <math.h>
#include <vector>

// Forward DeviceState definition to avoid circular include here.
#include "system/Device.h"

// Thermal model constants (first-order)
static constexpr float WIRE_T_MAX_C     = 150.0f;
static constexpr float MAX_THERMAL_DT_S = 0.30f;  // cap per-step dt for stability
static constexpr float MAX_THERMAL_DT_TOTAL_S = 10.0f; // guard against huge gaps that would spin watchdog

// Helper: resolve ground-tie/charge resistor and sense-leak current
static float _getGroundTieOhms() {
    float r = DEFAULT_CHARGE_RESISTOR_OHMS;
    if (CONF) {
        r = CONF->GetFloat(CHARGE_RESISTOR_KEY, DEFAULT_CHARGE_RESISTOR_OHMS);
    }
    if (!isfinite(r) || r <= 0.0f) {
        r = DEFAULT_CHARGE_RESISTOR_OHMS;
    }
    return r;
}

static float _senseLeakCurrent(float busVoltage) {
    if (busVoltage <= 0.0f) return 0.0f;
    const float rtot = DIVIDER_TOP_OHMS + DIVIDER_BOTTOM_OHMS + _getGroundTieOhms();
    if (!(rtot > 0.0f && isfinite(rtot))) return 0.0f;
    return busVoltage / rtot;
}

// ======================================================================
// WireConfigStore
// ======================================================================

void WireConfigStore::loadFromNvs() {
    _wireOhmPerM  = CONF->GetFloat(WIRE_OHM_PER_M_KEY,  DEFAULT_WIRE_OHM_PER_M);
    _targetResOhm = CONF->GetFloat(R0XTGT_KEY,          DEFAULT_TARG_RES_OHMS);
    _wireGaugeAwg = CONF->GetInt(WIRE_GAUGE_KEY,        DEFAULT_WIRE_GAUGE);

    if (!isfinite(_wireOhmPerM)  || _wireOhmPerM  <= 0.0f) _wireOhmPerM  = DEFAULT_WIRE_OHM_PER_M;
    if (!isfinite(_targetResOhm) || _targetResOhm <= 0.0f) _targetResOhm = DEFAULT_TARG_RES_OHMS;
    if (_wireGaugeAwg <= 0 || _wireGaugeAwg > 60) _wireGaugeAwg = DEFAULT_WIRE_GAUGE;

    static const char* WIRE_RES_KEYS[HeaterManager::kWireCount] = {
        R01OHM_KEY, R02OHM_KEY, R03OHM_KEY, R04OHM_KEY, R05OHM_KEY,
        R06OHM_KEY, R07OHM_KEY, R08OHM_KEY, R09OHM_KEY, R10OHM_KEY
    };
    static const char* ACCESS_KEYS[HeaterManager::kWireCount] = {
        OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
        OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
    };

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        float r = CONF->GetFloat(WIRE_RES_KEYS[i], DEFAULT_WIRE_RES_OHMS);
        if (!isfinite(r) || r <= 0.01f) r = DEFAULT_WIRE_RES_OHMS;
        _wireR[i] = r;

        _access[i] = CONF->GetBool(ACCESS_KEYS[i], false);
    }
}

void WireConfigStore::saveToNvs() const {
    static const char* WIRE_RES_KEYS[HeaterManager::kWireCount] = {
        R01OHM_KEY, R02OHM_KEY, R03OHM_KEY, R04OHM_KEY, R05OHM_KEY,
        R06OHM_KEY, R07OHM_KEY, R08OHM_KEY, R09OHM_KEY, R10OHM_KEY
    };
    static const char* ACCESS_KEYS[HeaterManager::kWireCount] = {
        OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY, OUT04_ACCESS_KEY, OUT05_ACCESS_KEY,
        OUT06_ACCESS_KEY, OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY, OUT10_ACCESS_KEY
    };

    CONF->PutFloat(WIRE_OHM_PER_M_KEY, _wireOhmPerM);
    CONF->PutFloat(R0XTGT_KEY,         _targetResOhm);
    CONF->PutInt  (WIRE_GAUGE_KEY,     _wireGaugeAwg);

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        CONF->PutFloat(WIRE_RES_KEYS[i], _wireR[i]);
        CONF->PutBool(ACCESS_KEYS[i],    _access[i]);
    }
}

float WireConfigStore::getWireResistance(uint8_t index) const {
    if (index == 0 || index > HeaterManager::kWireCount) return DEFAULT_WIRE_RES_OHMS;
    return _wireR[index - 1];
}

void WireConfigStore::setWireResistance(uint8_t index, float ohms) {
    if (index == 0 || index > HeaterManager::kWireCount) return;
    if (!isfinite(ohms) || ohms <= 0.01f) return;
    _wireR[index - 1] = ohms;
}

bool WireConfigStore::getAccessFlag(uint8_t index) const {
    if (index == 0 || index > HeaterManager::kWireCount) return false;
    return _access[index - 1];
}

void WireConfigStore::setAccessFlag(uint8_t index, bool allowed) {
    if (index == 0 || index > HeaterManager::kWireCount) return;
    _access[index - 1] = allowed;
}

float WireConfigStore::getTargetResOhm() const {
    return _targetResOhm;
}

void WireConfigStore::setTargetResOhm(float ohms) {
    if (isfinite(ohms) && ohms > 0.0f) _targetResOhm = ohms;
}

float WireConfigStore::getWireOhmPerM() const {
    return _wireOhmPerM;
}

void WireConfigStore::setWireOhmPerM(float v) {
    if (isfinite(v) && v > 0.0f) _wireOhmPerM = v;
}

int WireConfigStore::getWireGaugeAwg() const {
    return _wireGaugeAwg;
}

void WireConfigStore::setWireGaugeAwg(int awg) {
    if (awg > 0 && awg <= 60) _wireGaugeAwg = awg;
}

// ======================================================================
// WireStateModel
// ======================================================================

WireRuntimeState& WireStateModel::wire(uint8_t index) {
    static WireRuntimeState dummy;
    if (index == 0 || index > HeaterManager::kWireCount) return dummy;
    return _wire[index - 1];
}

const WireRuntimeState& WireStateModel::wire(uint8_t index) const {
    static WireRuntimeState dummy;
    if (index == 0 || index > HeaterManager::kWireCount) return dummy;
    return _wire[index - 1];
}

uint16_t WireStateModel::getLastMask() const {
    return _lastMask;
}

void WireStateModel::setLastMask(uint16_t m) {
    _lastMask = m & ((1u << HeaterManager::kWireCount) - 1u);
}

// ======================================================================
// WireThermalModel
// ======================================================================

void WireThermalModel::init(const HeaterManager& heater, float ambientC) {
    _ambientC = ambientC;
    const uint32_t now = millis();

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        WireInfo wi = heater.getWireInfo(i + 1);
        WireThermalState& ws = _state[i];

        ws.R0 = (wi.resistanceOhm > 0.01f) ? wi.resistanceOhm : 1.0f;
        ws.T                = ambientC;
        ws.lastUpdateMs     = now;
        ws.locked           = false;
        ws.cooldownReleaseMs = 0;

        // Also prime HeaterManager's cached temperature.
        const_cast<HeaterManager&>(heater).setWireEstimatedTemp(i + 1, ws.T);
    }
    _initialized = true;
}

float WireThermalModel::wireResistanceAtTemp(uint8_t idx) const {
    if (idx >= HeaterManager::kWireCount) return 1e6f;
    const WireThermalState& ws = _state[idx];
    // Simplified: keep resistance fixed at the cold value.
    return ws.R0;
}

void WireThermalModel::advanceWireTemp(WireThermalState& ws, float ambientC, float powerW, float dtS) {
    if (!(isfinite(dtS) && dtS > 0.0f)) return;

    float remaining = dtS;
    // Prevent excessive sub-steps if timestamps jump (keeps task watchdog happy).
    if (remaining > MAX_THERMAL_DT_TOTAL_S) remaining = MAX_THERMAL_DT_TOTAL_S;
    const float C = _thermalMassC;
    float k = _heatLossK;
    if (!isfinite(C) || C <= 0.0f) return;
    if (!isfinite(k) || k < 0.0f) k = 0.0f;

    while (remaining > 0.0f) {
        float step = remaining;
        if (step > MAX_THERMAL_DT_S) step = MAX_THERMAL_DT_S;
        const float dT = ((powerW - k * (ws.T - ambientC)) / C) * step;
        ws.T += dT;
        remaining -= step;
    }
}

void WireThermalModel::integrateCurrentOnly(const CurrentSensor::Sample* curBuf, size_t nCur,
                                            const HeaterManager::OutputEvent* outBuf, size_t nOut,
                                            float ambientC,
                                            WireStateModel& runtime, HeaterManager& heater) {
    if (!_initialized) {
        init(heater, ambientC);
    }
    _ambientC = ambientC;

    uint16_t currentMask = runtime.getLastMask();
    size_t   outIndex    = 0;

    auto handleLockout = [&](uint8_t w,
                             WireThermalState& ws,
                             WireRuntimeState& rt) {
        ws.locked = false; // disable latch; rely on PWM/error control
        rt.locked   = false;
        rt.overTemp = isfinite(rt.tempC) && rt.tempC >= WIRE_T_MAX_C;
    };

    for (size_t i = 0; i < nCur; ++i) {
        const uint32_t ts    = curBuf[i].timestampMs;
        const float    Imeas = curBuf[i].currentA;
        (void)Imeas;

        // Apply all mask changes up to this sample timestamp.
        while (outIndex < nOut && outBuf[outIndex].timestampMs <= ts) {
            currentMask = outBuf[outIndex].mask;
            ++outIndex;
        }

        float V_branch = NAN;
        if (currentMask != 0 && isfinite(Imeas) && Imeas > 0.0f) {
            float Gtot = 0.0f;
            for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
                if (currentMask & (1u << w)) {
                    float R = wireResistanceAtTemp(w);
                    if (R > 0.01f && isfinite(R)) {
                        Gtot += 1.0f / R;
                    }
                }
            }
            if (Gtot > 0.0f) {
                const float Rpar = 1.0f / Gtot;
                V_branch = Imeas * Rpar;
            }
        }

        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            const uint16_t bit = (1u << w);
            float dt = (ws.lastUpdateMs == 0) ? 0.0f : (ts - ws.lastUpdateMs) * 0.001f;

            float P = 0.0f;
            if ((currentMask & bit) && isfinite(V_branch) && V_branch > 0.0f) {
                float R = wireResistanceAtTemp(w);
                if (R > 0.01f && isfinite(R)) {
                    const float Iw = V_branch / R;
                    P = Iw * Iw * R;
                }
            }

            advanceWireTemp(ws, _ambientC, P, dt);

            WireRuntimeState& rt = runtime.wire(w + 1);
            rt.lastPowerW   = (currentMask & bit) ? P : 0.0f;
        }

        // Clamp and publish temps after each current sample.
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            if (ws.T > WIRE_T_MAX_C) ws.T = WIRE_T_MAX_C;
            if (ws.T < _ambientC - 10.0f) ws.T = _ambientC - 10.0f;

            WireRuntimeState& rt = runtime.wire(w + 1);
            rt.tempC        = ws.T;
            rt.lastUpdateMs = ts;

            handleLockout(w, ws, rt);

            heater.setWireEstimatedTemp(w + 1, ws.T);
            ws.lastUpdateMs = ts;
        }
    }

    runtime.setLastMask(currentMask);
}

void WireThermalModel::coolingOnlyTick(float ambientC,
                                       WireStateModel& runtime,
                                       HeaterManager& heater) {
    if (!_initialized) {
        init(heater, ambientC);
    }
    _ambientC = ambientC;

    uint16_t currentMask = runtime.getLastMask();

    const uint32_t nowTs = millis();
    for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
        WireThermalState& ws = _state[w];
        float dt = (ws.lastUpdateMs == 0) ? 0.0f : (nowTs - ws.lastUpdateMs) * 0.001f;
        advanceWireTemp(ws, _ambientC, 0.0f, dt);

        if (ws.T > WIRE_T_MAX_C) ws.T = WIRE_T_MAX_C;
        if (ws.T < _ambientC - 10.0f) ws.T = _ambientC - 10.0f;

        WireRuntimeState& rt = runtime.wire(w + 1);
        rt.tempC        = ws.T;
        rt.lastPowerW   = 0.0f;
        rt.lastUpdateMs = nowTs;

        ws.locked = false;
        rt.locked   = false;
        rt.overTemp = isfinite(rt.tempC) && rt.tempC >= WIRE_T_MAX_C;

        heater.setWireEstimatedTemp(w + 1, ws.T);
        ws.lastUpdateMs = nowTs;
    }

    runtime.setLastMask(currentMask);
}

void WireThermalModel::integrateCapModel(const CpDischg::Sample* voltBuf, size_t nVolt,
                                         const HeaterManager::OutputEvent* outBuf, size_t nOut,
                                         float capF, float vSrc, float rChargeOhm,
                                         float ambientC,
                                         WireStateModel& runtime, HeaterManager& heater) {
    if (!_initialized) {
        init(heater, ambientC);
    }
    _ambientC = ambientC;

    const float C = capF;
    if (!(isfinite(C) && C > 0.0f)) {
        // No capacitance known: only apply cooling.
        const uint32_t nowTs = millis();
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            float dt = (ws.lastUpdateMs == 0) ? 0.0f : (nowTs - ws.lastUpdateMs) * 0.001f;
            advanceWireTemp(ws, _ambientC, 0.0f, dt);
            ws.lastUpdateMs = nowTs;
            heater.setWireEstimatedTemp(w + 1, ws.T);
            WireRuntimeState& rt = runtime.wire(w + 1);
            rt.tempC        = ws.T;
            rt.lastUpdateMs = nowTs;
        }
        return;
    }

    float rCharge = rChargeOhm;
    if (!isfinite(rCharge) || rCharge <= 0.0f) {
        rCharge = INFINITY; // no source / open relay
    }
    float vS = (isfinite(vSrc) && vSrc > 0.0f) ? vSrc : 0.0f;

    uint16_t currentMask = runtime.getLastMask();

    auto handleLockout = [&](uint8_t w,
                             WireThermalState& ws,
                             WireRuntimeState& rt) {
        ws.locked = false; // disable latch; rely on PWM/error control
        rt.locked   = false;
        rt.overTemp = isfinite(rt.tempC) && rt.tempC >= WIRE_T_MAX_C;
    };

    auto applyCoolingTo = [&](uint32_t ts) {
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            float dt = (ws.lastUpdateMs == 0) ? 0.0f : (ts - ws.lastUpdateMs) * 0.001f;
            advanceWireTemp(ws, _ambientC, 0.0f, dt);
            ws.lastUpdateMs = ts;
            WireRuntimeState& rt = runtime.wire(w + 1);
            if (!(currentMask & (1u << w))) {
                rt.lastPowerW = 0.0f;
            }
        }
    };

    auto equivalentResistance = [&](uint16_t mask) -> float {
        float G = 0.0f;
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            if (!(mask & (1u << w))) continue;
            float R = wireResistanceAtTemp(w);
            if (R > 0.01f && isfinite(R)) {
                G += 1.0f / R;
            }
        }
        if (!(G > 0.0f)) return INFINITY;
        return 1.0f / G;
    };

    auto applyHeatSegment = [&](uint16_t mask,
                                float v0,
                                float dtS) -> float {
        if (!(mask != 0 && isfinite(v0) && v0 > 0.0f && isfinite(dtS) && dtS > 0.0f)) {
            return v0;
        }

        const float Rpar = equivalentResistance(mask);
        if (!isfinite(Rpar) || Rpar <= 0.0f) {
            return v0;
        }

        const float Eload = CapModel::energyToLoadJ(v0, dtS, C, Rpar, vS, rCharge);
        const float v1    = CapModel::predictVoltage(v0, dtS, C, Rpar, vS, rCharge);

        // Distribute load energy across parallel branches by conductance fraction.
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            if (!(mask & (1u << w))) continue;

            WireThermalState& ws = _state[w];
            float R = wireResistanceAtTemp(w);
            if (!(R > 0.01f && isfinite(R))) continue;

            const float frac = Rpar / R; // (1/R)/Gtot
            float Ew = Eload * frac;
            if (!isfinite(Ew) || Ew < 0.0f) Ew = 0.0f;
            if (_thermalMassC > 0.0f && isfinite(_thermalMassC)) {
                ws.T += (Ew / _thermalMassC);
            }

            WireRuntimeState& rt = runtime.wire(w + 1);
            rt.lastPowerW = (dtS > 0.0f) ? (Ew / dtS) : 0.0f;
        }

        return v1;
    };

    // Advance last seen bus voltage from voltBuf.
    size_t vIndex = 0;
    auto updateBusVTo = [&](uint32_t ts) {
        if (!voltBuf || nVolt == 0) return;
        while (vIndex < nVolt && voltBuf[vIndex].timestampMs <= ts) {
            float v = voltBuf[vIndex].voltageV;
            if (isfinite(v)) {
                _lastBusV = v;
            }
            ++vIndex;
        }
    };

    // Process mask transitions as pulse segments.
    for (size_t i = 0; i < nOut; ++i) {
        const uint32_t ts = outBuf[i].timestampMs;
        const uint16_t newMask = outBuf[i].mask;

        updateBusVTo(ts);
        applyCoolingTo(ts);

        if (newMask != currentMask) {
            // End any active segment (currentMask) at this timestamp.
            if (_pulseActive && currentMask != 0 && currentMask == _pulseMask && ts > _pulseStartMs) {
                const float dtS = (ts - _pulseStartMs) * 0.001f;
                (void)applyHeatSegment(_pulseMask, _pulseStartV, dtS);
            }

            // Start new segment if newMask is non-zero.
            if (newMask != 0) {
                _pulseActive  = true;
                _pulseMask    = newMask;
                _pulseStartMs = ts;
                _pulseStartV  = isfinite(_lastBusV) ? _lastBusV : vS;
            } else {
                _pulseActive  = false;
                _pulseMask    = 0;
                _pulseStartMs = 0;
                _pulseStartV  = NAN;
            }

            currentMask = newMask;
        }
    }

    // Apply cooling (and partial heating if a pulse is still active) up to "now".
    const uint32_t nowTs = millis();
    updateBusVTo(nowTs);
    applyCoolingTo(nowTs);

    if (_pulseActive && _pulseMask != 0 && nowTs > _pulseStartMs) {
        const float dtS = (nowTs - _pulseStartMs) * 0.001f;
        const float v0  = isfinite(_pulseStartV) ? _pulseStartV : (isfinite(_lastBusV) ? _lastBusV : vS);
        const float v1  = applyHeatSegment(_pulseMask, v0, dtS);
        _pulseStartMs = nowTs;
        _pulseStartV  = v1;
    }

    // Clamp, publish, and enforce lockouts.
    for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
        WireThermalState& ws = _state[w];
        if (ws.T > WIRE_T_MAX_C) ws.T = WIRE_T_MAX_C;
        if (ws.T < _ambientC - 10.0f) ws.T = _ambientC - 10.0f;

        WireRuntimeState& rt = runtime.wire(w + 1);
        rt.tempC        = ws.T;
        rt.lastUpdateMs = nowTs;

        handleLockout(w, ws, rt);

        heater.setWireEstimatedTemp(w + 1, ws.T);
        ws.lastUpdateMs = nowTs;
    }

    runtime.setLastMask(currentMask);
}

void WireThermalModel::integrate(const CurrentSensor::Sample* curBuf, size_t nCur,
                                 const CpDischg::Sample*     voltBuf, size_t nVolt,
                                 const HeaterManager::OutputEvent* outBuf, size_t nOut,
                                 float idleCurrentA, float ambientC,
                                 WireStateModel& runtime, HeaterManager& heater) {
    if (!_initialized) {
        init(heater, ambientC);
    }
    _ambientC = ambientC;

    uint16_t currentMask = runtime.getLastMask();
    size_t   outIndex    = 0;

    for (size_t i = 0; i < nCur; ++i) {
        const uint32_t ts    = curBuf[i].timestampMs;
        const float    Imeas = curBuf[i].currentA;
        // Find closest voltage sample at/after this timestamp.
        float Vmeas = NAN;
        if (voltBuf && nVolt > 0) {
            // Simple nearest: assume voltBuf is in ascending time.
            size_t vi = 0;
            while (vi + 1 < nVolt && voltBuf[vi + 1].timestampMs <= ts) {
                ++vi;
            }
            Vmeas = voltBuf[vi].voltageV;
            // If next sample is closer in time, pick it.
            if (vi + 1 < nVolt) {
                uint32_t dt0 = ts - voltBuf[vi].timestampMs;
                uint32_t dt1 = voltBuf[vi + 1].timestampMs - ts;
                if (dt1 < dt0) {
                    Vmeas = voltBuf[vi + 1].voltageV;
                }
            }
        }

        // Apply all mask changes up to this sample timestamp.
        while (outIndex < nOut && outBuf[outIndex].timestampMs <= ts) {
            currentMask = outBuf[outIndex].mask;
            ++outIndex;
        }

        // Heating + cooling (first-order model).
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            const uint16_t bit = (1u << w);
            float dt = (ws.lastUpdateMs == 0) ? 0.0f : (ts - ws.lastUpdateMs) * 0.001f;

            float P = 0.0f;
            if ((currentMask & bit) && isfinite(Vmeas) && Vmeas > 0.0f) {
                float R = wireResistanceAtTemp(w);
                if (R > 0.01f && isfinite(R)) {
                    const float Iw = Vmeas / R;
                    P = Iw * Iw * R;
                }
            }

            advanceWireTemp(ws, _ambientC, P, dt);

            WireRuntimeState& rt = runtime.wire(w + 1);
            rt.lastPowerW = (currentMask & bit) ? P : 0.0f;
        }

        // Clamp and publish temps.
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            if (ws.T > WIRE_T_MAX_C) ws.T = WIRE_T_MAX_C;
            if (ws.T < _ambientC - 10.0f) ws.T = _ambientC - 10.0f;

            WireRuntimeState& rt = runtime.wire(w + 1);
            rt.tempC        = ws.T;
            rt.lastUpdateMs = ts;
            ws.locked = false;
            rt.locked   = false;
            rt.overTemp = isfinite(rt.tempC) && rt.tempC >= WIRE_T_MAX_C;

            heater.setWireEstimatedTemp(w + 1, ws.T);
            ws.lastUpdateMs = ts;
        }
    }

    runtime.setLastMask(currentMask);
}

float WireThermalModel::getWireTemp(uint8_t index) const {
    if (index == 0 || index > HeaterManager::kWireCount) return NAN;
    return _state[index - 1].T;
}

void WireThermalModel::setThermalParams(float tauSec, float kLoss, float thermalMassC) {
    if (!isfinite(tauSec) || tauSec <= 0.0f) {
        tauSec = DEFAULT_WIRE_TAU_SEC;
    }
    if (!isfinite(kLoss) || kLoss < 0.0f) {
        kLoss = DEFAULT_WIRE_K_LOSS;
    }
    if (!isfinite(thermalMassC) || thermalMassC <= 0.0f) {
        thermalMassC = DEFAULT_WIRE_THERMAL_C;
    }
    _tauSec       = tauSec;
    _heatLossK    = kLoss;
    _thermalMassC = thermalMassC;
}

bool WireThermalModel::applyExternalWireTemp(uint8_t index, float tempC, uint32_t tsMs,
                                             WireStateModel& runtime, HeaterManager& heater) {
    if (index == 0 || index > HeaterManager::kWireCount) return false;
    if (!isfinite(tempC)) return false;

    WireThermalState& ws = _state[index - 1];
    const uint32_t ts = (tsMs != 0) ? tsMs : millis();

    ws.T = tempC;
    if (ws.T > WIRE_T_MAX_C) ws.T = WIRE_T_MAX_C;
    if (ws.T < _ambientC - 10.0f) ws.T = _ambientC - 10.0f;
    ws.lastUpdateMs = ts;
    ws.locked = false;

    WireRuntimeState& rt = runtime.wire(index);
    rt.tempC        = ws.T;
    rt.lastUpdateMs = ts;
    rt.locked       = false;
    rt.overTemp     = isfinite(rt.tempC) && rt.tempC >= WIRE_T_MAX_C;

    heater.setWireEstimatedTemp(index, ws.T);
    return true;
}

// ======================================================================
// WirePresenceManager
// ======================================================================

void WirePresenceManager::probeAll(HeaterManager& heater,
                                   WireStateModel& state,
                                   CurrentSensor& cs,
                                   float busVoltage,
                                   float minValidFraction,
                                   float maxValidFraction,
                                   uint16_t settleMs,
                                   uint8_t samples) {
    if (busVoltage <= 0.0f) return;
    (void)maxValidFraction; // detection now hinges on absolute current

    const bool forcePresence = (DEVICE_FORCE_ALL_WIRES_PRESENT != 0);
    const float minDetectA = (minValidFraction > 0.0f) ? minValidFraction : 0.05f;

    bool prevStates[HeaterManager::kWireCount];
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        prevStates[i] = heater.getOutputState(i + 1);
    }

    heater.setOutputMask(0);
    vTaskDelay(pdMS_TO_TICKS(settleMs));

    for (uint8_t idx = 0; idx < HeaterManager::kWireCount; ++idx) {
        const uint8_t wireIdx = idx + 1;
        WireInfo wi = heater.getWireInfo(wireIdx);
        float R = wi.resistanceOhm;
        if (!isfinite(R) || R <= 0.01f) {
            WireRuntimeState& rt = state.wire(wireIdx);
            rt.present      = false;
            rt.lastUpdateMs = millis();
            heater.setWirePresence(wireIdx, false, 0.0f);
            continue;
        }

        uint16_t mask = (1u << idx);
        heater.setOutputMask(mask);
        vTaskDelay(pdMS_TO_TICKS(settleMs));

        float sumA = 0.0f;
        for (uint8_t s = 0; s < samples; ++s) {
            sumA += cs.readCurrent();
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        float Imeas = sumA / (float)samples;

        const float leak = _senseLeakCurrent(busVoltage);
        float ImeasNet = Imeas - leak;
        if (ImeasNet < 0.0f) ImeasNet = 0.0f;

        bool connected = forcePresence ||
                         (isfinite(ImeasNet) && ImeasNet >= minDetectA);

        heater.setWirePresence(wireIdx, connected, ImeasNet);

        WireRuntimeState& rt = state.wire(wireIdx);
        rt.present      = connected;
        rt.lastUpdateMs = millis();

        heater.setOutputMask(0);
        vTaskDelay(pdMS_TO_TICKS(settleMs));
    }

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        if (prevStates[i]) heater.setOutput(i + 1, true);
    }

    state.setLastMask(heater.getOutputMask());
}

void WirePresenceManager::updatePresenceFromMask(HeaterManager& heater,
                                                 WireStateModel& state,
                                                 uint16_t mask,
                                                 float totalCurrentA,
                                                 float busVoltage,
                                                 float minValidRatio) {
    if (mask == 0) return;

    const bool forcePresence = (DEVICE_FORCE_ALL_WIRES_PRESENT != 0);
    const float minDetectA = (minValidRatio > 0.0f) ? minValidRatio : 0.05f;

    float netCurrent = totalCurrentA - _senseLeakCurrent(busVoltage);
    if (netCurrent < 0.0f) netCurrent = 0.0f;
    const bool connected = forcePresence ||
                           (isfinite(netCurrent) && netCurrent >= minDetectA);

    const uint32_t now = millis();
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        if (!(mask & (1u << i))) continue;
        WireRuntimeState& rt = state.wire(i + 1);

        rt.present      = connected;
        rt.lastUpdateMs = now;
        heater.setWirePresence(i + 1, connected, netCurrent);
    }
}

bool WirePresenceManager::hasAnyConnected(const WireStateModel& state) const {
    if (DEVICE_FORCE_ALL_WIRES_PRESENT != 0) return true;
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        if (state.wire(i + 1).present) return true;
    }
    return false;
}

// ======================================================================
// WirePlanner
// ======================================================================

uint16_t WirePlanner::chooseMask(const WireConfigStore& cfg,
                                 const WireStateModel&  state,
                                 float targetResOhm) const {
    if (!isfinite(targetResOhm) || targetResOhm <= 0.0f) {
        targetResOhm = cfg.getTargetResOhm();
    }

    const uint16_t FULL = (1u << HeaterManager::kWireCount);
    const bool forcePresence = (DEVICE_FORCE_ALL_WIRES_PRESENT != 0);

    // Build allowed mask from access flags + presence (planner-level gating).
    uint16_t allowedMask = 0;
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        const WireRuntimeState& rt = state.wire(i + 1);
        if (cfg.getAccessFlag(i + 1) && (forcePresence || rt.present)) {
            allowedMask |= (1u << i);
        }
    }
    if (allowedMask == 0) {
        return 0;
    }

    uint16_t chosen = 0;

    // Only consider masks reasonably close to the target (cold).
    const float tol = max(targetResOhm * 0.15f, 1.0f); // 15% or 1 ohm

    // Fairness weight: penalize masks that use recently used wires.
    const float fairnessK = max(targetResOhm * 0.05f, 0.5f);

    float bestScore = INFINITY;
    float bestErr   = INFINITY;

    for (uint16_t m = 1; m < FULL; ++m) {
        if ((m & ~allowedMask) != 0) continue; // uses disallowed wire

        float Req = equivalentResistance(cfg, state, m);
        if (!isfinite(Req) || Req <= 0.0f) continue;

        float err = fabsf(Req - targetResOhm);

        // Skip masks far from target; if none meet tolerance we'll fall back later.
        if (err > tol) continue;

        float usageSum = 0.0f;
        for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
            if (m & (1u << i)) {
                usageSum += state.wire(i + 1).usageScore;
            }
        }

        float score = err + fairnessK * usageSum;
        if (score < bestScore || (fabsf(score - bestScore) <= 1e-6f && err < bestErr)) {
            bestScore = score;
            bestErr   = err;
            chosen    = m;
        }
    }

    // If no mask satisfied tolerance, fall back to best score across all.
    if (chosen == 0) {
        for (uint16_t m = 1; m < FULL; ++m) {
            if ((m & ~allowedMask) != 0) continue;
            float Req = equivalentResistance(cfg, state, m);
            if (!isfinite(Req) || Req <= 0.0f) continue;
            float err = fabsf(Req - targetResOhm);
            float usageSum = 0.0f;
            for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
                if (m & (1u << i)) {
                    usageSum += state.wire(i + 1).usageScore;
                }
            }
            float score = err + fairnessK * usageSum;
            if (score < bestScore || (fabsf(score - bestScore) <= 1e-6f && err < bestErr)) {
                bestScore = score;
                bestErr   = err;
                chosen    = m;
            }
        }
    }

    _lastChosenMask = chosen;
    return chosen;
}

float WirePlanner::equivalentResistance(const WireConfigStore& cfg,
                                         const WireStateModel&  state,
                                         uint16_t mask) const {
    const bool forcePresence = (DEVICE_FORCE_ALL_WIRES_PRESENT != 0);
    float G = 0.0f;
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        if (!(mask & (1u << i))) continue;
        const WireRuntimeState& rt = state.wire(i + 1);
        if (!forcePresence && !rt.present) {
            return INFINITY; // mask relies on a missing wire
        }
        float R = cfg.getWireResistance(i + 1);
        if (R <= 0.01f || !isfinite(R)) {
            continue;
        }
        G += 1.0f / R;
    }
    if (G <= 0.0f) return INFINITY;
    return 1.0f / G;
}

// ======================================================================
// WireSafetyPolicy
// ======================================================================

uint16_t WireSafetyPolicy::filterMask(uint16_t requestedMask,
                                      const WireConfigStore& cfg,
                                      const WireStateModel&  state,
                                      DeviceState            devState) const {
    uint16_t mask = requestedMask & ((1u << HeaterManager::kWireCount) - 1u);

    if (devState != DeviceState::Running) {
        return 0;
    }

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        uint16_t bit = (1u << i);
        if (!(mask & bit)) continue;

        const WireRuntimeState& rt = state.wire(i + 1);
        bool access = cfg.getAccessFlag(i + 1);
        bool presentOk = (DEVICE_FORCE_ALL_WIRES_PRESENT != 0) ? true : rt.present;

        // Presence/thermal gating enforced unless override is on.
        if (!access || rt.overTemp || rt.locked || !presentOk) {
            mask &= ~bit;
        }
    }
    return mask;
}

// ======================================================================
// WireActuator
// ======================================================================

uint16_t WireActuator::applyRequestedMask(uint16_t requestedMask,
                                          const WireConfigStore& cfg,
                                          WireStateModel&        state,
                                          DeviceState            devState) {
    WireSafetyPolicy policy;
    uint16_t safeMask = policy.filterMask(requestedMask, cfg, state, devState);
    if (WIRE) {
        WIRE->setOutputMask(safeMask);
    }
    state.setLastMask(safeMask);
    return safeMask;
}

// ======================================================================
// WireTelemetryAdapter
// ======================================================================

void WireTelemetryAdapter::fillSnapshot(StatusSnapshot& out,
                                        const WireConfigStore& /*cfg*/,
                                        const WireStateModel&  state) const {
    float totalP = 0.0f;
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        const WireRuntimeState& rt = state.wire(i + 1);
        out.wireTemps[i] = rt.tempC;
        out.outputs[i]   = ((state.getLastMask() & (1u << i)) != 0);
        totalP += rt.lastPowerW;
    }
    (void)totalP; // total power can be added to StatusSnapshot later if desired.
}

void WireTelemetryAdapter::writeMonitorJson(JsonObject& root,
                                            const StatusSnapshot& snap) const {
    JsonArray wireTemps = root.createNestedArray("wireTemps");
    JsonObject outputs   = root.createNestedObject("outputs");

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        wireTemps.add(snap.wireTemps[i]);
        outputs[String("output") + String(i + 1)] = snap.outputs[i];
    }
}
