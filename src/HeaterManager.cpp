/**************************************************************
 *  HeaterManager.cpp
 **************************************************************/

#include "HeaterManager.h"

// Static singleton pointer
HeaterManager* HeaterManager::s_instance = nullptr;

// NVS keys for per-wire resistance
static const char* WIRE_RES_KEYS[HeaterManager::kWireCount] = {
    R01OHM_KEY, R02OHM_KEY, R03OHM_KEY, R04OHM_KEY, R05OHM_KEY,
    R06OHM_KEY, R07OHM_KEY, R08OHM_KEY, R09OHM_KEY, R10OHM_KEY
};

// ==========================================================================
// Singleton access
// ==========================================================================

void HeaterManager::Init() {
    if (!s_instance) {
        s_instance = new HeaterManager();
    }
}

HeaterManager* HeaterManager::Get() {
    if (!s_instance) {
        s_instance = new HeaterManager();
    }
    return s_instance;
}

// ==========================================================================
// Constructor
// ==========================================================================

HeaterManager::HeaterManager()
    : wireOhmPerM(0.0f),
      targetResOhms(0.0f),
      _initialized(false),
      _mutex(nullptr),
      _currentMask(0),
      _historyHead(0),
      _historySeq(0)
{
    for (uint8_t i = 0; i < kWireCount; ++i) {
        wires[i].index              = i + 1;
        wires[i].resistanceOhm      = DEFAULT_WIRE_RES_OHMS;
        wires[i].lengthM            = 0.0f;
        wires[i].crossSectionAreaM2 = 0.0f;
        wires[i].volumeM3           = 0.0f;
        wires[i].massKg             = 0.0f;
        wires[i].temperatureC       = NAN;
        wires[i].connected          = true;   // default: unknown / not confirmed
        wires[i].presenceCurrentA   = 0.0f;
    }
}

// ==========================================================================
// Lifecycle
// ==========================================================================

void HeaterManager::begin() {
    if (_initialized) {
        return;
    }

    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                    Heater Manager Init                  #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    _mutex = xSemaphoreCreateMutex();

    // Configure all outputs as OFF.
    for (uint8_t i = 0; i < kWireCount; ++i) {
        pinMode(enaPins[i], OUTPUT);
        digitalWrite(enaPins[i], LOW);
    }
    _currentMask = 0;

    loadWireConfig();
    _initialized = true;
}

// ==========================================================================
// Lock helpers
// ==========================================================================

bool HeaterManager::lock() const {
    if (_mutex == nullptr) {
        return true;  // allow limited use before begin()
    }
    return (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE);
}

void HeaterManager::unlock() const {
    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
}

// ==========================================================================
// Load from NVS & geometry
// ==========================================================================

void HeaterManager::loadWireConfig() {
    if (CONF) {
        // Global Ω/m
        wireOhmPerM = CONF->GetFloat(WIRE_OHM_PER_M_KEY, DEFAULT_WIRE_OHM_PER_M);
        if (!isfinite(wireOhmPerM) || wireOhmPerM <= 0.0f) {
            wireOhmPerM = DEFAULT_WIRE_OHM_PER_M;
        }

        // Global target resistance
        targetResOhms = CONF->GetFloat(R0XTGT_KEY, DEFAULT_TARG_RES_OHMS);
        if (!isfinite(targetResOhms) || targetResOhms <= 0.0f) {
            targetResOhms = DEFAULT_TARG_RES_OHMS;
        }

        // Per-wire resistance
        for (uint8_t i = 0; i < kWireCount; ++i) {
            float r = CONF->GetFloat(WIRE_RES_KEYS[i], DEFAULT_WIRE_RES_OHMS);
            if (!isfinite(r) || r <= 0.01f) {
                r = DEFAULT_WIRE_RES_OHMS;
            }
            wires[i].resistanceOhm = r;
        }
    } else {
        // Fallback if NVS not ready
        wireOhmPerM   = DEFAULT_WIRE_OHM_PER_M;
        targetResOhms = DEFAULT_TARG_RES_OHMS;
        for (uint8_t i = 0; i < kWireCount; ++i) {
            wires[i].resistanceOhm = DEFAULT_WIRE_RES_OHMS;
        }
    }

    // Recompute geometry for each wire
    for (uint8_t i = 0; i < kWireCount; ++i) {
        computeWireGeometry(wires[i]);
    }

    DEBUG_PRINTF("[HeaterManager] Ω/m = %.4f | TargetR = %.3f Ω\n",
                 wireOhmPerM, targetResOhms);

    DEBUGGSTART();
    for (uint8_t i = 0; i < kWireCount; ++i) {
        const WireInfo& w = wires[i];

        const float areaMm2  = w.crossSectionAreaM2 * 1.0e6f;  // m² → mm²
        const float volumeCm3 = w.volumeM3 * 1.0e6f;           // m³ → cm³
        const float massG    = w.massKg * 1000.0f;             // kg → g

        DEBUG_PRINTF(
            "[HeaterManager] Wire %u: R=%.2f Ω | L=%.3f m | A=%.3f mm² | "
            "V=%.3f cm³ | m=%.3f g\n",
            w.index,
            w.resistanceOhm,
            w.lengthM,
            areaMm2,
            volumeCm3,
            massG
        );
    }
    DEBUGGSTOP();

}

