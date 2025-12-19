/**************************************************************
 * WireSubsystem.h
 *
 * Modular wire-control helpers:
 *  - Configuration storage (NVS-backed)
 *  - Runtime wire state
 *  - Thermal integration (virtual temperatures)
 *  - Presence detection
 *  - Planner (target resistance)
 *  - Safety policy
 *  - Actuator (mask -> HeaterManager)
 *  - Telemetry adapter (StatusSnapshot / JSON)
 *
 * NOTE: This header is designed to be integrated gradually.
 *       It does not change existing behavior until you call it
 *       from Device / HeaterManager / DeviceTransport.
 **************************************************************/
#ifndef WIRE_SUBSYSTEM_H
#define WIRE_SUBSYSTEM_H

#include <Arduino.h>
#include "control/HeaterManager.h"
#include "sensing/CurrentSensor.h"
#include "control/CpDischg.h"
#include "system/StatusSnapshot.h"
#include "system/Config.h"
#include <math.h>

// ======================================================================
// CapModel – simple R-C prediction helpers
// ======================================================================
//
// Models the bus as:
//   - A capacitor C [F] at the load node
//   - A charge path from a source Vsrc through Rcharge [Ω] (optional)
//   - A resistive load Rload [Ω] (optional)
//
// dV/dt = (Vsrc - V)/ (Rcharge*C) - V/(Rload*C)
//
// Notes:
//  - Pass Rcharge as INFINITY (or <=0) to model "relay open" (no source).
//  - Pass Rload   as INFINITY (or <=0) to model "no load" (pure recharge).
// ======================================================================
namespace CapModel {

static inline double _safeResOhm(double r) {
    if (!isfinite(r) || r <= 0.0) return INFINITY;
    return r;
}

static inline double predictVoltage(double v0,
                                    double dtS,
                                    double capF,
                                    double rLoadOhm,
                                    double vSrc,
                                    double rChargeOhm)
{
    if (!isfinite(v0)) v0 = 0.0;
    if (!isfinite(dtS) || dtS <= 0.0) return v0;
    if (!isfinite(capF) || capF <= 0.0) return v0;

    const double rL = _safeResOhm(rLoadOhm);
    const double rC = _safeResOhm(rChargeOhm);
    double vS = (isfinite(vSrc) && vSrc > 0.0) ? vSrc : 0.0;

    // No source + no load -> hold.
    if (isinf(rC) && isinf(rL)) {
        return v0;
    }

    // No source -> pure discharge: V(t)=V0*exp(-t/(Rload*C))
    if (isinf(rC)) {
        if (isinf(rL)) return v0;
        const double tau = rL * capF;
        if (!isfinite(tau) || tau <= 0.0) return v0;
        return v0 * exp(-dtS / tau);
    }

    // No load -> pure charge: V(t)=Vsrc + (V0-Vsrc)*exp(-t/(Rcharge*C))
    if (isinf(rL)) {
        const double tau = rC * capF;
        if (!isfinite(tau) || tau <= 0.0) return v0;
        return vS + (v0 - vS) * exp(-dtS / tau);
    }

    // Source + load -> first-order to V_inf with tau = (Rcharge||Rload)*C
    const double rSum = rC + rL;
    if (!isfinite(rSum) || rSum <= 0.0) return v0;

    const double rEff = (rC * rL) / rSum;
    const double tau  = rEff * capF;
    if (!isfinite(tau) || tau <= 0.0) return v0;

    const double vInf = vS * (rL / rSum);
    return vInf + (v0 - vInf) * exp(-dtS / tau);
}

// Energy delivered to the load resistor over dt (Joules).
static inline double energyToLoadJ(double v0,
                                   double dtS,
                                   double capF,
                                   double rLoadOhm,
                                   double vSrc,
                                   double rChargeOhm)
{
    if (!isfinite(v0)) v0 = 0.0;
    if (!isfinite(dtS) || dtS <= 0.0) return 0.0;
    if (!isfinite(capF) || capF <= 0.0) return 0.0;

    const double rL = _safeResOhm(rLoadOhm);
    const double rC = _safeResOhm(rChargeOhm);
    double vS = (isfinite(vSrc) && vSrc > 0.0) ? vSrc : 0.0;

    if (isinf(rL)) {
        return 0.0f; // no load -> no load energy
    }

    // No source: use capacitor energy drop directly (stable numerically).
    if (isinf(rC)) {
        const double v1 = predictVoltage(v0, dtS, capF, rL, 0.0, INFINITY);
        return 0.5 * capF * (v0 * v0 - v1 * v1);
    }

    const double rSum = rC + rL;
    if (!isfinite(rSum) || rSum <= 0.0) return 0.0;

    const double rEff = (rC * rL) / rSum;
    const double tau  = rEff * capF;
    if (!isfinite(tau) || tau <= 0.0) return 0.0;

    const double vInf = vS * (rL / rSum);
    const double A    = v0 - vInf;

    const double e1 = exp(-dtS / tau);
    const double e2 = exp(-2.0 * dtS / tau);

    const double term = vInf * vInf * dtS
                      + 2.0 * vInf * A * tau * (1.0 - e1)
                      + (A * A) * (tau * 0.5) * (1.0 - e2);

    return term / rL;
}

} // namespace CapModel

