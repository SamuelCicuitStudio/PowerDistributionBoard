#include "PowerTracker.h"
#include "NVSManager.h"   // same one used behind CONF

// -----------------------------------------------------------------------------
// NVS helpers
// -----------------------------------------------------------------------------

void PowerTracker::loadFromNVS() {
    if (!CONF) return;

    _totalEnergy_Wh          = CONF->GetFloat(PT_KEY_TOTAL_ENERGY_WH, 0.0f);
    _totalSessions           = (uint32_t)CONF->GetInt(PT_KEY_TOTAL_SESSIONS, 0);
    _totalSessionsOk         = (uint32_t)CONF->GetInt(PT_KEY_TOTAL_SESSIONS_OK, 0);

    _lastSession.energy_Wh   = CONF->GetFloat(PT_KEY_LAST_SESS_ENERGY_WH, 0.0f);
    _lastSession.duration_s  = (uint32_t)CONF->GetInt(PT_KEY_LAST_SESS_DURATION_S, 0);
    _lastSession.peakPower_W = CONF->GetFloat(PT_KEY_LAST_SESS_PEAK_W, 0.0f);
    _lastSession.peakCurrent_A = CONF->GetFloat(PT_KEY_LAST_SESS_PEAK_A, 0.0f);
    _lastSession.valid       = (_lastSession.duration_s > 0 || _lastSession.energy_Wh > 0.0f);
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
// Public API
// -----------------------------------------------------------------------------

void PowerTracker::begin() {
    loadFromNVS();
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

    // If continuous is not running, we still can fall back to last current,
    // but design intent is: Device ensures startContinuous() is active.
    if (!cs.isContinuousRunning()) {
        // Fallback: single-sample approximate integration.
        uint32_t now = millis();
        float I = fabsf(cs.getLastCurrent());
        if (_lastSampleTsMs == 0) {
            _lastSampleTsMs = now;
            return;
        }

        float dt_s = (now - _lastSampleTsMs) * 0.001f;
        _lastSampleTsMs = now;
        if (dt_s <= 0.0f) return;

        float netI = I - _idleCurrentA;
        if (netI < 0.0f) netI = 0.0f;

        if (_nominalBusV > 0.0f && netI > 0.0f) {
            float P = _nominalBusV * netI;
            float dE_Wh = (P * dt_s) / 3600.0f;
            _sessionEnergy_Wh     += dE_Wh;
            if (I > _sessionPeakCurrent_A) _sessionPeakCurrent_A = I;
            if (P > _sessionPeakPower_W)   _sessionPeakPower_W   = P;
        }
        return;
    }

    // Normal path: integrate from 10s history incrementally.
    CurrentSensor::Sample buf[64];
    uint32_t newSeq = _lastHistorySeq;
    size_t n = cs.getHistorySince(_lastHistorySeq, buf, (size_t)64, newSeq);

    if (n == 0) {
        // No new samples; nothing to do.
        return;
    }

    // Process samples in chronological order.
    for (size_t i = 0; i < n; ++i) {
        const uint32_t ts = buf[i].timestampMs;
        float I = fabsf(buf[i].currentA); // magnitude

        if (_lastSampleTsMs == 0) {
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
            const float P     = _nominalBusV * netI;       // W (approx)
            const float dE_Wh = (P * dt_s) / 3600.0f;      // Wh

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

    // Flush any remaining history via last known sensor (if available).
    // Caller should ideally have called update() just before endSession().
    uint32_t now = millis();
    uint32_t durMs = (now >= _startMs) ? (now - _startMs)
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

    DEBUG_PRINTF(
        "[PowerTracker] Session end (%s): E=%.4f Wh, dur=%lus, Ppk=%.2f W, Ipk=%.2f A\n",
        success ? "OK" : "ABORT",
        (double)s.energy_Wh,
        (unsigned long)s.duration_s,
        (double)s.peakPower_W,
        (double)s.peakCurrent_A
    );
}