// Compute derived geometric/thermal properties for one wire.
void HeaterManager::computeWireGeometry(WireInfo& w) {
    const float R = w.resistanceOhm;

    if (!isfinite(R) || R <= 0.0f ||
        !isfinite(wireOhmPerM) || wireOhmPerM <= 0.0f)
    {
        w.lengthM            = 0.0f;
        w.crossSectionAreaM2 = 0.0f;
        w.volumeM3           = 0.0f;
        w.massKg             = 0.0f;
        return;
    }

    const float A = NICHROME_RESISTIVITY / wireOhmPerM; // m²
    const float L = R / wireOhmPerM;                    // m
    const float V = A * L;                              // m³
    const float m = NICHROME_DENSITY * V;               // kg

    w.lengthM            = (isfinite(L) && L > 0.0f) ? L : 0.0f;
    w.crossSectionAreaM2 = (isfinite(A) && A > 0.0f) ? A : 0.0f;
    w.volumeM3           = (isfinite(V) && V > 0.0f) ? V : 0.0f;
    w.massKg             = (isfinite(m) && m > 0.0f) ? m : 0.0f;
}

// ==========================================================================
// Output control (single-channel)
// ==========================================================================

void HeaterManager::setOutput(uint8_t index, bool enable) {
    if (index == 0 || index > kWireCount) {
        return;
    }
    const uint8_t bit = index - 1;

    if (!lock()) {
        return;
    }

    uint16_t newMask = _currentMask;
    if (enable) {
        newMask |= (1u << bit);
    } else {
        newMask &= ~(1u << bit);
    }

    // Update hardware pin
    digitalWrite(enaPins[bit], enable ? HIGH : LOW);

    // If effective mask changed, log it
    if (newMask != _currentMask) {
        _currentMask = newMask;
        logOutputMaskChange(_currentMask);
    }

    unlock();
}

void HeaterManager::disableAll() {
    if (!lock()) {
        return;
    }

    if (_currentMask != 0) {
        // Only touch pins if anything is on
        for (uint8_t i = 0; i < kWireCount; ++i) {
            digitalWrite(enaPins[i], LOW);
        }
        _currentMask = 0;
        logOutputMaskChange(0);
    }

    unlock();
}

bool HeaterManager::getOutputState(uint8_t index) const {
    if (index == 0 || index > kWireCount) {
        return false;
    }
    const uint8_t bit = index - 1;

    if (!lock()) {
        return false;
    }

    // We trust _currentMask as the source of truth.
    const bool state = ((_currentMask & (1u << bit)) != 0);

    unlock();
    return state;
}

// ==========================================================================
// Output control (mask-based)
// ==========================================================================

void HeaterManager::setOutputMask(uint16_t mask) {
    // Only 10 bits are meaningful
    mask &= ((1u << kWireCount) - 1u);

    if (!lock()) {
        return;
    }

    if (mask == _currentMask) {
        // Nothing to do
        unlock();
        return;
    }

    // Update only changed pins to minimize glitches
    uint16_t diff = mask ^ _currentMask;
    for (uint8_t i = 0; i < kWireCount; ++i) {
        uint16_t bit = (1u << i);
        if (diff & bit) {
            bool on = (mask & bit) != 0;
            digitalWrite(enaPins[i], on ? HIGH : LOW);
        }
    }

    _currentMask = mask;
    logOutputMaskChange(_currentMask);

    unlock();
}

uint16_t HeaterManager::getOutputMask() const {
    if (!lock()) {
        return 0;
    }
    uint16_t m = _currentMask;
    unlock();
    return m;
}

// ==========================================================================
// Output history API
// ==========================================================================

void HeaterManager::logOutputMaskChange(uint16_t newMask) {
    // Assumes _mutex is already held.
    // Do not record duplicate entries with same mask (should be guaranteed
    // by callers, but we guard anyway).
    if (_historySeq > 0) {
        const uint32_t lastIdx = (_historyHead == 0)
                               ? (OUTPUT_HISTORY_SIZE - 1)
                               : (_historyHead - 1) % OUTPUT_HISTORY_SIZE;
        if (_history[lastIdx].mask == newMask) {
            return;
        }
    }

    const uint32_t idx = _historyHead % OUTPUT_HISTORY_SIZE;
    _history[idx].timestampMs = millis();
    _history[idx].mask        = newMask;

    _historyHead++;
    _historySeq++;
}

