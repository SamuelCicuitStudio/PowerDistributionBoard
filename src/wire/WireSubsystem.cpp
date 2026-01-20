#include <WireSubsystem.hpp>
#include <Config.hpp>
#include <CpDischg.hpp>
#include <math.h>
#include <vector>

// Forward DeviceState definition to avoid circular include here.
#include <Device.hpp>

// Thermal model constants (first-order)
static constexpr double WIRE_T_MAX_C     = 150.0;
static constexpr double WIRE_LOCK_MARGIN_C = 10.0;
static constexpr double WIRE_LOCK_RELEASE_MARGIN_C = 5.0;
static constexpr uint32_t WIRE_LOCK_MIN_COOLDOWN_MS = 500;
static constexpr double WIRE_RES_SCALE_MIN = 0.2;
static constexpr double WIRE_RES_SCALE_MAX = 3.0;
static constexpr double WIRE_AMBIENT_CLAMP_C = 10.0;
static constexpr double NICHROME_ALPHA = 0.00017;
static constexpr double MAX_THERMAL_DT_S = 0.30;  // cap per-step dt for stability
static constexpr double MAX_THERMAL_DT_TOTAL_S = 10.0; // guard against huge gaps that would spin watchdog

static double resolveWireMaxTempC() {
    static double cached = WIRE_T_MAX_C;
    static uint32_t lastMs = 0;
    const uint32_t nowMs = millis();
    if (lastMs != 0 && (nowMs - lastMs) < 1000) {
        return cached;
    }
    lastMs = nowMs;
    double maxC = WIRE_T_MAX_C;
    if (CONF) {
        const float cfg = CONF->GetFloat(NICHROME_FINAL_TEMP_C_KEY,
                                         DEFAULT_NICHROME_FINAL_TEMP_C);
        if (isfinite(cfg) && cfg > 0.0f && cfg < maxC) {
            maxC = cfg;
        }
    }
    cached = maxC;
    return cached;
}

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
    _wireGaugeAwg = CONF->GetInt(WIRE_GAUGE_KEY,        DEFAULT_WIRE_GAUGE);

    if (!isfinite(_wireOhmPerM)  || _wireOhmPerM  <= 0.0f) _wireOhmPerM  = DEFAULT_WIRE_OHM_PER_M;
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

void WireThermalModel::init(const HeaterManager& heater, double ambientC) {
    _ambientC = ambientC;
    const uint32_t now = millis();

    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        WireInfo wi = heater.getWireInfo(i + 1);
        WireThermalState& ws = _state[i];

        ws.R0 = (wi.resistanceOhm > 0.01f) ? wi.resistanceOhm : 1.0;
        ws.T                = ambientC;
        ws.lastUpdateMs     = now;
        ws.locked           = false;
        ws.cooldownReleaseMs = 0;
        ws.tauSec           = _tauSec;
        ws.kLoss            = _heatLossK;
        double capC = _thermalMassC;
        if (isfinite(wi.massKg) && wi.massKg > 0.0f) {
            const double cFromMass =
                static_cast<double>(wi.massKg) * NICHROME_SPECIFIC_HEAT;
            if (isfinite(cFromMass) && cFromMass > 0.0) {
                capC = cFromMass;
            }
        }
        ws.capC             = capC;

        // Also prime HeaterManager's cached temperature.
        const_cast<HeaterManager&>(heater).setWireEstimatedTemp(i + 1, ws.T);
    }
    _initialized = true;
}

double WireThermalModel::wireResistanceAtTemp(uint8_t idx) const {
    if (idx >= HeaterManager::kWireCount) return 1e6;
    const WireThermalState& ws = _state[idx];
    double r0 = (isfinite(ws.R0) && ws.R0 > 0.01) ? ws.R0 : 1.0;
    double t = ws.T;
    if (!isfinite(t)) t = _ambientC;
    const double dT = t - _ambientC;
    double scale = 1.0 + NICHROME_ALPHA * dT;
    if (scale < WIRE_RES_SCALE_MIN) scale = WIRE_RES_SCALE_MIN;
    if (scale > WIRE_RES_SCALE_MAX) scale = WIRE_RES_SCALE_MAX;
    return r0 * scale;
}

