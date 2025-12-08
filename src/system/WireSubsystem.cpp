#include "system/WireSubsystem.h"
#include "system/Config.h"
#include "control/CpDischg.h"
#include <math.h>

// Forward DeviceState definition to avoid circular include here.
#include "system/Device.h"

// Local copies of thermal constants (match Device.h defaults)
static constexpr float NICHROME_CP_J_PER_KG  = 450.0f;
static constexpr float NICHROME_ALPHA        = 0.00017f;
static constexpr float DEFAULT_TAU_SEC       = 1.5f;
static constexpr float WIRE_T_MAX_C          = 150.0f;

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

    if (!isfinite(_wireOhmPerM)  || _wireOhmPerM  <= 0.0f) _wireOhmPerM  = DEFAULT_WIRE_OHM_PER_M;
    if (!isfinite(_targetResOhm) || _targetResOhm <= 0.0f) _targetResOhm = DEFAULT_TARG_RES_OHMS;

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

        const float m = (wi.massKg > 0.0f) ? wi.massKg : 0.0001f;
        ws.C_th = m * NICHROME_CP_J_PER_KG;
        if (!isfinite(ws.C_th) || ws.C_th <= 0.0f) {
            ws.C_th = 0.05f;
        }

        ws.tau              = (DEFAULT_TAU_SEC > 0.05f) ? DEFAULT_TAU_SEC : 0.05f;
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
    const float dT = ws.T - _ambientC;
    float scale = 1.0f + NICHROME_ALPHA * dT;
    if (scale < 0.2f) scale = 0.2f;
    if (scale > 3.0f) scale = 3.0f;
    return ws.R0 * scale;
}

void WireThermalModel::integrate(const CurrentSensor::Sample* curBuf, size_t nCur,
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

        // Apply all mask changes up to this sample timestamp.
        while (outIndex < nOut && outBuf[outIndex].timestampMs <= ts) {
            currentMask = outBuf[outIndex].mask;
            ++outIndex;
        }

        // Cooling step for each wire.
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            if (!ws.lastUpdateMs) ws.lastUpdateMs = ts;
            float dt = (ts - ws.lastUpdateMs) * 0.001f;
            if (dt > 0.0f) {
                const float Tinf   = _ambientC;
                const float factor = expf(-dt / ws.tau);
                ws.T = Tinf + (ws.T - Tinf) * factor;
                ws.lastUpdateMs = ts;
            }
        }

        // Heating if wires are on and net current > 0.
        float I_net = Imeas - idleCurrentA;
        if (I_net < 0.0f) I_net = 0.0f;

        if (currentMask != 0 && I_net > 0.0f) {
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
                const float V = I_net / Gtot;
                for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
                    if (!(currentMask & (1u << w))) continue;

                    WireThermalState& ws = _state[w];
                    float R = wireResistanceAtTemp(w);
                    if (!(R > 0.01f && isfinite(R))) continue;
                    float dt = (ts - ws.lastUpdateMs) * 0.001f;
                    if (dt <= 0.0f) continue;

                    const float P    = (V * V) / R;
                    const float dT_h = (P * dt) / ws.C_th;
                    ws.T += dT_h;

                    WireRuntimeState& rt = runtime.wire(w + 1);
                    rt.lastPowerW   = P;
                    rt.tempC        = ws.T;
                    rt.lastUpdateMs = ts;
                }
            }
        }

        // Clamp and publish temps.
        for (uint8_t w = 0; w < HeaterManager::kWireCount; ++w) {
            WireThermalState& ws = _state[w];
            if (ws.T > WIRE_T_MAX_C) ws.T = WIRE_T_MAX_C;
            if (ws.T < _ambientC - 10.0f) ws.T = _ambientC - 10.0f;

            WireRuntimeState& rt = runtime.wire(w + 1);
            rt.tempC = ws.T;

            heater.setWireEstimatedTemp(w + 1, ws.T);
        }
    }

    runtime.setLastMask(currentMask);
}

float WireThermalModel::getWireTemp(uint8_t index) const {
    if (index == 0 || index > HeaterManager::kWireCount) return NAN;
    return _state[index - 1].T;
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
        float Iexp  = busVoltage / R;
        if (Iexp < 0.01f) Iexp = 0.01f;

        const float leak = _senseLeakCurrent(busVoltage);
        float ImeasNet = Imeas - leak;
        if (ImeasNet < 0.0f) ImeasNet = 0.0f;

        float ratio = ImeasNet / Iexp;

        bool connected = isfinite(Imeas) &&
                         Imeas > 0.01f &&
                         ratio >= minValidFraction &&
                         ratio <= maxValidFraction;

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
    if (mask == 0 || busVoltage <= 0.0f) return;

    float G = 0.0f;
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        if (!(mask & (1u << i))) continue;
        const WireRuntimeState& rt = state.wire(i + 1);
        if (!rt.present) continue;
        if (isfinite(rt.tempC) && rt.tempC >= 150.0f) continue;

        WireInfo wi = heater.getWireInfo(i + 1);
        float R = wi.resistanceOhm;
        if (R > 0.01f && isfinite(R)) {
            G += 1.0f / R;
        }
    }
    if (G <= 0.0f) return;

    float Iexp  = busVoltage * G;
    if (Iexp <= 0.0f) return;

    float netCurrent = totalCurrentA - _senseLeakCurrent(busVoltage);
    if (netCurrent < 0.0f) netCurrent = 0.0f;
    float ratio = netCurrent / Iexp;

    if (!isfinite(ratio) || ratio >= minValidRatio) {
        return; // looks fine
    }

    const uint32_t now = millis();
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        if (!(mask & (1u << i))) continue;
        WireRuntimeState& rt = state.wire(i + 1);
        if (!rt.present) continue;
        if (isfinite(rt.tempC) && rt.tempC >= 150.0f) continue;

        rt.present      = false;
        rt.lastUpdateMs = now;
        heater.setWirePresence(i + 1, false, netCurrent);
    }
}

bool WirePresenceManager::hasAnyConnected(const WireStateModel& state) const {
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

    float    bestScore = INFINITY;
    uint16_t bestMask  = 0;

    for (uint16_t m = 1; m < FULL; ++m) {
        float Req = equivalentResistance(cfg, state, m);
        if (!isfinite(Req) || Req <= 0.0f) continue;

        float score = fabsf(Req - targetResOhm);
        if (score < bestScore) {
            bestScore = score;
            bestMask  = m;
        }
    }
    return bestMask;
}

float WirePlanner::equivalentResistance(const WireConfigStore& cfg,
                                        const WireStateModel&  state,
                                        uint16_t mask) const {
    float G = 0.0f;
    for (uint8_t i = 0; i < HeaterManager::kWireCount; ++i) {
        if (!(mask & (1u << i))) continue;

        const WireRuntimeState& rt = state.wire(i + 1);
        if (!rt.present || rt.locked || !rt.allowedByAccess) {
            return INFINITY;
        }

        float R = cfg.getWireResistance(i + 1);
        if (R > 0.01f && isfinite(R)) {
            G += 1.0f / R;
        }
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

        if (!access || !rt.present || rt.overTemp || rt.locked) {
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
