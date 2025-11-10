#ifndef POWER_TRACKER_H
#define POWER_TRACKER_H

#include <stdint.h>
#include "Utils.h"
#include "CurrentSensor.h"
#include "NVSManager.h"

// ----------------------------------------------------------------------------
// PowerTracker
// ----------------------------------------------------------------------------
// - Integrates energy from CurrentSensor 10s history.
// - Uses (Imeas - idleCurrentA)+ as net heater current.
// - Uses a nominal / supplied bus voltage for power estimation.
// - Call begin() once at boot.
// - Call startSession() when heating loop starts.
// - Call update() periodically during RUN (any mode).
// - Call endSession() when StartLoop() exits.
// ----------------------------------------------------------------------------

class PowerTracker {
public:
    struct SessionStats {
        bool      valid          = false;
        float     energy_Wh      = 0.0f;
        uint32_t  duration_s     = 0;      // rounded seconds
        float     peakPower_W    = 0.0f;
        float     peakCurrent_A  = 0.0f;
    };

    // Singleton-style access (optional but convenient)
    static PowerTracker* Get() {
        static PowerTracker instance;
        return &instance;
    }

    // Load persisted totals & last session stats from NVS.
    void begin();

    // Start a new heating session.
    //
    //  - nominalBusV: estimated DC bus / heater voltage (V)
    //  - idleCurrentA: baseline current to subtract (AC, relay, etc.)
    //
    // You typically call this right after transitioning to DeviceState::Running
    // and after calibrateIdleCurrent().
    void startSession(float nominalBusV, float idleCurrentA);

    // Update integration from CurrentSensor 10s history.
    //
    // Call this regularly while RUNNING (e.g. inside the main loop in both
    // SEQUENTIAL and ADVANCED modes). As long as it's called at least once
    // every few seconds, no samples are lost.
    void update(CurrentSensor& cs);

    // End the current session.
    //
    //  - success: true if loop finished normally; false if aborted/fault.
    // Flushes pending samples, finalizes SessionStats, and persists:
    //   - total energy
    //   - session counters
    //   - last-session KPIs
    void endSession(bool success);

    bool isSessionActive() const { return _active; }

    // --- Exposed stats for web / diagnostics ---

    // Lifetime totals (since device install / last reset)
    float    getTotalEnergy_Wh()      const { return _totalEnergy_Wh; }
    uint32_t getTotalSessions()       const { return _totalSessions; }
    uint32_t getTotalSuccessful()     const { return _totalSessionsOk; }

    // Last completed session
    const SessionStats& getLastSession() const { return _lastSession; }

    // Current in-progress session snapshot (non-persisted)
    SessionStats getCurrentSessionSnapshot() const {
        SessionStats s;
        if (_active) {
            uint32_t now = millis();
            s.valid         = true;
            s.energy_Wh     = _sessionEnergy_Wh;
            s.duration_s    = (now - _startMs) / 1000U;
            s.peakPower_W   = _sessionPeakPower_W;
            s.peakCurrent_A = _sessionPeakCurrent_A;
        }
        return s;
    }

private:
    PowerTracker() = default;

    // Internal helpers
    void loadFromNVS();
    void saveTotalsToNVS() const;
    void saveLastSessionToNVS() const;

    // Session state
    bool      _active            = false;
    uint32_t  _startMs           = 0;
    uint32_t  _lastSampleTsMs    = 0;
    uint32_t  _lastHistorySeq    = 0;

    float     _nominalBusV       = 0.0f;
    float     _idleCurrentA      = 0.0f;

    float     _sessionEnergy_Wh  = 0.0f;
    float     _sessionPeakPower_W= 0.0f;
    float     _sessionPeakCurrent_A = 0.0f;

    // Persisted totals
    float     _totalEnergy_Wh    = 0.0f;
    uint32_t  _totalSessions     = 0;
    uint32_t  _totalSessionsOk   = 0;

    // Last session snapshot
    SessionStats _lastSession;
};

#define POWER_TRACKER PowerTracker::Get()

#endif // POWER_TRACKER_H