void WireThermalModel::advanceWireTemp(WireThermalState& ws, double ambientC, double powerW, double dtS) {
    if (!(isfinite(dtS) && dtS > 0.0)) return;

    double remaining = dtS;
    // Prevent excessive sub-steps if timestamps jump (keeps task watchdog happy).
    if (remaining > MAX_THERMAL_DT_TOTAL_S) remaining = MAX_THERMAL_DT_TOTAL_S;
    double C = (isfinite(ws.capC) && ws.capC > 0.0) ? ws.capC : _thermalMassC;
    double k = (isfinite(ws.kLoss) && ws.kLoss >= 0.0) ? ws.kLoss : _heatLossK;
    if (!isfinite(C) || C <= 0.0) return;
    if (!isfinite(k) || k < 0.0) k = 0.0;

    while (remaining > 0.0) {
        double step = remaining;
        if (step > MAX_THERMAL_DT_S) step = MAX_THERMAL_DT_S;
        const double dT = ((powerW - k * (ws.T - ambientC)) / C) * step;
        ws.T += dT;
        remaining -= step;
    }
}

void WireThermalModel::applyThermalGuards(WireThermalState& ws,
                                          WireRuntimeState& rt,
                                          uint32_t tsMs) {
    const double maxC = resolveWireMaxTempC();
    if (ws.T > maxC) ws.T = maxC;
    if (ws.T < _ambientC - WIRE_AMBIENT_CLAMP_C) {
        ws.T = _ambientC - WIRE_AMBIENT_CLAMP_C;
    }

    double lockTemp = maxC - WIRE_LOCK_MARGIN_C;
    if (lockTemp < 0.0) lockTemp = maxC;
    double releaseTemp = lockTemp - WIRE_LOCK_RELEASE_MARGIN_C;
    if (releaseTemp < 0.0) releaseTemp = 0.0;

    if (!ws.locked) {
        if (ws.T >= lockTemp) {
            ws.locked = true;
            ws.cooldownReleaseMs = tsMs + WIRE_LOCK_MIN_COOLDOWN_MS;
        }
    } else {
        if (tsMs >= ws.cooldownReleaseMs && ws.T <= releaseTemp) {
            ws.locked = false;
        }
    }

    rt.tempC     = ws.T;
    rt.locked    = ws.locked;
    rt.overTemp  = isfinite(ws.T) && ws.T >= maxC;
}

void WireThermalModel::integrateCurrentOnly(const CurrentSensor::Sample* curBuf, size_t nCur,
                                            const HeaterManager::OutputEvent* outBuf, size_t nOut,
                                            double ambientC,
                                            WireStateModel& runtime, HeaterManager& heater) {
    if (!_initialized) {
        init(heater, ambientC);
    }
    _ambientC = ambientC;

    uint16_t currentMask = runtime.getLastMask();
    size_t   outIndex    = 0;

    for (size_t i = 0; i < nCur; ++i) {
        const uint32_t ts    = curBuf[i].timestampMs;
        const double   Imeas = curBuf[i].currentA;
        (void)Imeas;

        // Apply all mask changes up to this sample timestamp.
        while (outIndex < nOut && outBuf[outIndex].timestampMs <= ts) {
            currentMask = outBuf[outIndex].mask;
            ++outIndex;
        }

        double V_branch = NAN;
        if (currentMask != 0 && isfinite(Imeas) && Imeas > 0.0) {
            double Gtot = 0.0;
            for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
                if (currentMask & (1u << w)) {
                    double R = wireResistanceAtTemp(w);
                    if (R > 0.01 && isfinite(R)) {
                        Gtot += 1.0 / R;
                    }
                }
            }
            if (Gtot > 0.0) {
                const double Rpar = 1.0 / Gtot;
                V_branch = Imeas * Rpar;
            }
        }

        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            const uint16_t bit = (1u << w);
            double dt = (ws.lastUpdateMs == 0) ? 0.0 : (ts - ws.lastUpdateMs) * 0.001;

            double P = 0.0;
            if ((currentMask & bit) && isfinite(V_branch) && V_branch > 0.0) {
                double R = wireResistanceAtTemp(w);
                if (R > 0.01 && isfinite(R)) {
                    const double Iw = V_branch / R;
                    P = Iw * Iw * R;
                }
            }

            advanceWireTemp(ws, _ambientC, P, dt);

            WireRuntimeState& rt = runtime.wire(w + 1);
            rt.lastPowerW   = (currentMask & bit) ? P : 0.0;
        }

        // Clamp and publish temps after each current sample.
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            WireRuntimeState& rt = runtime.wire(w + 1);
            applyThermalGuards(ws, rt, ts);
            rt.lastUpdateMs = ts;

            heater.setWireEstimatedTemp(w + 1, ws.T);
            ws.lastUpdateMs = ts;
        }
    }

    runtime.setLastMask(currentMask);
}

