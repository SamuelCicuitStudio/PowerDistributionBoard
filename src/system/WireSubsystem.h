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

// ======================================================================
// WireRuntimeState: per-wire runtime fields
// ======================================================================

struct WireRuntimeState {
    bool      present         = true;   // physical presence, as seen by presence manager
    bool      overTemp        = false;  // latched over-temperature
    bool      locked          = false;  // locked out by thermal/safety policy
    bool      allowedByAccess = true;   // from config access flags

    float     tempC           = NAN;    // latest virtual temperature
    float     lastPowerW      = 0.0f;   // last computed power
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
    void init(const HeaterManager& heater, float ambientC);

    void integrate(const CurrentSensor::Sample* curBuf, size_t nCur,
                   const CpDischg::Sample*     voltBuf, size_t nVolt,
                   const HeaterManager::OutputEvent* outBuf, size_t nOut,
                   float idleCurrentA, float ambientC,
                   WireStateModel& runtime, HeaterManager& heater);

    // Variant that uses only current history (no voltage) to estimate
    // per-wire power and temperature rise.
    void integrateCurrentOnly(const CurrentSensor::Sample* curBuf, size_t nCur,
                              const HeaterManager::OutputEvent* outBuf, size_t nOut,
                              float ambientC,
                              WireStateModel& runtime, HeaterManager& heater);

    float getWireTemp(uint8_t index) const;
    void  setCoolingScale(float scale);
    void  setCoolingParams(float kCold, float maxDropC, float scale);

private:
    struct WireThermalState {
        float    R0              = 1.0f;
        float    C_th            = 0.05f;
        float    tau             = 0.5f;
        float    T               = 25.0f;
        uint32_t lastUpdateMs    = 0;
        bool     locked          = false;
        uint32_t cooldownReleaseMs = 0;
    };

    float wireResistanceAtTemp(uint8_t idx) const;

    WireThermalState _state[HeaterManager::kWireCount];
    float            _ambientC      = 25.0f;
    bool             _initialized   = false;
    float            _coolingScale  = 1.0f;
    float            _coolKCold     = DEFAULT_COOL_K_COLD;
    float            _coolKHot      = 0.04f; // default hot-side cooling gain (slower cooling)
    float            _maxCoolDropC  = DEFAULT_MAX_COOL_DROP_C;
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