size_t HeaterManager::getOutputHistorySince(uint32_t lastSeq,
                                            OutputEvent* out,
                                            size_t maxOut,
                                            uint32_t& newSeq) const
{
    if (!out || maxOut == 0) {
        newSeq = lastSeq;
        return 0;
    }

    if (!const_cast<HeaterManager*>(this)->lock()) {
        newSeq = lastSeq;
        return 0;
    }

    const uint32_t seqNow = _historySeq;

    if (seqNow == 0) {
        // No events yet
        unlock();
        newSeq = 0;
        return 0;
    }

    // Oldest sequence index still available in the ring
    const uint32_t maxSpan = (seqNow > OUTPUT_HISTORY_SIZE)
                           ? OUTPUT_HISTORY_SIZE
                           : seqNow;
    const uint32_t minSeq = seqNow - maxSpan;

    // Clamp lastSeq inside available window
    if (lastSeq < minSeq) {
        lastSeq = minSeq;
    }
    if (lastSeq > seqNow) {
        lastSeq = seqNow;
    }

    uint32_t available = seqNow - lastSeq;
    if (available > maxOut) {
        available = maxOut;
    }

    for (uint32_t i = 0; i < available; ++i) {
        uint32_t sSeq = lastSeq + i;
        uint32_t idx  = sSeq % OUTPUT_HISTORY_SIZE;
        out[i] = _history[idx];
    }

    newSeq = lastSeq + available;

    unlock();
    return static_cast<size_t>(available);
}

// ==========================================================================
// Resistance / target configuration
// ==========================================================================

void HeaterManager::setWireResistance(uint8_t index, float ohms) {
    if (index == 0 || index > kWireCount) return;
    if (!isfinite(ohms) || ohms <= 0.01f) return;

    const uint8_t i = index - 1;

    if (!lock()) return;

    wires[i].resistanceOhm = ohms;
    computeWireGeometry(wires[i]);

    if (CONF) {
        CONF->PutFloat(WIRE_RES_KEYS[i], ohms);
    }

    unlock();
}

float HeaterManager::getWireResistance(uint8_t index) const {
    if (index == 0 || index > kWireCount) return 0.0f;
    const uint8_t i = index - 1;

    if (!lock()) return 0.0f;

    float r = wires[i].resistanceOhm;

    unlock();
    return r;
}

void HeaterManager::setTargetResistanceAll(float ohms) {
    if (!isfinite(ohms) || ohms <= 0.0f) return;
    if (!lock()) return;

    targetResOhms = ohms;

    if (CONF) {
        CONF->PutFloat(R0XTGT_KEY, ohms);
    }

    unlock();
}

// ==========================================================================
// Wire info / temperature
// ==========================================================================

WireInfo HeaterManager::getWireInfo(uint8_t index) const {
    WireInfo out{};
    out.index = 0;

    if (index == 0 || index > kWireCount) {
        return out;
    }

    const uint8_t i = index - 1;

    if (!lock()) return out;

    out = wires[i];

    unlock();
    return out;
}

void HeaterManager::setWireEstimatedTemp(uint8_t index, float tempC) {
    if (index == 0 || index > kWireCount) return;
    const uint8_t i = index - 1;

    if (!lock()) return;

    wires[i].temperatureC = tempC;

    unlock();
}

float HeaterManager::getWireEstimatedTemp(uint8_t index) const {
    if (index == 0 || index > kWireCount) {
        return NAN;
    }
    const uint8_t i = index - 1;

    if (!lock()) return NAN;

    float t = wires[i].temperatureC;

    unlock();
    return t;
}

void HeaterManager::resetAllEstimatedTemps(float ambientC) {
    if (!lock()) return;

    for (uint8_t i = 0; i < kWireCount; ++i) {
        wires[i].temperatureC = ambientC;
    }

    unlock();
}