void WireThermalModel::coolingOnlyTick(double ambientC,
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
        double dt = (ws.lastUpdateMs == 0) ? 0.0 : (nowTs - ws.lastUpdateMs) * 0.001;
        advanceWireTemp(ws, _ambientC, 0.0, dt);

        WireRuntimeState& rt = runtime.wire(w + 1);
        applyThermalGuards(ws, rt, nowTs);
        rt.lastPowerW   = 0.0;
        rt.lastUpdateMs = nowTs;

        heater.setWireEstimatedTemp(w + 1, ws.T);
        ws.lastUpdateMs = nowTs;
    }

    runtime.setLastMask(currentMask);
}

void WireThermalModel::integrateCapModel(const CpDischg::Sample* voltBuf, size_t nVolt,
                                         const HeaterManager::OutputEvent* outBuf, size_t nOut,
                                         double capF, double vSrc, double rChargeOhm,
                                         double ambientC,
                                         WireStateModel& runtime, HeaterManager& heater) {
    if (!_initialized) {
        init(heater, ambientC);
    }
    _ambientC = ambientC;

    const double C = capF;
    if (!(isfinite(C) && C > 0.0)) {
        // No capacitance known: only apply cooling.
        const uint32_t nowTs = millis();
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            double dt = (ws.lastUpdateMs == 0) ? 0.0 : (nowTs - ws.lastUpdateMs) * 0.001;
            advanceWireTemp(ws, _ambientC, 0.0, dt);
            ws.lastUpdateMs = nowTs;
            heater.setWireEstimatedTemp(w + 1, ws.T);
            WireRuntimeState& rt = runtime.wire(w + 1);
            applyThermalGuards(ws, rt, nowTs);
            rt.lastUpdateMs = nowTs;
        }
        return;
    }

    double rCharge = rChargeOhm;
    if (!isfinite(rCharge) || rCharge <= 0.0) {
        rCharge = INFINITY; // no source / open relay
    }
    double vS = (isfinite(vSrc) && vSrc > 0.0) ? vSrc : 0.0;

    uint16_t currentMask = runtime.getLastMask();

    auto applyCoolingTo = [&](uint32_t ts) {
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            double dt = (ws.lastUpdateMs == 0) ? 0.0 : (ts - ws.lastUpdateMs) * 0.001;
            advanceWireTemp(ws, _ambientC, 0.0, dt);
            ws.lastUpdateMs = ts;
            WireRuntimeState& rt = runtime.wire(w + 1);
            if (!(currentMask & (1u << w))) {
                rt.lastPowerW = 0.0;
            }
        }
    };

    auto equivalentResistance = [&](uint16_t mask) -> double {
        double G = 0.0;
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            if (!(mask & (1u << w))) continue;
            double R = wireResistanceAtTemp(w);
            if (R > 0.01 && isfinite(R)) {
                G += 1.0 / R;
            }
        }
        if (!(G > 0.0)) return INFINITY;
        return 1.0 / G;
    };

    auto applyHeatSegment = [&](uint16_t mask,
                                double v0,
                                double dtS) -> double {
        if (!(mask != 0 && isfinite(v0) && v0 > 0.0 && isfinite(dtS) && dtS > 0.0)) {
            return v0;
        }

        const double Rpar = equivalentResistance(mask);
        if (!isfinite(Rpar) || Rpar <= 0.0) {
            return v0;
        }

        const double Eload = CapModel::energyToLoadJ(v0, dtS, C, Rpar, vS, rCharge);
        const double v1    = CapModel::predictVoltage(v0, dtS, C, Rpar, vS, rCharge);

        // Distribute load energy across parallel branches by conductance fraction.
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            if (!(mask & (1u << w))) continue;

            WireThermalState& ws = _state[w];
            double R = wireResistanceAtTemp(w);
            if (!(R > 0.01 && isfinite(R))) continue;

            const double frac = Rpar / R; // (1/R)/Gtot
            double Ew = Eload * frac;
            if (!isfinite(Ew) || Ew < 0.0) Ew = 0.0;
            double capC = (isfinite(ws.capC) && ws.capC > 0.0) ? ws.capC : _thermalMassC;
            if (capC > 0.0 && isfinite(capC)) {
                ws.T += (Ew / capC);
            }

            WireRuntimeState& rt = runtime.wire(w + 1);
            rt.lastPowerW = (dtS > 0.0) ? (Ew / dtS) : 0.0;
        }

        return v1;
    };

    // Advance last seen bus voltage from voltBuf.
    size_t vIndex = 0;
    auto updateBusVTo = [&](uint32_t ts) {
        if (!voltBuf || nVolt == 0) return;
        while (vIndex < nVolt && voltBuf[vIndex].timestampMs <= ts) {
            double v = voltBuf[vIndex].voltageV;
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
                const double dtS = (ts - _pulseStartMs) * 0.001;
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
        const double dtS = (nowTs - _pulseStartMs) * 0.001;
        const double v0  = isfinite(_pulseStartV) ? _pulseStartV : (isfinite(_lastBusV) ? _lastBusV : vS);
        const double v1  = applyHeatSegment(_pulseMask, v0, dtS);
        _pulseStartMs = nowTs;
        _pulseStartV  = v1;
    }

    // Clamp, publish, and enforce lockouts.
    for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
        WireThermalState& ws = _state[w];
        WireRuntimeState& rt = runtime.wire(w + 1);
        applyThermalGuards(ws, rt, nowTs);
        rt.lastUpdateMs = nowTs;

        heater.setWireEstimatedTemp(w + 1, ws.T);
        ws.lastUpdateMs = nowTs;
    }

    runtime.setLastMask(currentMask);
}

