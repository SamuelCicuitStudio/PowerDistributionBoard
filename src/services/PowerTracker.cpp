#include "services/PowerTracker.h"
#include "services/NVSManager.h"

#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// -----------------------------------------------------------------------------
// NVS helpers
// -----------------------------------------------------------------------------

void PowerTracker::loadFromNVS() {
    if (!CONF) return;

    _totalEnergy_Wh      = CONF->GetFloat(PT_KEY_TOTAL_ENERGY_WH, 0.0f);
    _totalSessions       = (uint32_t)CONF->GetInt(PT_KEY_TOTAL_SESSIONS, 0);
    _totalSessionsOk     = (uint32_t)CONF->GetInt(PT_KEY_TOTAL_SESSIONS_OK, 0);

    _lastSession.energy_Wh     = CONF->GetFloat(PT_KEY_LAST_SESS_ENERGY_WH, 0.0f);
    _lastSession.duration_s    = (uint32_t)CONF->GetInt(PT_KEY_LAST_SESS_DURATION_S, 0);
    _lastSession.peakPower_W   = CONF->GetFloat(PT_KEY_LAST_SESS_PEAK_W, 0.0f);
    _lastSession.peakCurrent_A = CONF->GetFloat(PT_KEY_LAST_SESS_PEAK_A, 0.0f);
    _lastSession.valid         = (_lastSession.duration_s > 0 || _lastSession.energy_Wh > 0.0f);
}

void PowerTracker::saveTotalsToNVS() const {
    if (!CONF) return;

    CONF->PutFloat(PT_KEY_TOTAL_ENERGY_WH, _totalEnergy_Wh);
    CONF->PutInt  (PT_KEY_TOTAL_SESSIONS,  (int)_totalSessions);
    CONF->PutInt  (PT_KEY_TOTAL_SESSIONS_OK,(int)_totalSessionsOk);
}

void PowerTracker::saveLastSessionToNVS() const {
    if (!CONF) return;
    if (!_lastSession.valid) return;

    CONF->PutFloat(PT_KEY_LAST_SESS_ENERGY_WH, _lastSession.energy_Wh);
    CONF->PutInt  (PT_KEY_LAST_SESS_DURATION_S,(int)_lastSession.duration_s);
    CONF->PutFloat(PT_KEY_LAST_SESS_PEAK_W,    _lastSession.peakPower_W);
    CONF->PutFloat(PT_KEY_LAST_SESS_PEAK_A,    _lastSession.peakCurrent_A);
}

// -----------------------------------------------------------------------------
// History helpers (SPIFFS-backed ring buffer)
// -----------------------------------------------------------------------------

void PowerTracker::appendHistoryEntry(const HistoryEntry& e) {
    if (!e.valid) return;

    _history[_historyHead] = e;
    _history[_historyHead].valid = true;

    _historyHead = (_historyHead + 1) % POWERTRACKER_HISTORY_MAX;

    if (_historyCount < POWERTRACKER_HISTORY_MAX) {
        _historyCount++;
    }
    // If full, ring overwrite: oldest entry is implicitly dropped.
}

bool PowerTracker::saveHistoryToFile() const {
    // We assume SPIFFS.begin(...) was called in setup().
    if (!SPIFFS.begin(false)) {
        DEBUG_PRINTLN("[PowerTracker] SPIFFS not mounted; cannot save history.");
        return false;
    }

    const char* tmpPath = "/History.tmp";
    File f = SPIFFS.open(tmpPath, "w");
    if (!f) {
        DEBUG_PRINTLN("[PowerTracker] Failed to open temp history file for write.");
        return false;
    }

    // 500 entries x small objects -> 32KB is plenty.
    DynamicJsonDocument doc(32768);
    JsonArray arr = doc.createNestedArray("history");

    if (_historyCount > 0) {
        const uint16_t count = _historyCount;
        // Oldest entry index
        uint16_t idx = (_historyHead + POWERTRACKER_HISTORY_MAX - count) % POWERTRACKER_HISTORY_MAX;

        for (uint16_t i = 0; i < count; ++i) {
            const HistoryEntry& h = _history[idx];
            if (h.valid) {
                JsonObject row = arr.createNestedObject();
                row["start_ms"]      = h.startMs;
                row["duration_s"]    = h.stats.duration_s;
                row["energy_Wh"]     = h.stats.energy_Wh;
                row["peakPower_W"]   = h.stats.peakPower_W;
                row["peakCurrent_A"] = h.stats.peakCurrent_A;
            }
            idx = (idx + 1) % POWERTRACKER_HISTORY_MAX;
        }
    }

    if (serializeJson(doc, f) == 0) {
        DEBUG_PRINTLN("[PowerTracker] Failed to serialize history JSON.");
        f.close();
        SPIFFS.remove(tmpPath);
        return false;
    }

    f.close();

    SPIFFS.remove(POWERTRACKER_HISTORY_FILE);
    if (!SPIFFS.rename(tmpPath, POWERTRACKER_HISTORY_FILE)) {
        DEBUG_PRINTLN("[PowerTracker] Failed to rename history temp file.");
        return false;
    }

    DEBUG_PRINTF("[PowerTracker] History saved (%u entries).\n", (unsigned)_historyCount);
    return true;
}

