/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef POWER_TRACKER_H
#define POWER_TRACKER_H

#include <stdint.h>
#include "system/Utils.h"
#include "sensing/CurrentSensor.h"
#include "services/NVSManager.h"

// ----------------------------------------------------------------------------
// Persistent history configuration
// ----------------------------------------------------------------------------

#define POWERTRACKER_HISTORY_MAX   800
#define POWERTRACKER_HISTORY_FILE  "/History.json"

class PowerTracker {
public:
    struct SessionStats {
        bool      valid          = false;
        float     energy_Wh      = 0.0f;
        uint32_t  duration_s     = 0;      // rounded seconds
        float     peakPower_W    = 0.0f;
        float     peakCurrent_A  = 0.0f;
    };

    struct HistoryEntry {
        bool        valid   = false;
        uint32_t    startMs = 0;   // millis() when session started
        SessionStats stats;        // final stats snapshot
    };

    // Singleton-style access
    static PowerTracker* Get() {
        static PowerTracker instance;
        return &instance;
    }

    // Load persisted totals & last session stats from NVS.
    // Also loads history from /History.json (SPIFFS) if present.
    void begin();

    // Start a new heating session.
    void startSession(float nominalBusV, float idleCurrentA);

    // Update integration from CurrentSensor history.
    void update(CurrentSensor& cs);

    // End the current session and persist KPIs + history.
    //  success = true  => normal finish
    //          = false => aborted/fault (still logged in history)
    void endSession(bool success);

    bool isSessionActive() const { return _active; }

    // Lifetime totals
    float    getTotalEnergy_Wh()  const { return _totalEnergy_Wh; }
    uint32_t getTotalSessions()   const { return _totalSessions; }
    uint32_t getTotalSuccessful() const { return _totalSessionsOk; }

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

    // ------------------------------------------------------------------------
    // History API (used by WiFiManager /session_history, etc.)
    // indexFromNewest: 0 = most recent, 1 = previous, ...
    // ------------------------------------------------------------------------

    uint16_t getHistoryCount() const { return _historyCount; }

    bool getHistoryEntry(uint16_t indexFromNewest, HistoryEntry& out) const;

    // Optional: clear all history + delete file
    void clearHistory();

private:
    PowerTracker() = default;

    // NVS helpers
    void loadFromNVS();
    void saveTotalsToNVS() const;
    void saveLastSessionToNVS() const;

    // History helpers (SPIFFS)
    void loadHistoryFromFile();
    bool saveHistoryToFile() const;
    void appendHistoryEntry(const HistoryEntry& e);

    // Session state
    bool      _active            = false;
    uint32_t  _startMs           = 0;
    uint32_t  _lastSampleTsMs    = 0;
    uint32_t  _lastHistorySeq    = 0;
    uint32_t  _lastBusSeq        = 0;

    float     _nominalBusV       = 0.0f;
    float     _idleCurrentA      = 0.0f;

    float     _sessionEnergy_Wh  = 0.0f;
    float     _sessionPeakPower_W= 0.0f;
    float     _sessionPeakCurrent_A = 0.0f;

    // Persisted totals
    float     _totalEnergy_Wh    = 0.0f;
    uint32_t  _totalSessions     = 0;
    uint32_t  _totalSessionsOk   = 0;

    // Circular buffer for last POWERTRACKER_HISTORY_MAX sessions
    HistoryEntry _history[POWERTRACKER_HISTORY_MAX];
    uint16_t     _historyHead  = 0;   // next write index
    uint16_t     _historyCount = 0;   // number of valid entries

    // Last session snapshot (for quick access)
    SessionStats _lastSession;
};

#define POWER_TRACKER PowerTracker::Get()

#endif // POWER_TRACKER_H