void WireThermalModel::integrate(const CurrentSensor::Sample* curBuf, size_t nCur,
                                 const CpDischg::Sample*     voltBuf, size_t nVolt,
                                 const HeaterManager::OutputEvent* outBuf, size_t nOut,
                                 double ambientC,
                                 WireStateModel& runtime, HeaterManager& heater) {
    if (!_initialized) {
        init(heater, ambientC);
    }
    _ambientC = ambientC;

    uint16_t currentMask = runtime.getLastMask();
    size_t   outIndex    = 0;

    for (size_t i = 0; i < nCur; ++i) {
        const uint32_t ts    = curBuf[i].timestampMs;
        const double   Imeas = curBuf[i].currentA;
        // Find closest voltage sample at/after this timestamp.
        double Vmeas = NAN;
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
            double dt = (ws.lastUpdateMs == 0) ? 0.0 : (ts - ws.lastUpdateMs) * 0.001;

            double P = 0.0;
            if ((currentMask & bit) && isfinite(Vmeas) && Vmeas > 0.0) {
                double R = wireResistanceAtTemp(w);
                if (R > 0.01 && isfinite(R)) {
                    const double Iw = Vmeas / R;
                    P = Iw * Iw * R;
                }
            }

            advanceWireTemp(ws, _ambientC, P, dt);

            WireRuntimeState& rt = runtime.wire(w + 1);
            rt.lastPowerW = (currentMask & bit) ? P : 0.0;
        }

        // Clamp and publish temps.
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            WireRuntimeState& rt = runtime.wire(w + 1);
            applyThermalGuards(ws, rt, ts);
            rt.lastUpdateMs = ts;

            heater.setWireEstimatedTemp(w + 1, ws.T);
            ws.lastUpdateMs = ts;
        }
    }

    runtime.setLastMask(currentMask);
}