// ======================================================================
// WireRuntimeState: per-wire runtime fields
// ======================================================================

struct WireRuntimeState {
    bool      present         = true;   // physical presence, as seen by presence manager
    bool      overTemp        = false;  // latched over-temperature
    bool      locked          = false;  // locked out by thermal/safety policy
    bool      allowedByAccess = true;   // from config access flags

    double    tempC           = NAN;    // latest virtual temperature
    double    lastPowerW      = 0.0;    // last computed power
    uint32_t  lastUpdateMs    = 0;      // last time temp/power were updated
    float     usageScore      = 0.0f;   // recent ON usage for fairness rotation
};

// ======================================================================
// WireConfigStore – NVS-backed configuration
// ======================================================================

class WireConfigStore {
public:
    void loadFromNvs();
    void saveToNvs() const;

    float getWireResistance(uint8_t index) const;
    void  setWireResistance(uint8_t index, float ohms);

    bool  getAccessFlag(uint8_t index) const;
    void  setAccessFlag(uint8_t index, bool allowed);

    float getTargetResOhm() const;
    void  setTargetResOhm(float ohms);

    float getWireOhmPerM() const;
    void  setWireOhmPerM(float v);

    int   getWireGaugeAwg() const;
    void  setWireGaugeAwg(int awg);

private:
    float _wireR[HeaterManager::kWireCount]  = { DEFAULT_WIRE_RES_OHMS };
    bool  _access[HeaterManager::kWireCount] = { false };
    float _wireOhmPerM  = DEFAULT_WIRE_OHM_PER_M;
    float _targetResOhm = DEFAULT_TARG_RES_OHMS;
    int   _wireGaugeAwg = DEFAULT_WIRE_GAUGE;
};

// ======================================================================
// WireStateModel – runtime state only (no hardware, no NVS)
// ======================================================================

class WireStateModel {
public:
    WireRuntimeState&       wire(uint8_t index);
    const WireRuntimeState& wire(uint8_t index) const;

    uint16_t getLastMask() const;
    void     setLastMask(uint16_t m);

private:
    WireRuntimeState _wire[HeaterManager::kWireCount];
    uint16_t         _lastMask = 0;
};

// ======================================================================
// WireThermalModel – virtual temperature integration
// ======================================================================

class WireThermalModel {
public:
    void init(const HeaterManager& heater, double ambientC);

    void integrate(const CurrentSensor::Sample* curBuf, size_t nCur,
                   const CpDischg::Sample*     voltBuf, size_t nVolt,
                   const HeaterManager::OutputEvent* outBuf, size_t nOut,
                   double idleCurrentA, double ambientC,
                   WireStateModel& runtime, HeaterManager& heater);

    // Variant that uses only current history (no voltage) to estimate
    // per-wire power and temperature rise.
    void integrateCurrentOnly(const CurrentSensor::Sample* curBuf, size_t nCur,
                              const HeaterManager::OutputEvent* outBuf, size_t nOut,
                              double ambientC,
                              WireStateModel& runtime, HeaterManager& heater);