void HeaterManager::probeWirePresence(CurrentSensor& cs,
                                      float busVoltage,
                                      float minValidFraction,
                                      float maxValidFraction,
                                      uint16_t settleMs,
                                      uint8_t samples)
{
    // 1) Resolve bus voltage
    if (busVoltage <= 0.0f && CONF) {
        busVoltage = CONF->GetFloat(DC_VOLTAGE_KEY, 0.0f);
        if (busVoltage <= 0.0f) {
            busVoltage = CONF->GetFloat(DESIRED_OUTPUT_VOLTAGE_KEY, 0.0f);
        }
    }

    if (busVoltage <= 0.0f) {
        DEBUG_PRINTLN("[HeaterManager] probeWirePresence: No valid bus voltage, abort.");
        return;
    }

    // 2) Snapshot current states
    bool prevStates[kWireCount];
    for (uint8_t i = 0; i < kWireCount; ++i) {
        prevStates[i] = getOutputState(i + 1);
    }

    // Ensure all OFF for clean measurements
    setOutputMask(0);
    delay(settleMs);

    DEBUGGSTART();
    DEBUG_PRINTF("[HeaterManager] Probing wire presence at %.2f V\n", busVoltage);

    for (uint8_t idx = 0; idx < kWireCount; ++idx) {
        WireInfo& w = wires[idx];

        // Skip nonsense resistances
        if (!isfinite(w.resistanceOhm) || w.resistanceOhm <= 0.01f) {
            w.connected        = false;
            w.presenceCurrentA = 0.0f;
            DEBUG_PRINTF("  CH%u: skipped (invalid R=%.3f Ω)\n",
                         w.index, w.resistanceOhm);
            continue;
        }

        // 3) Enable only this channel
        uint16_t mask = (1u << idx);
        setOutputMask(mask);
        delay(settleMs);

        // 4) Measure average current
        float sumA = 0.0f;
        for (uint8_t s = 0; s < samples; ++s) {
            sumA += cs.readCurrent();
            delay(2);
        }
        float avgA = sumA / (float)samples;

        float expectedA = busVoltage / w.resistanceOhm;
        if (expectedA < 0.01f) expectedA = 0.01f;

        float ratio = avgA / expectedA;

        bool connected;
        if (!isfinite(avgA) || avgA < 0.01f) {
            // basically no current → open
            connected = false;
        } else if (ratio < minValidFraction) {
            // too low vs expected → open/bad contact
            connected = false;
        } else if (ratio > maxValidFraction) {
            // too high → suspicious (short / wrong value); treat as not OK
            connected = false;
        } else {
            // in band → looks like a real load
            connected = true;
        }

        w.connected        = connected;
        w.presenceCurrentA = avgA;

        DEBUG_PRINTF("  CH%u: I=%.3f A, R=%.3f Ω, Iexp=%.3f A, ratio=%.2f => %s\n",
                     w.index,
                     avgA,
                     w.resistanceOhm,
                     expectedA,
                     ratio,
                     connected ? "CONNECTED" : "OPEN/FAULT");

        // 5) Turn this channel off before the next one
        setOutputMask(0);
        delay(settleMs);
    }

    // 6) Restore previous states
    for (uint8_t i = 0; i < kWireCount; ++i) {
        if (prevStates[i]) {
            setOutput(i + 1, true);
        }
    }

    DEBUGGSTOP();
}
void HeaterManager::updatePresenceFromMask(uint16_t mask,
                                           float totalCurrentA,
                                           float busVoltage,
                                           float minValidRatio)
{
    if (mask == 0) return;
    if (totalCurrentA < 0.0f) totalCurrentA = 0.0f;

    // Resolve bus voltage if not provided
    if (busVoltage <= 0.0f && CONF) {
        busVoltage = CONF->GetFloat(DC_VOLTAGE_KEY, 0.0f);
        if (busVoltage <= 0.0f) {
            busVoltage = CONF->GetFloat(DESIRED_OUTPUT_VOLTAGE_KEY, 0.0f);
        }
    }
    if (busVoltage <= 0.0f) return;

    // Expected total current from wires in mask that are still "connected"
    float G = 0.0f;
    for (uint8_t i = 0; i < kWireCount; ++i) {
        if (mask & (1u << i)) {
            const WireInfo& w = wires[i];
            if (!w.connected) continue;          // already considered missing
            const float R = w.resistanceOhm;
            if (R > 0.01f && isfinite(R)) {
                G += 1.0f / R;
            }
        }
    }
    if (G <= 0.0f) return;

    const float expectedI = busVoltage * G;
    if (expectedI <= 0.0f) return;

    const float ratio = totalCurrentA / expectedI;

    // If we're far below expected current, treat these wires as gone.
    if (!isfinite(ratio) || ratio < minValidRatio) {
        for (uint8_t i = 0; i < kWireCount; ++i) {
            if (mask & (1u << i)) {
                wires[i].connected        = false;
                wires[i].presenceCurrentA = totalCurrentA;
                DEBUG_PRINTF(
                    "[HeaterManager] Wire %u marked NO-PRESENCE "
                    "(I=%.3fA, Iexp=%.3fA, ratio=%.2f)\n",
                    wires[i].index,
                    totalCurrentA,
                    expectedI,
                    ratio
                );
            }
        }
    }
}

bool HeaterManager::hasAnyConnected() const {
    for (uint8_t i = 0; i < kWireCount; ++i) {
        if (wires[i].connected) return true;
    }
    return false;
}
