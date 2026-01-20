#include <PowerTracker.hpp>
#include <NVSManager.hpp>

#include <FS.h>
#include <SPIFFS.h>
#include <cbor.h>
#include <CborStream.hpp>
#include <vector>
#include <BusSampler.hpp>

namespace {
constexpr size_t kHistoryCborKeyMax = 32;
constexpr size_t kHistoryCborMaxBytes = 131072;

bool readCborUInt_(CborValue* it, uint64_t& value) {
    if (!cbor_value_is_integer(it)) return false;
    if (cbor_value_get_uint64(it, &value) != CborNoError) return false;
    return cbor_value_advance(it) == CborNoError;
}

bool readCborDouble_(CborValue* it, double& value) {
    if (cbor_value_is_double(it)) {
        if (cbor_value_get_double(it, &value) != CborNoError) return false;
        return cbor_value_advance(it) == CborNoError;
    }
    if (cbor_value_is_float(it)) {
        float tmp = 0.0f;
        if (cbor_value_get_float(it, &tmp) != CborNoError) return false;
        value = tmp;
        return cbor_value_advance(it) == CborNoError;
    }
    if (cbor_value_is_integer(it)) {
        int64_t iv = 0;
        if (cbor_value_get_int64(it, &iv) != CborNoError) return false;
        value = static_cast<double>(iv);
        return cbor_value_advance(it) == CborNoError;
    }
    return false;
}

bool skipCborValue_(CborValue* it) {
    return cbor_value_advance(it) == CborNoError;
}

bool advanceIfNull_(CborValue* it) {
    if (!cbor_value_is_null(it)) return false;
    return cbor_value_advance(it) == CborNoError;
}
} // namespace

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

    const uint16_t count = _historyCount;
    uint16_t validCount = 0;
    if (count > 0) {
        uint16_t idx = (_historyHead + POWERTRACKER_HISTORY_MAX - count) % POWERTRACKER_HISTORY_MAX;
        for (uint16_t i = 0; i < count; ++i) {
            if (_history[idx].valid) validCount++;
            idx = (idx + 1) % POWERTRACKER_HISTORY_MAX;
        }
    }

    bool ok = true;
    ok = ok && CborStream::writeMapHeader(f, 1);
    ok = ok && CborStream::writeText(f, "history");
    ok = ok && CborStream::writeArrayHeader(f, validCount);

    if (count > 0) {
        uint16_t idx = (_historyHead + POWERTRACKER_HISTORY_MAX - count) % POWERTRACKER_HISTORY_MAX;
        for (uint16_t i = 0; i < count && ok; ++i) {
            const HistoryEntry& h = _history[idx];
            if (h.valid) {
                ok = ok && CborStream::writeMapHeader(f, 5);
                ok = ok && CborStream::writeText(f, "start_ms");
                ok = ok && CborStream::writeUInt(f, h.startMs);
                ok = ok && CborStream::writeText(f, "duration_s");
                ok = ok && CborStream::writeUInt(f, h.stats.duration_s);
                ok = ok && CborStream::writeText(f, "energy_Wh");
                ok = ok && CborStream::writeFloatOrNull(f, h.stats.energy_Wh);
                ok = ok && CborStream::writeText(f, "peakPower_W");
                ok = ok && CborStream::writeFloatOrNull(f, h.stats.peakPower_W);
                ok = ok && CborStream::writeText(f, "peakCurrent_A");
                ok = ok && CborStream::writeFloatOrNull(f, h.stats.peakCurrent_A);
            }
            idx = (idx + 1) % POWERTRACKER_HISTORY_MAX;
        }
    }

    f.close();
    if (!ok) {
        DEBUG_PRINTLN("[PowerTracker] Failed to serialize history CBOR.");
        SPIFFS.remove(tmpPath);
        return false;
    }

    SPIFFS.remove(POWERTRACKER_HISTORY_FILE);
    if (!SPIFFS.rename(tmpPath, POWERTRACKER_HISTORY_FILE)) {
        DEBUG_PRINTLN("[PowerTracker] Failed to rename history temp file.");
        return false;
    }

    DEBUG_PRINTF("[PowerTracker] History saved (%u entries).\n", (unsigned)validCount);
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
        DEBUG_PRINTLN("[PowerTracker] No existing history file, starting empty.");
        return;
    }

    File f = SPIFFS.open(POWERTRACKER_HISTORY_FILE, "r");
    if (!f) {
        DEBUG_PRINTLN("[PowerTracker] Failed to open history file.");
        return;
    }

    const size_t size = f.size();
    if (size == 0 || size > kHistoryCborMaxBytes) {
        DEBUG_PRINTLN("[PowerTracker] History file size invalid.");
        f.close();
        return;
    }

    std::vector<uint8_t> buf(size);
    const size_t read = f.read(buf.data(), size);
    f.close();

    if (read != size) {
        DEBUG_PRINTLN("[PowerTracker] Failed to read history file.");
        return;
    }

    CborParser parser;
    CborValue it;
    if (cbor_parser_init(buf.data(), buf.size(), 0, &parser, &it) != CborNoError) {
        DEBUG_PRINTLN("[PowerTracker] Failed to parse history CBOR.");
        return;
    }

    if (!cbor_value_is_map(&it)) {
        DEBUG_PRINTLN("[PowerTracker] History CBOR root is not a map.");
        return;
    }

    CborValue mapIt;
    if (cbor_value_enter_container(&it, &mapIt) != CborNoError) {
        DEBUG_PRINTLN("[PowerTracker] Failed to enter history CBOR map.");
        return;
    }

    bool found = false;
    while (!cbor_value_at_end(&mapIt)) {
        if (!cbor_value_is_text_string(&mapIt)) {
            DEBUG_PRINTLN("[PowerTracker] Invalid history CBOR key.");
            return;
        }
        char key[kHistoryCborKeyMax];
        size_t keyLen = sizeof(key) - 1;
        if (cbor_value_copy_text_string(&mapIt, key, &keyLen, &mapIt) != CborNoError) {
            DEBUG_PRINTLN("[PowerTracker] Failed to read history CBOR key.");
            return;
        }
        key[keyLen] = '\0';

        if (strcmp(key, "history") == 0) {
            found = true;
            if (!cbor_value_is_array(&mapIt)) {
                DEBUG_PRINTLN("[PowerTracker] History CBOR missing array.");
                return;
            }
            CborValue arrIt;
            if (cbor_value_enter_container(&mapIt, &arrIt) != CborNoError) {
                DEBUG_PRINTLN("[PowerTracker] Failed to enter history array.");
                return;
            }
            while (!cbor_value_at_end(&arrIt)) {
                if (_historyCount >= POWERTRACKER_HISTORY_MAX) {
                    break;
                }
                if (!cbor_value_is_map(&arrIt)) {
                    if (!skipCborValue_(&arrIt)) {
                        DEBUG_PRINTLN("[PowerTracker] Failed to skip history item.");
                        return;
                    }
                    continue;
                }
                CborValue rowIt;
                if (cbor_value_enter_container(&arrIt, &rowIt) != CborNoError) {
                    DEBUG_PRINTLN("[PowerTracker] Failed to enter history row.");
                    return;
                }

                HistoryEntry e{};
                e.valid = true;

                while (!cbor_value_at_end(&rowIt)) {
                    if (!cbor_value_is_text_string(&rowIt)) {
                        DEBUG_PRINTLN("[PowerTracker] Invalid history row key.");
                        return;
                    }
                    char rowKey[kHistoryCborKeyMax];
                    size_t rowLen = sizeof(rowKey) - 1;
                    if (cbor_value_copy_text_string(&rowIt, rowKey, &rowLen, &rowIt) != CborNoError) {
                        DEBUG_PRINTLN("[PowerTracker] Failed to read history row key.");
                        return;
                    }
                    rowKey[rowLen] = '\0';

                    if (strcmp(rowKey, "start_ms") == 0 || strcmp(rowKey, "startMs") == 0) {
                        if (advanceIfNull_(&rowIt)) continue;
                        uint64_t v = 0;
                        if (readCborUInt_(&rowIt, v)) {
                            e.startMs = static_cast<uint32_t>(v);
                            continue;
                        }
                    } else if (strcmp(rowKey, "duration_s") == 0 || strcmp(rowKey, "durationS") == 0) {
                        if (advanceIfNull_(&rowIt)) continue;
                        uint64_t v = 0;
                        if (readCborUInt_(&rowIt, v)) {
                            e.stats.duration_s = static_cast<uint32_t>(v);
                            continue;
                        }
                    } else if (strcmp(rowKey, "energy_Wh") == 0 || strcmp(rowKey, "energyWh") == 0) {
                        if (advanceIfNull_(&rowIt)) continue;
                        double v = 0.0;
                        if (readCborDouble_(&rowIt, v)) {
                            e.stats.energy_Wh = static_cast<float>(v);
                            continue;
                        }
                    } else if (strcmp(rowKey, "peakPower_W") == 0 || strcmp(rowKey, "peakPowerW") == 0) {
                        if (advanceIfNull_(&rowIt)) continue;
                        double v = 0.0;
                        if (readCborDouble_(&rowIt, v)) {
                            e.stats.peakPower_W = static_cast<float>(v);
                            continue;
                        }
                    } else if (strcmp(rowKey, "peakCurrent_A") == 0 || strcmp(rowKey, "peakCurrentA") == 0) {
                        if (advanceIfNull_(&rowIt)) continue;
                        double v = 0.0;
                        if (readCborDouble_(&rowIt, v)) {
                            e.stats.peakCurrent_A = static_cast<float>(v);
                            continue;
                        }
                    }

                    if (!skipCborValue_(&rowIt)) {
                        DEBUG_PRINTLN("[PowerTracker] Failed to skip history row value.");
                        return;
                    }
                }

                if (cbor_value_leave_container(&arrIt, &rowIt) != CborNoError) {
                    DEBUG_PRINTLN("[PowerTracker] Failed to exit history row.");
                    return;
                }

                appendHistoryEntry(e);
            }
            if (cbor_value_leave_container(&mapIt, &arrIt) != CborNoError) {
                DEBUG_PRINTLN("[PowerTracker] Failed to exit history array.");
                return;
            }
        } else {
            if (!skipCborValue_(&mapIt)) {
                DEBUG_PRINTLN("[PowerTracker] Failed to skip history CBOR value.");
                return;
            }
        }
    }

    if (!found) {
        DEBUG_PRINTLN("[PowerTracker] History CBOR missing 'history' array.");
        return;
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
    _lastBusSeq           = 0;

    DEBUG_PRINTLN("[PowerTracker] Session started");
}

void PowerTracker::update() {
    if (!_active) return;

    if (!BUS_SAMPLER) {
        return;
    }

    BusSampler::Sample vbuf[64];
    uint32_t newBusSeq = _lastBusSeq;
    size_t nv = BUS_SAMPLER->getHistorySince(_lastBusSeq, vbuf, (size_t)64, newBusSeq);
    if (nv == 0) {
        _lastBusSeq = newBusSeq;
        return;
    }

    for (size_t i = 0; i < nv; ++i) {
        const uint32_t ts = vbuf[i].timestampMs;
        const float V = vbuf[i].voltageV;
        const float I = fabsf(vbuf[i].currentA);
        if (!isfinite(V) || !isfinite(I)) continue;

        if (ts < _startMs) {
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
        if (I <= 0.0f) continue;

        const float P     = V * I;
        const float dE_Wh = (P * dt_s) / 3600.0f;

        _sessionEnergy_Wh += dE_Wh;
        if (I > _sessionPeakCurrent_A) _sessionPeakCurrent_A = I;
        if (P > _sessionPeakPower_W)   _sessionPeakPower_W   = P;
    }

    _lastBusSeq = newBusSeq;
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