    // Variant that estimates heating from a capacitor + recharge resistor model.
    // Uses output-mask history and bus voltage snapshots (no per-sample current needed).
    void integrateCapModel(const CpDischg::Sample* voltBuf, size_t nVolt,
                           const HeaterManager::OutputEvent* outBuf, size_t nOut,
                           double capF, double vSrc, double rChargeOhm,
                           double ambientC,
                           WireStateModel& runtime, HeaterManager& heater);

    // Cooling-only integration (no new history). Keeps temps decaying and
    // lockout timers advancing even when current/voltage samples are missing.
    void coolingOnlyTick(double ambientC,
                         WireStateModel& runtime,
                         HeaterManager& heater);

    double getWireTemp(uint8_t index) const;
    void   setThermalParams(double tauSec, double kLoss, double thermalMassC);
    bool   applyExternalWireTemp(uint8_t index, double tempC, uint32_t tsMs,
                                WireStateModel& runtime, HeaterManager& heater);

private:
    struct WireThermalState {
        double   R0              = 1.0;
        double   T               = 25.0;
        uint32_t lastUpdateMs    = 0;
        bool     locked          = false;
        uint32_t cooldownReleaseMs = 0;
    };

    double wireResistanceAtTemp(uint8_t idx) const;
    void   advanceWireTemp(WireThermalState& ws, double ambientC, double powerW, double dtS);

    WireThermalState _state[HeaterManager::kWireCount];
    double           _ambientC      = 25.0;
    bool             _initialized   = false;
    double           _tauSec        = DEFAULT_WIRE_TAU_SEC;
    double           _heatLossK     = DEFAULT_WIRE_K_LOSS;
    double           _thermalMassC  = DEFAULT_WIRE_THERMAL_C;

    // Pulse state for integrateCapModel()
    bool     _pulseActive   = false;
    uint16_t _pulseMask     = 0;
    uint32_t _pulseStartMs  = 0;
    double   _pulseStartV   = NAN;
    double   _lastBusV      = NAN;
};

// ======================================================================
// WirePresenceManager – presence detection
// ======================================================================

class WirePresenceManager {
public:
    void probeAll(HeaterManager& heater,
                  WireStateModel& state,
                  CurrentSensor& cs,
                  float busVoltage,
                  float minValidFraction = 0.25f,
                  float maxValidFraction = 4.0f,
                  uint16_t settleMs = 30,
                  uint8_t samples = 10);

    void updatePresenceFromMask(HeaterManager& heater,
                                WireStateModel& state,
                                uint16_t mask,
                                float totalCurrentA,
                                float busVoltage,
                                float minValidRatio = 0.20f);

    bool hasAnyConnected(const WireStateModel& state) const;
};

// ======================================================================
// WirePlanner – target resistance planner
// ======================================================================

class WirePlanner {
public:
    uint16_t chooseMask(const WireConfigStore& cfg,
                        const WireStateModel&  state,
                        float targetResOhm) const;

private:
    // Remember last chosen mask to enable round‑robin across calls,
    // independent of the hardware's current output mask.
    mutable uint16_t _lastChosenMask = 0;

    float equivalentResistance(const WireConfigStore& cfg,
                               const WireStateModel&  state,
                               uint16_t mask) const;
};

// ======================================================================
// WireSafetyPolicy – safety gating
// ======================================================================

class WireSafetyPolicy {
public:
    uint16_t filterMask(uint16_t requestedMask,
                        const WireConfigStore& cfg,
                        const WireStateModel&  state,
                        DeviceState            devState) const;
};

// ======================================================================
// WireActuator – requested → safe → HeaterManager
// ======================================================================

class WireActuator {
public:
    uint16_t applyRequestedMask(uint16_t requestedMask,
                                const WireConfigStore& cfg,
                                WireStateModel&        state,
                                DeviceState            devState);
};

// ======================================================================
// WireTelemetryAdapter – wire → StatusSnapshot / JSON
// ======================================================================

class WireTelemetryAdapter {
public:
    void fillSnapshot(StatusSnapshot& out,
                      const WireConfigStore& cfg,
                      const WireStateModel&  state) const;

    void writeMonitorJson(JsonObject& root,
                          const StatusSnapshot& snap) const;
};

#endif // WIRE_SUBSYSTEM_H