double WireThermalModel::getWireTemp(uint8_t index) const {
    if (index == 0 || index > HeaterManager::kWireCount) return NAN;
    return _state[index - 1].T;
}

void WireThermalModel::setThermalParams(double tauSec, double kLoss, double thermalMassC) {
    if (!isfinite(tauSec) || tauSec <= 0.0) {
        tauSec = DEFAULT_WIRE_MODEL_TAU;
    }
    if (!isfinite(kLoss) || kLoss < 0.0) {
        kLoss = DEFAULT_WIRE_MODEL_K;
    }
    if (!isfinite(thermalMassC) || thermalMassC <= 0.0) {
        thermalMassC = DEFAULT_WIRE_MODEL_C;
    }
    _tauSec       = tauSec;
    _heatLossK    = kLoss;
    _thermalMassC = thermalMassC;
}

void WireThermalModel::setWireThermalParams(uint8_t index,
                                            double tauSec,
                                            double kLoss,
                                            double thermalMassC) {
    if (index == 0 || index > HeaterManager::kWireCount) return;

    WireThermalState& ws = _state[index - 1];
    double capFallback = ws.capC;
    if (!isfinite(capFallback) || capFallback <= 0.0) {
        capFallback = _thermalMassC;
    }

    if (!isfinite(kLoss) || kLoss <= 0.0) {
        kLoss = ws.kLoss;
    }
    if (!isfinite(kLoss) || kLoss <= 0.0) {
        kLoss = _heatLossK;
    }
    if (!isfinite(thermalMassC) || thermalMassC <= 0.0) {
        thermalMassC = capFallback;
    }
    if (!isfinite(tauSec) || tauSec <= 0.0) {
        if (isfinite(kLoss) && kLoss > 0.0 &&
            isfinite(thermalMassC) && thermalMassC > 0.0) {
            tauSec = thermalMassC / kLoss;
        } else {
            tauSec = ws.tauSec;
        }
    }
    if (!isfinite(thermalMassC) || thermalMassC <= 0.0) {
        if (isfinite(tauSec) && tauSec > 0.0 &&
            isfinite(kLoss) && kLoss > 0.0) {
            thermalMassC = tauSec * kLoss;
        } else {
            thermalMassC = capFallback;
        }
    }

    ws.tauSec = tauSec;
    ws.kLoss = kLoss;
    ws.capC = thermalMassC;
}

bool WireThermalModel::applyExternalWireTemp(uint8_t index, double tempC, uint32_t tsMs,
                                             WireStateModel& runtime, HeaterManager& heater) {
    if (index == 0 || index > HeaterManager::kWireCount) return false;
    if (!isfinite(tempC)) return false;

    WireThermalState& ws = _state[index - 1];
    const uint32_t ts = (tsMs != 0) ? tsMs : millis();

    ws.T = tempC;
    ws.lastUpdateMs = ts;

    WireRuntimeState& rt = runtime.wire(index);
    applyThermalGuards(ws, rt, ts);
    rt.lastUpdateMs = ts;

    heater.setWireEstimatedTemp(index, ws.T);
    return true;
}

// ======================================================================


// ======================================================================
// WireTelemetryAdapter
// ======================================================================

void WireTelemetryAdapter::fillSnapshot(StatusSnapshot& out,
                                        const WireConfigStore& cfg,
                                        const WireStateModel&  state) const {
    double totalP = 0.0;
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        const WireRuntimeState& rt = state.wire(i + 1);
        const bool allowed = cfg.getAccessFlag(i + 1);
        out.wireTemps[i] = allowed ? rt.tempC : NAN;
        out.outputs[i]   = ((state.getLastMask() & (1u << i)) != 0);
        out.wirePresent[i] = rt.present;
        totalP += rt.lastPowerW;
    }
    (void)totalP; // total power can be added to StatusSnapshot later if desired.
}