void PowerTracker::loadHistoryFromFile() {
    _historyHead = 0;
    _historyCount = 0;

    if (!SPIFFS.begin(false)) {
        DEBUG_PRINTLN("[PowerTracker] SPIFFS not mounted; no history loaded.");
        return;
    }

    if (!SPIFFS.exists(POWERTRACKER_HISTORY_FILE)) {
        DEBUG_PRINTLN("[PowerTracker] No existing /History.json, starting empty.");
        return;
    }

    File f = SPIFFS.open(POWERTRACKER_HISTORY_FILE, "r");
    if (!f) {
        DEBUG_PRINTLN("[PowerTracker] Failed to open /History.json.");
        return;
    }

    DynamicJsonDocument doc(32768);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        DEBUG_PRINT("[PowerTracker] Failed to parse /History.json: ");
        DEBUG_PRINTLN(err.c_str());
        return;
    }

    JsonArray arr = doc["history"].as<JsonArray>();
    if (arr.isNull()) {
        DEBUG_PRINTLN("[PowerTracker] /History.json missing 'history' array.");
        return;
    }

    for (JsonObject obj : arr) {
        if (_historyCount >= POWERTRACKER_HISTORY_MAX) break;

        HistoryEntry e;
        e.valid = true;

        // Accept both snakeCase and camelCase flavors if they ever appear.
        e.startMs              = obj["start_ms"]      | obj["startMs"]      | 0;
        e.stats.duration_s     = obj["duration_s"]    | obj["durationS"]    | 0;
        e.stats.energy_Wh      = obj["energy_Wh"]     | obj["energyWh"]     | 0.0f;
        e.stats.peakPower_W    = obj["peakPower_W"]   | obj["peakPowerW"]   | 0.0f;
        e.stats.peakCurrent_A  = obj["peakCurrent_A"] | obj["peakCurrentA"] | 0.0f;

        appendHistoryEntry(e);
    }

    DEBUG_PRINTF("[PowerTracker] Loaded %u history entries from SPIFFS.\n",
                 (unsigned)_historyCount);
}

bool PowerTracker::getHistoryEntry(uint16_t indexFromNewest, HistoryEntry& out) const {
    if (indexFromNewest >= _historyCount) return false;

    // head points to next write; newest entry is at head-1.
    uint16_t idx = (_historyHead + POWERTRACKER_HISTORY_MAX - 1 - indexFromNewest)
                   % POWERTRACKER_HISTORY_MAX;

    if (!_history[idx].valid) return false;
    out = _history[idx];
    return true;
}

