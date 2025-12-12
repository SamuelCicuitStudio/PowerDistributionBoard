/**************************************************************
 *  HeaterManager.cpp
 **************************************************************/

#include "control/HeaterManager.h"
#include "system/Device.h"
#include <math.h>
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
      wireGaugeAwg(DEFAULT_WIRE_GAUGE),
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
        wires[i].lastOnMs           = 0;
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

        // Global wire gauge (AWG)
        wireGaugeAwg = CONF->GetInt(WIRE_GAUGE_KEY, DEFAULT_WIRE_GAUGE);
        if (wireGaugeAwg <= 0 || wireGaugeAwg > kMaxAwg) {
            wireGaugeAwg = DEFAULT_WIRE_GAUGE;
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
        wireGaugeAwg  = DEFAULT_WIRE_GAUGE;
        for (uint8_t i = 0; i < kWireCount; ++i) {
            wires[i].resistanceOhm = DEFAULT_WIRE_RES_OHMS;
        }
    }

    // Recompute geometry for each wire
    for (uint8_t i = 0; i < kWireCount; ++i) {
        computeWireGeometry(wires[i]);
    }

    DEBUG_PRINTF("[HeaterManager] O/m = %.4f | TargetR = %.3f O\n",
                 wireOhmPerM, targetResOhms);

    DEBUGGSTART();
    for (uint8_t i = 0; i < kWireCount; ++i) {
        const WireInfo& w = wires[i];

        const float areaMm2  = w.crossSectionAreaM2 * 1.0e6f;  // m² → mm²
        const float volumeCm3 = w.volumeM3 * 1.0e6f;           // m³ → cm³
        const float massG    = w.massKg * 1000.0f;             // kg → g

        DEBUG_PRINTF(
            "[HeaterManager] Wire %u: R=%.2f O | L=%.3f m | A=%.3f mm² | "
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

    auto awgToDiameterMeters = [](int awg) -> float {
        // Standard AWG formula: d_inch = 0.005 * 92^((36-AWG)/39)
        // Convert to meters.
        if (awg <= 0 || awg > kMaxAwg) return NAN;
        const float d_inch = 0.005f * powf(92.0f, (36.0f - static_cast<float>(awg)) / 39.0f);
        return d_inch * 0.0254f; // inch -> m
    };

    if (!isfinite(R) || R <= 0.0f) {
        w.lengthM            = 0.0f;
        w.crossSectionAreaM2 = 0.0f;
        w.volumeM3           = 0.0f;
        w.massKg             = 0.0f;
        return;
    }

    // Prefer AWG-derived cross-section if gauge is valid; fall back to Ω/m.
    float areaM2 = NAN;
    const float dM = awgToDiameterMeters(wireGaugeAwg);
    if (isfinite(dM) && dM > 0.0f) {
        const float pi = 3.14159265359f;
        areaM2 = pi * 0.25f * dM * dM;
    }
    if (!isfinite(areaM2) || areaM2 <= 0.0f) {
        if (isfinite(wireOhmPerM) && wireOhmPerM > 0.0f) {
            areaM2 = NICHROME_RESISTIVITY / wireOhmPerM;
        }
    }
    if (!isfinite(areaM2) || areaM2 <= 0.0f) {
        w.lengthM            = 0.0f;
        w.crossSectionAreaM2 = 0.0f;
        w.volumeM3           = 0.0f;
        w.massKg             = 0.0f;
        return;
    }

    // Use resistivity + cross-section to derive length from measured R.
    const float L = (isfinite(R) && R > 0.0f)
                        ? (R * areaM2 / NICHROME_RESISTIVITY)
                        : 0.0f;               // m
    const float V = areaM2 * L;               // m³
    const float m = NICHROME_DENSITY * V;     // kg

    w.lengthM            = (isfinite(L) && L > 0.0f) ? L : 0.0f;
    w.crossSectionAreaM2 = (isfinite(areaM2) && areaM2 > 0.0f) ? areaM2 : 0.0f;
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
    const uint16_t oldMask = _currentMask;
    if (enable) {
        newMask |= (1u << bit);
    } else {
        newMask &= ~(1u << bit);
    }

    // Record when this wire was turned ON (edge detection).
    if (enable && !(oldMask & (1u << bit))) {
        wires[bit].lastOnMs = millis();
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
    // Hard gate: never allow mask if device not running
    if (!DEVICE || DEVICE->getState() != DeviceState::Running) {
        mask = 0;
    }
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

    const uint16_t oldMask = _currentMask;

    // Update only changed pins to minimize glitches
    uint16_t diff = mask ^ _currentMask;
    for (uint8_t i = 0; i < kWireCount; ++i) {
        uint16_t bit = (1u << i);
        if (diff & bit) {
            bool on = (mask & bit) != 0;
            digitalWrite(enaPins[i], on ? HIGH : LOW);
            if (on && !(oldMask & bit)) {
                wires[i].lastOnMs = millis();
            }
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

void HeaterManager::setWireGaugeAwg(int awg) {
    if (awg <= 0 || awg > kMaxAwg) return;
    if (!lock()) return;
    wireGaugeAwg = awg;
    // Recompute geometry with the new cross-section.
    for (uint8_t i = 0; i < kWireCount; ++i) {
        computeWireGeometry(wires[i]);
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

void HeaterManager::setWirePresence(uint8_t index, bool connected, float presenceCurrentA) {
    if (index == 0 || index > kWireCount) return;
    const uint8_t i = index - 1;

    if (!lock()) return;

    wires[i].connected        = connected;
    wires[i].presenceCurrentA = presenceCurrentA;

    unlock();
}

void HeaterManager::probeWirePresence(CurrentSensor& cs,
                                      float busVoltage,
                                      float minValidFraction,
                                      float maxValidFraction,
                                      uint16_t settleMs,
                                      uint8_t samples)
{
    Device* dev = DEVICE;
    if (!dev) {
        return;
    }

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

    dev->getWirePresenceManager().probeAll(*this,
                                           dev->getWireStateModel(),
                                           cs,
                                           busVoltage,
                                           minValidFraction,
                                           maxValidFraction,
                                           settleMs,
                                           samples);
}

void HeaterManager::updatePresenceFromMask(uint16_t mask,
                                           float totalCurrentA,
                                           float busVoltage,
                                           float minValidRatio)
{
    Device* dev = DEVICE;
    if (!dev || mask == 0) {
        return;
    }
    if (totalCurrentA < 0.0f) {
        totalCurrentA = 0.0f;
    }

    if (busVoltage <= 0.0f && CONF) {
        busVoltage = CONF->GetFloat(DC_VOLTAGE_KEY, 0.0f);
        if (busVoltage <= 0.0f) {
            busVoltage = CONF->GetFloat(DESIRED_OUTPUT_VOLTAGE_KEY, 0.0f);
        }
    }
    if (busVoltage <= 0.0f) {
        return;
    }

    dev->getWirePresenceManager().updatePresenceFromMask(*this,
                                                         dev->getWireStateModel(),
                                                         mask,
                                                         totalCurrentA,
                                                         busVoltage,
                                                         minValidRatio);
}

bool HeaterManager::hasAnyConnected() const {
    for (uint8_t i = 0; i < kWireCount; ++i) {
        if (wires[i].connected) return true;
    }
    return false;
}