void PowerTracker::clearHistory() {
    for (uint16_t i = 0; i < POWERTRACKER_HISTORY_MAX; ++i) {
        _history[i].valid = false;
    }
    _historyHead  = 0;
    _historyCount = 0;

    if (SPIFFS.begin(false)) {
        SPIFFS.remove(POWERTRACKER_HISTORY_FILE);
    }

    DEBUG_PRINTLN("[PowerTracker] History cleared.");
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void PowerTracker::begin() {
    loadFromNVS();
    loadHistoryFromFile();
    _active = false;
}

void PowerTracker::startSession(float nominalBusV, float idleCurrentA) {
    if (_active) {
        // Close previous session defensively as failed.
        endSession(false);
    }

    _active               = true;
    _startMs              = millis();
    _lastSampleTsMs       = 0;
    _lastHistorySeq       = 0;

    _nominalBusV          = (nominalBusV > 0.0f) ? nominalBusV : 0.0f;
    _idleCurrentA         = (idleCurrentA >= 0.0f) ? idleCurrentA : 0.0f;

    _sessionEnergy_Wh     = 0.0f;
    _sessionPeakPower_W   = 0.0f;
    _sessionPeakCurrent_A = 0.0f;

    DEBUG_PRINTLN("[PowerTracker] Session started");
}

void PowerTracker::update(CurrentSensor& cs) {
    if (!_active) return;

    // If continuous sampling isn't running, approximate using last current.
    if (!cs.isContinuousRunning()) {
        uint32_t now = millis();
        float I = fabsf(cs.getLastCurrent());
        // Ignore samples taken before the session start timestamp.
        if (now < _startMs) {
            _lastSampleTsMs = 0;
            return;
        }

        if (_lastSampleTsMs == 0 || _lastSampleTsMs < _startMs) {
            _lastSampleTsMs = now;
            return;
        }

        float dt_s = (now - _lastSampleTsMs) * 0.001f;
        _lastSampleTsMs = now;
        if (dt_s <= 0.0f) return;

        float netI = I - _idleCurrentA;
        if (netI < 0.0f) netI = 0.0f;

        if (_nominalBusV > 0.0f && netI > 0.0f) {
            float P      = _nominalBusV * netI;
            float dE_Wh  = (P * dt_s) / 3600.0f;
            _sessionEnergy_Wh += dE_Wh;
            if (I > _sessionPeakCurrent_A) _sessionPeakCurrent_A = I;
            if (P > _sessionPeakPower_W)   _sessionPeakPower_W   = P;
        }
        return;
    }

    // Normal path: integrate from CurrentSensor rolling history.
    CurrentSensor::Sample buf[64];
    uint32_t newSeq = _lastHistorySeq;
    size_t n = cs.getHistorySince(_lastHistorySeq, buf, (size_t)64, newSeq);

    if (n == 0) {
        return;
    }

    for (size_t i = 0; i < n; ++i) {
        const uint32_t ts = buf[i].timestampMs;
        float I = fabsf(buf[i].currentA);

        // Skip any samples that occurred before this session started.
        if (ts < _startMs) {
            // Reset baseline so we don't integrate across the session boundary.
            _lastSampleTsMs = 0;
            continue;
        }

        if (_lastSampleTsMs == 0 || _lastSampleTsMs < _startMs) {
            _lastSampleTsMs = ts;
            if (I > _sessionPeakCurrent_A) _sessionPeakCurrent_A = I;
            continue;
        }

        float dt_s = (ts - _lastSampleTsMs) * 0.001f;
        if (dt_s <= 0.0f) {
            if (I > _sessionPeakCurrent_A) _sessionPeakCurrent_A = I;
            continue;
        }

        _lastSampleTsMs = ts;

        float netI = I - _idleCurrentA;
        if (netI < 0.0f) netI = 0.0f;

        if (_nominalBusV > 0.0f && netI > 0.0f) {
            const float P     = _nominalBusV * netI;
            const float dE_Wh = (P * dt_s) / 3600.0f;

            _sessionEnergy_Wh += dE_Wh;

            if (I > _sessionPeakCurrent_A) _sessionPeakCurrent_A = I;
            if (P > _sessionPeakPower_W)   _sessionPeakPower_W   = P;
        }
    }

    _lastHistorySeq = newSeq;
}

void PowerTracker::endSession(bool success) {
    if (!_active) return;

    _active = false;

    uint32_t now   = millis();
    uint32_t durMs = (now >= _startMs)
                     ? (now - _startMs)
                     : (UINT32_MAX - _startMs + now + 1);

    SessionStats s;
    s.valid          = true;
    s.energy_Wh      = _sessionEnergy_Wh;
    s.duration_s     = durMs / 1000U;
    s.peakPower_W    = _sessionPeakPower_W;
    s.peakCurrent_A  = _sessionPeakCurrent_A;

    // Update totals
    _totalSessions++;
    if (success) {
        _totalSessionsOk++;
    }
    _totalEnergy_Wh += s.energy_Wh;

    _lastSession = s;

    saveTotalsToNVS();
    saveLastSessionToNVS();

    // Append to in-memory history + persist to SPIFFS
    HistoryEntry he;
    he.valid   = true;
    he.startMs = _startMs;
    he.stats   = s;
    appendHistoryEntry(he);
    saveHistoryToFile();

    DEBUG_PRINTF(
        "[PowerTracker] Session end (%s): E=%.4f Wh, dur=%lus, Ppk=%.2f W, Ipk=%.2f A\n",
        success ? "OK" : "ABORT",
        (double)s.energy_Wh,
        (unsigned long)s.duration_s,
        (double)s.peakPower_W,
        (double)s.peakCurrent_A
    );
}
